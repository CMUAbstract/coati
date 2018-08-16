#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <libmsp/mem.h>
#include <msp430.h>
#include "coati.h"
#include "filter.h"
#include "tx.h"
#include "event.h"
#include "top_half.h"
#include <signal.h>

#ifndef LIBCOATIGCC_ENABLE_DIAGNOSTICS
#define LCG_PRINTF(...)
#else
#define LCG_PRINTF printf
#endif


__nv uint8_t ev_buf[EV_BUF_SIZE];
__nv void * ev_src[EV_NUM_DIRTY_ENTRIES];
__nv void * ev_dst[EV_NUM_DIRTY_ENTRIES];
__nv size_t ev_size[EV_NUM_DIRTY_ENTRIES];
#ifdef LIBCOATIGCC_BUFFER_ALL
__nv void * ev_read_list[EV_NUM_DIRTY_ENTRIES];
__nv void * ev_write_list[EV_NUM_DIRTY_ENTRIES];

__nv task_t * cur_tx_start;

volatile uint16_t num_evread = 0;
volatile uint16_t num_evwrite = 0;
#else
/*__nv the_event_ctx = {
  .task = NULL,
  .extra_state = &bh_state_0,
  .extra_ev_state = &bh_state_ev_0,
  .commit_state = EV_PH1
};*/

#endif // BUFFER_ALL

// Only needs to be a pointer when we're using full buffering because then we've
// only got one event context at any given time so the context swap inside
// events doesn't happen repeatedly and overwrite our return address.
#ifdef LIBCOATIGCC_BUFFER_ALL
__nv context_t * thread_ctx;
#else
__nv context_t  thread_ctx;
#endif // BUFFER_ALL

volatile uint16_t num_evbe = 0;

__nv ev_state state_ev_1 = {0};
__nv ev_state state_ev_0 = {
    .num_devv = 0,
#ifdef LIBCOATIGCC_BUFFER_ALL
    .num_read = 0,
    .num_write = 0,
#endif // BUFFER_ALL
    .in_ev = 0,
#ifdef LIBCOATIGCC_BUFFER_ALL
  .ev_need_commit = 0
#else
  .count = 0,
  .committed = 0
#endif // BUFFER_ALL
};


// For instrumentation
__nv uint16_t _numEvents_uncommitted = 0;

// Need to use totally different functions depending on whether we're buffering
// or not
#ifdef LIBCOATIGCC_BUFFER_ALL
/*
 * @brief function to act as interrupt handler when an event interrupt is
 * triggered
 */
void event_handler(context_t *new_event_ctx) {
  //TRANS_TIMER_START
  // Disable all event interrupts but enable global interrupts
  // NEEDS TO BE DONE IN THAT ORDER!!!!!!!
  _disable_events();
  __enable_interrupt();
  LCG_PRINTF("in event handler!\r\n");
  // Quick sanity check to make sure context hasn't been corrupted... if we're
  // here, it's DEFINITELY an event, so the in_ev bit had better be set.
  if(((ev_state *)new_event_ctx->extra_ev_state)->in_ev == 0){
    printf("Error! event context corrupted, in_ev = 0\r\n");
    while(1);
  }

  // Clear commit flag for this context
  // TODO add in some error checking
  ((ev_state *)new_event_ctx->extra_ev_state)->ev_need_commit = 0;
  ((ev_state *)new_event_ctx->extra_ev_state)->num_devv =
                              ((ev_state *)curctx->extra_ev_state)->num_devv;
  // Clear event buffer counters but don't turn on curctx->in_ev! If we died
  // after seeting that, events would continously be disabled and they'd never
  // get turned back on
  num_evbe = 0;
  num_evread = 0;
  num_evwrite = 0;

  // Pass across the number of variables in the ev buffer from the threadctx
  // to the active eventctx so that the next event can access the old
  // variables (it's fine to leave this unprotected from a power standpoint,
  // it'll just get rewritten if we power down
  ((ev_state *)new_event_ctx->extra_ev_state)->num_devv =
      ((ev_state *)curctx->extra_ev_state)->num_devv;
  LCG_PRINTF("In event handler! coming from %x \r\n",curctx->task->func);

  // Point threads' context at current context
  thread_ctx=curctx;
  LCG_PRINTF("Double check in:  %x, in tx: %u \r\n",thread_ctx->task->func,
  ((tx_state *)thread_ctx->extra_state)->in_tx);
  //printf("SR:%x\r\n",READ_SP);
  // Set curctx with prepackaged event_ctx
  curctx = new_event_ctx;
  //printf("h: ");
  TRANS_TIMER_STOP
  __asm__ volatile ( // volatile because output operands unused by C
        "mov #0x2400, r1\n"
        "br %[ntask]\n"
        :
        : [ntask] "r" (curctx->task->func)
        );
}
#else
// Defined new function here
/*
 * @brief function that starts walking the queue of events
 */
