#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <libmsp/mem.h>
#include <msp430.h>
#include "coati.h"
#include "filter.h"
#include "tx.h"
#include "event.h"

#ifndef LIBCOATIGCC_ENABLE_DIAGNOSTICS
#define LCG_PRINTF(...)
#else
#define LCG_PRINTF printf
#endif


__nv uint8_t ev_dirty_buf[BUF_SIZE];
__nv void * ev_dirty_src[NUM_DIRTY_ENTRIES];
__nv void * ev_dirty_dst[NUM_DIRTY_ENTRIES];
__nv size_t ev_dirty_size[NUM_DIRTY_ENTRIES];

__nv context_t * thread_ctx;
__nv task_t * cur_tx_start;

/**
 * @brief a kind of complicated variable declaration, this should only happen
 * when the thing is initially programmed though, so no worries
 * @info this sets the filter_size based on what's in bloom_filter.h, so this
 * will likely need to change to improve flexibility/portability
 */
static unsigned djb(unsigned data);
volatile uint16_t num_evbe = 0;
volatile uint8_t need_ev_commit = 0;

__nv bloom_hash hash = {&djb, NULL};
__nv bloom_filter write_filters[NUM_PRIO_LEVELS]= {
    {FILTER_SIZE, {0}, &hash},
    {FILTER_SIZE, {0}, &hash}
};

__nv bloom_filter read_filters[NUM_PRIO_LEVELS]= {
    {FILTER_SIZE, {0}, &hash},
    {FILTER_SIZE, {0}, &hash}
};

__nv ev_state state_ev_1 = {0};
__nv ev_state state_ev_0 = {
    .num_devv = 0,
    .in_ev = 0,
    .ev_need_commit = 0
};

/**
 * @brief the BYO hash function that we need for running the bloom filter
 */
unsigned djb(unsigned data){
    uint16_t hash = 5381;
    hash = ((hash << 5) + hash) + (data & 0xFF00);
    hash = ((hash << 5) + hash) + (data & 0x00FF);
    return hash & 0xFFFF;
}

/*
 * @brief function to act as interrupt handler when an event interrupt is
 * triggered
 */
void event_handler(context_t *new_event_ctx) {
    // Disable all event interrupts
    _disable_events();

    // Clear event buffer counters but don't turn on curctx->in_ev! If we died
    // after seeting that, events would continously be disabled and they'd never
    // get turned back on
    //((ev_state *)(curctx->extra_ev_state))->in_ev = 1;
    num_evbe = 0;

    // Pass across the number of variables in the ev buffer from the threadctx
    // to the active eventctx so that the next event can access the old
    // variables (it's fine to leave this unprotected from a power standpoint,
    // it'll just get rewritten if we power down
    ((ev_state *)new_event_ctx->extra_ev_state)->num_devv =
        ((ev_state *)curctx->extra_ev_state)->num_devv;
    LCG_PRINTF("In event handler! coming from %x \r\n",curctx->task->func);

    // Point threads' context at current context
    thread_ctx=curctx;
    LCG_PRINTF("Double check in:  %x \r\n",thread_ctx->task->func);

    // Set curctx with prepackaged event_ctx
    curctx = new_event_ctx;
    __asm__ volatile ( // volatile because output operands unused by C
          "mov #0x2400, r1\n"
          "br %[ntask]\n"
          :
          : [ntask] "r" (curctx->task->func)
          );
}


/*
 *@brief function to return from event and correctly restore state
 */
void event_return() {
  uint8_t test = 0;
  // Write down that we need a commit
  ((ev_state *)(curctx->extra_ev_state))->ev_need_commit = 1;
  LCG_PRINTF("Double check out:  %x \r\n",thread_ctx->task->func);
  // Figure out if there's an ongoing transaction
  if(((tx_state *)(thread_ctx->extra_state))->in_tx == 0) {
      LCG_PRINTF("Starting ev_commit\r\n");
      ev_commit();
      ((ev_state *)(thread_ctx->extra_ev_state))->ev_need_commit= 0;
      ((ev_state *)(thread_ctx->extra_ev_state))->num_devv= 0;
      need_ev_commit = 0;
  }
  num_evbe = 0;
  // This really isn't necessary. It shouldn't come up since curctx = eventctx
  ((ev_state *)(curctx->extra_ev_state))->ev_need_commit= 0;
  LCG_PRINTF("Double check out2:  %x \r\n",thread_ctx->task->func);

  // Complicated, gross swap to follow so we correctly handle the update to
  // num_devv
  tx_state *new_tx_state;
  ev_state *new_ev_state;
  //Figure out which buffer to point to
  new_tx_state = (thread_ctx->extra_state == &state_0 ? &state_1 : &state_0);
  new_ev_state = (thread_ctx->extra_ev_state == &state_ev_0 ? &state_ev_1 :
                  &state_ev_0);
  //Copy over to the new buffers 
  *new_tx_state =*((tx_state *)(thread_ctx->extra_state));
  *new_ev_state =*((ev_state *)(thread_ctx->extra_ev_state));


  // Update the number of dirty variables, otherwise leave the copies the same
  new_ev_state->num_devv = ((ev_state *)curctx->extra_ev_state)->num_devv +
      num_evbe;

  LCG_PRINTF("Checking thread stuff\r\n");

  if(thread_ctx == context_ptr0 || thread_ctx == context_ptr1) {
    //Do swap
    context_t *next_ctx;
    next_ctx = (thread_ctx == context_ptr0 ? context_ptr1 : context_ptr0);
    LCG_PRINTF("Inside checking thread task %x \r\n",thread_ctx->task);
    next_ctx->extra_state = new_tx_state;
    next_ctx->extra_ev_state = new_ev_state;
    next_ctx->task = thread_ctx->task;
    LCG_PRINTF("Inside checking next task %x \r\n",next_ctx->task);
    curctx = next_ctx;
  }
  else {
    //Error! thread_ctx should always be one of the contexts established in
    //coati.c
    LCG_PRINTF("Error! got knocked out of correct ptrs!");
    while(1);
  }

  _enable_events();
  // Need to re-enable events her
  LCG_PRINTF("Enabled events, headed to %x \r\n", curctx->task->func);
  // jump to curctx
  __asm__ volatile ( // volatile because output operands unused by C
        "br %[nt]\n"
        : /* no outputs */
        : [nt] "r" (curctx->task->func)
        );
}

