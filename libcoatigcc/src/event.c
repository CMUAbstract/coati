#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <libmsp/mem.h>
#include <msp430.h>
#include "coati.h"
#include "filter.h"
#include "tx.h"
#include "event.h"

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
volatile uint16_t num_evbe;
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
    // Clear task buffer counters
    num_dtv = 0;
    num_tbe = 0;
    // Clear event buffer counters and set in_ev
    ((ev_state *)(curctx->extra_ev_state))->num_devv = 0;
    ((ev_state *)(curctx->extra_ev_state))->in_ev = 1;
    ((ev_state *)(curctx->extra_ev_state))->ev_need_commit = 0;
    need_ev_commit = 0;
    num_evbe = 0;

    uint16_t temp = ((uint16_t)new_event_ctx->task->func);
    temp |= 0x1;
    new_event_ctx->task->func = (void (*)(void))temp;
    // Disable all event interrupts
    _disable_events();

    // Point threads' context at current context
    thread_ctx = curctx;

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
  // Do nothing, we'll check for conflicts on tx commit
  need_ev_commit = 1;
  if(((tx_state *)(thread_ctx->extra_state))->in_tx == 0) {
  }
  printf("Returning from event!\r\n");
  curctx = thread_ctx;
  // Need to re-enable events here
  _enable_events();
  while(1);
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
    if(num_evbe) {
      for(int i = 0; i < num_evbe; i++) {
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
    if(num_evbe) {
      for(int i = 0; i < num_evbe; i++) {
          if(addr == ev_dirty_src[i])
              return ev_dirty_dst[i];
      }
    }
    return NULL;
}

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
            ev_dst = tx_dirty_buf_alloc(task_dirty_buf_src[i],
                      task_dirty_buf_size[i]);
            if(!ev_dst) {
                // Error! We ran out of space in tx buf
                printf("Out of space!\r\n");
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

void ev_commit() {
    // Copy all tx buff entries to main memory
    while(((ev_state *)(curctx->extra_ev_state))->num_devv > 0) {
        uint16_t num_devv =((ev_state *)(curctx->extra_ev_state))->num_devv;
        memcpy( ev_dirty_src[num_devv -1],
                ev_dirty_dst[num_devv - 1],
                ev_dirty_size[num_devv - 1]
        );
        ((ev_state *)(curctx->extra_ev_state))->num_devv--;
    }
    // zeroing need_tx_commit MUST come after removing in_tx condition since we
    // perform tx_commit based on the need_tx_commit flag
    ((ev_state *)(curctx->extra_ev_state))->in_ev = 0;
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
