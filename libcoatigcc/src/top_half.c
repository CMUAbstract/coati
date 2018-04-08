#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <libmsp/mem.h>

#include "top_half.h"
#include "coati.h"
#include "event.h"
#include "types.h"

// Queue of event updates stored in a non-volatile buffer
__nv event_queue_t event_queue = {
  .tasks = {0},
};


/*
 * @brief function to handle top half clean up for interrupts. This goes in the
 * ISR at the very end of the routine. That increment to "event_queue.count"
 * needs to be the last darn thing that happens before RETI
 */
uint8_t top_half_return (void *deferred_task) {
  printf("Storing %x\r\n",((task_t *)deferred_task)->func);
  event_queue.tasks[((ev_state *)curctx->extra_ev_state)->count + 1] = deferred_task;
  // We can only perform this unprotected increment of count because no more
  // writes can possibly occur.
 ((ev_state *)curctx->extra_ev_state)->count++; 
  return 0;
}

/*
 * @brief checks if there is still room in the event queue for more entries.
 */
uint8_t top_half_start() {
  printf("count = %u\r\n",((ev_state *)curctx->extra_ev_state)->count);
  if(((ev_state *)curctx->extra_ev_state)->count + 1 > NUM_WQ_ENTRIES) {
    printf("Too many!\r\n");
    return 1;
  }
  return 0;
}

