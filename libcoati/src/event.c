#include <stdio.h>
#include "event.h"
#include <stddef.h>
#include <libmsp/mem.h>
#include "coati.h"
#include "filter.h"
#include "tx.h"
#include "event.h"


__nv context_t * thread_ctx;
__nv task_t * cur_tx_start;

/**
 * @brief a kind of complicated variable declaration, this should only happen
 * when the thing is initially programmed though, so no worries
 * @info this sets the filter_size based on what's in bloom_filter.h, so this
 * will likely need to change to improve flexibility/portability
 */
static unsigned djb(unsigned data);

__nv bloom_hash hash = {&djb, NULL};
__nv bloom_filter filters[NUM_PRIO_LEVELS]= {
    {FILTER_SIZE, {0}, &hash},
    {FILTER_SIZE, {0}, &hash}
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
    // Set the last bit so we can use that as an indicator we're in an event
    printf("In event handler!");
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
  printf("thread tx status = %i \r\n",((tx_state *)thread_ctx->extra_state)->in_tx);
  if(((tx_state *)(thread_ctx->extra_state))->in_tx) {
      test = compare_filters(filters + THREAD, filters + EV);
      if(test) {
        thread_ctx->task = cur_tx_start;
        printf("Conflict Detected!! changing to %x  from %x\r\n",
            (unsigned) thread_ctx->task->func,
            (unsigned) curctx->task->func);
      }
      else{
        printf("No conflicts!!\r\n");
      }
  }
  curctx = thread_ctx;
  printf("In event return next func =%x\r\n",
      (unsigned) curctx->task->func);

  // Need to re-enable events here
  _enable_events();
  // jump to curctx
  __asm__ volatile ( // volatile because output operands unused by C
        "br %[nt]\n"
        : /* no outputs */
        : [nt] "r" (curctx->task->func)
        );

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
