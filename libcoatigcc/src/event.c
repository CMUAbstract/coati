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

__nv uint8_t ev_buf[BUF_SIZE];
__nv uint16_t ev_buf_level = 0;
__nv table_t ev_table = {.bucket_len = {0}, .active_bins = 0};

__nv size_t table_sizes[NUM_BINS];
__nv size_t src_table_sizes[NUM_BINS];

#ifdef LIBCOATIGCC_BUFFER_ALL

#ifdef LIBCOATIGCC_SER_TX_AFTER
__nv src_table ev_write_table;
#else
__nv src_table ev_read_table;
#endif // SER_TX_AFTER

__nv task_t * cur_tx_start;

#endif // BUFFER_ALL

// Only needs to be a pointer when we're using full buffering because then we've
// only got one event context at any given time so the context swap inside
// events doesn't happen repeatedly and overwrite our return address.
#ifdef LIBCOATIGCC_BUFFER_ALL
__nv context_t * thread_ctx;
#else
__nv context_t  thread_ctx;
#endif // BUFFER_ALL


__nv ev_state state_ev_1 = {0};
__nv ev_state state_ev_0 = {
    .in_ev = 0,
#ifdef LIBCOATIGCC_BUFFER_ALL
  .ev_need_commit = 0,
  .perm_sizes = table_sizes,
  .src_sizes = src_table_sizes,
  .perm_buf_level = 0
#else
  .count = 0,
  .committed = 0,
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
  ((ev_state *)curctx->extra_ev_state)->ev_need_commit = 0;
  // Set all of the event_buffer/src_table lengths based on the stuff stored in
  // the permanent lengths from the current ev state
  ev_buf_level = ((ev_state *)curctx->extra_ev_state)->perm_buf_level;
  for(int i = 0; i < NUM_BINS; i++) {
    ev_table.bucket_len[i] = 
            ((ev_state*)curctx->extra_ev_state)->perm_sizes[i];
  }
  for(int i = 0; i < NUM_BINS; i++) {
    #ifdef LIBCOATIGCC_SER_TX_AFTER
    ev_write_table.bucket_len[i] =
            ((ev_state*)curctx->extra_ev_state)->src_sizes[i];
    #else
    ev_read_table.bucket_len[i] =
            ((ev_state*)curctx->extra_ev_state)->src_sizes[i];
    #endif // SER_TX_AFTER
  }
  // Point threads' context at current context
  thread_ctx=curctx;
  LCG_PRINTF("Double check in:  %x, in tx: %u \r\n",thread_ctx->task->func,
  ((tx_state *)thread_ctx->extra_state)->in_tx);
  
  // Set curctx with prepackaged event_ctx
  curctx = new_event_ctx;
  
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
  context_t *next_ctx;
  tx_state *new_tx_state;
  ev_state *new_ev_state;

  next_ctx = (curctx == context_ptr0 ? context_ptr1 : context_ptr0);

  new_tx_state = (curctx->extra_state == &state_0 ? &state_1 : &state_0);

  new_ev_state = (curctx->extra_ev_state == &state_ev_0 ? &state_ev_1 :
                  &state_ev_0);

  // Transfer in tx value
  new_tx_state->in_tx = ((tx_state *)curctx->extra_state)->in_tx;
  // TODO remove this check once we confirm that the lengths are always 0
  if(ev_buf_level) {
    printf("Error! buf level isn't 0");
    while(1);
  }
  for(int i = 0; i < NUM_BINS; i++) {
    if(ev_table.bucket_len[i] == 0) {
      printf("Error! bucket len isn't 0");
      while(1);
    }
  }
  new_ev_state->in_ev = 1;
  next_ctx->extra_ev_state = new_ev_state;
  next_ctx->commit_state = NO_COMMIT;
  next_ctx->task = event_queue.tasks[1];
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
 * @brief: commit values touched during events back to main memory
 */
void ev_commit_ph2() {
  // Copy all commit list entries
  while(ev_table.active_bins > 0)  {
    uint16_t bin = ev_table.active_bins - 1;
    uint16_t slot;
    // Walk through each slot in each bin w/ at least one value slotted in
    while(ev_table.bucket_len[bin] > 0) {
      slot = ev_table.bucket_len[bin] - 1;
      // Copy from dst in ev buf to "home" for that variable
      memcpy( ev_table.src[bin][slot],
              ev_table.dst[bin][slot],
              ev_table.size[bin][slot]
            );
      // Decrement number of items in bin
      ev_table.bucket_len[bin]--;
    }
    // Decrement number of bins left to check
    ev_table.active_bins--;
  }
  #ifdef LIBCOATIGCC_BUFFER_ALL
  for(int i = 0; i < NUM_BINS; i++) {
    tsk_table.bucket_len[i] = 0;
  }
  tsk_buf_level = 0;
  ((ev_state *)(curctx->extra_ev_state))->ev_need_commit = 0;
  #endif
}