/*
 * @brief returns the index into the tx buffers of where the data is located
 */
int16_t  evfind(const void * addr) {
    int16_t num_vars = 0;
    num_vars = ((ev_state *)curctx->extra_ev_state)->num_devv + num_evbe;
    if(num_vars) {
      for(int i = 0; i < num_vars; i++) {
          if(addr == ev_dirty_src[i])
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
          if(addr == ev_dirty_src[i])
              return ev_dirty_dst[i];
      }
    }
    return NULL;
}

/*
 * DEPRACATED
 */
void evcommit_ph1() {
    // check for new stuff to add to tx buf from tsk buf
    if(!num_tbe)
        return;

    for(int i = 0; i < num_tbe; i++) {
        // Step in and find the tx_buf and change the spot where commit_ph2 will
        // write back to
        // TODO add in some optimizations here for when we're committing the
        // transaction too. No need to write back to tx if committing after
        // active task

        // hunt for addr in tx buf and return new dst if it's in there
        void *ev_dst = ev_get_dst(task_dirty_buf_src[i]);
        if(!ev_dst) {
            LCG_PRINTF("Running event buf alloc!\r\n");
            ev_dst = tx_dirty_buf_alloc(task_dirty_buf_src[i],
                      task_dirty_buf_size[i]);
            if(!ev_dst) {
                // Error! We ran out of space in tx buf
                LCG_PRINTF("Out of event space!\r\n");
                while(1);
                return;
            }
        }
        task_commit_list_src[i] = ev_dst;
        task_commit_list_dst[i] = task_dirty_buf_dst[i];
        task_commit_list_size[i] = task_dirty_buf_size[i];

    }
    num_dtv = num_tbe;
}

/*
 * @brief: commit values touched during events back to main memory
 */
void ev_commit() {
    // Copy all tx buff entries to main memory
    while(((ev_state *)(curctx->extra_ev_state))->num_devv > 0) {
        uint16_t num_devv =((ev_state *)(curctx->extra_ev_state))->num_devv;
        LCG_PRINTF("Copying %i th time from %x to %x \r\n", num_devv-1,
        ev_dirty_src[num_devv-1],
        ev_dirty_dst[num_devv - 1]);
        memcpy( ev_dirty_src[num_devv -1],
                ev_dirty_dst[num_devv - 1],
                ev_dirty_size[num_devv - 1]
        );
        ((ev_state *)(curctx->extra_ev_state))->num_devv--;
    }
    ((ev_state *)(curctx->extra_ev_state))->ev_need_commit = 0;
}

void *event_memcpy(void *dest, void *src, uint16_t num) {
  if ((uintptr_t) dest % sizeof(unsigned) == 0 &&
      (uintptr_t) dest % sizeof(unsigned) == 0) {
    unsigned *d = dest;
    unsigned tmp;
    const unsigned *s = src;
    for (unsigned i = 0; i < num/sizeof(unsigned); i++) {
      tmp = *((unsigned *) read(&s[i], sizeof(unsigned), EVENT));
      write(&d[i], sizeof(unsigned), EVENT, tmp);
    }
  } else {
    char *d = dest;
    const char *s = src;
    char tmp;
    for (unsigned i = 0; i < num; i++) {
      tmp = *((char *) read(&s[i], sizeof(char), EVENT));
      write(&d[i], sizeof(char), EVENT, tmp);
    }
  }
  return dest;
}



/*
 * @brief allocs space in event buffer and returns a pointer to the spot,
 * returns NULL if buf is out of space
 */
void * ev_dirty_buf_alloc(void * addr, size_t size) {
    uint16_t new_ptr;
    LCG_PRINTF("In alloc! num_evbe = %i \r\n",num_evbe);
    uint16_t num_vars = 0;
    num_vars = ((ev_state *)curctx->extra_ev_state)->num_devv + num_evbe;
    if(num_vars) {
        new_ptr = (uint16_t) ev_dirty_dst[num_vars - 1] +
        ev_dirty_size[num_vars - 1];
    }
    else {
        new_ptr = (uint16_t) ev_dirty_buf;
    }
    if(new_ptr + size > (unsigned) (ev_dirty_buf + BUF_SIZE)) {
        return NULL;
    }
    else {
        num_evbe++;
        num_vars++;
        LCG_PRINTF("new src = %x new dst = %x index %i\r\n", addr, new_ptr,
        num_vars);
        //(((ev_state *)(curctx->extra_ev_state))->num_devv)++;
        ev_dirty_src[num_vars - 1] = addr;
        ev_dirty_dst[num_vars - 1] = (void *) new_ptr;
        ev_dirty_size[num_vars - 1] = size;
    }
    return (void *) new_ptr;
}