void queued_event_handoff(void) {
  _disable_events();
  LCG_PRINTF("in event handler!\r\n");
  context_t *next_ctx;
  tx_state *new_tx_state;
  ev_state *new_ev_state;

  next_ctx = (curctx == context_ptr0 ? context_ptr1 : context_ptr0);

  new_tx_state = (curctx->extra_state == &state_0 ? &state_1 : &state_0);

  new_ev_state = (curctx->extra_ev_state == &state_ev_0 ? &state_ev_1 :
                  &state_ev_0);

  // Transfer in tx value
  new_tx_state->in_tx = ((tx_state *)curctx->extra_state)->in_tx;
  // Clear num_devv and set in_ev flag
  new_ev_state->num_devv = 0;
  new_ev_state->in_ev = 1;
  next_ctx->extra_ev_state = new_ev_state;
  next_ctx->commit_state = NO_COMMIT;
  next_ctx->task = event_queue.tasks[1];
  num_evbe = 0;

  LCG_PRINTF("In event queue! coming from %x, going to %x \r\n",
          curctx->task->func, next_ctx->task->func);
  // Copy contents of curctx to threadctx
  thread_ctx.task = curctx->task;
  // Double check that this actually works as expected, we probably need to set
  // this commit state to NO_COMMIT on ret from deferred events anyway
  thread_ctx.commit_state = curctx->commit_state;
  // Set curctx with prepackaged event_ctx
  curctx = next_ctx;
  // Post check
  LCG_PRINTF("curctx func %x, thread_ctx func %x \r\n",
          curctx->task->func, thread_ctx.task->func);
  // No timer start in this function because we are always calling this function
  // from a function that has a TIMER_START call
  TRANS_TIMER_STOP
  __asm__ volatile ( // volatile because output operands unused by C
        "mov #0x2400, r1\n"
        "br %[ntask]\n"
        :
        : [ntask] "r" (curctx->task->func)
        );

}
#endif // BUFFER_ALL

/*
 * @brief returns the index into the tx buffers of where the data is located
 */
int16_t  ev_find(const void * addr) {
    int16_t num_vars = 0;
    num_vars = ((ev_state *)curctx->extra_ev_state)->num_devv + num_evbe;
    if(num_vars) {
      for(int i = 0; i < num_vars; i++) {
      #ifdef LIBCOATIGCC_TEST_COUNT
        access_len++;
      #endif
          //LCG_PRINTF("Checking %x \r\n",ev_dirty_src[i]);
          if(addr == ev_src[i])
              return i;
      }
    }
    return -1;
}

/*
 * @brief returns the pointer into the tx dirty buf where the data is located
 */
void *  ev_get_dst(void * addr) {
    int16_t num_vars = 0;
    num_vars = ((ev_state *)curctx->extra_ev_state)->num_devv + num_evbe;
    if(num_vars) {
      for(int i = 0; i < num_vars; i++) {
          if(addr == ev_src[i])
              return ev_dst[i];
      }
    }
    return NULL;
}


/*
 * @brief: commit values touched during events back to main memory
 */
void ev_commit_ph2() {
    // Copy all tx buff entries to main memory
    LCG_PRINTF("Running ev_commit! should commit %u\r\n",
    ((ev_state*)(curctx->extra_ev_state))->num_devv);
    while(((ev_state *)(curctx->extra_ev_state))->num_devv > 0) {
        uint16_t num_devv = ((ev_state *)(curctx->extra_ev_state))->num_devv;
        LCG_PRINTF("Copying %i th time from %x to %x \r\n", num_devv-1,
        ev_src[num_devv-1],
        ev_dst[num_devv - 1]);
        memcpy( ev_src[num_devv -1],
                ev_dst[num_devv - 1],
                ev_size[num_devv - 1]
        );
        ((ev_state *)(curctx->extra_ev_state))->num_devv--;
    }
#ifdef LIBCOATIGCC_BUFFER_ALL
    ((ev_state *)(curctx->extra_ev_state))->ev_need_commit = 0;
#endif
}




/*
 * @brief allocs space in event buffer and returns a pointer to the spot,
 * returns NULL if buf is out of space
 */
void * ev_buf_alloc(void * addr, size_t size) {
    uint16_t new_ptr;
    LCG_PRINTF("In alloc! num_evbe = %i, buf = %x\r\n",num_evbe, ev_buf);
    uint16_t num_vars = 0;
    num_vars = ((ev_state *)curctx->extra_ev_state)->num_devv + num_evbe;
    if(num_vars) {
        new_ptr = (uint8_t *) ev_dst[num_vars - 1] +
        ev_size[num_vars - 1];
    }
    else {
        new_ptr = (uint8_t *) ev_buf;
    }
    // Fix alignment struggles
    if(size == 2) {
      while(new_ptr & 0x1)
        new_ptr++;
    }
    if(size == 4) {
      while(new_ptr & 0x11)
        new_ptr++;
    }
    if(new_ptr + size > (unsigned) (ev_buf + EV_BUF_SIZE)) {
        LCG_PRINTF("asking for %u, only have %u \r\n", new_ptr + size,
        (unsigned) (ev_buf + EV_BUF_SIZE));
        return NULL;
    }
    else {
        num_evbe++;
        num_vars++;
        LCG_PRINTF("ev new src = %x new dst = %x index %i\r\n", addr, new_ptr,
        num_vars);
        ev_src[num_vars - 1] = addr;
        ev_dst[num_vars - 1] = (void *) new_ptr;
        ev_size[num_vars - 1] = size;
    }
    return (void *) new_ptr;
}

