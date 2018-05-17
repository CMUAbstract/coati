#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <libmsp/mem.h>

#include "top_half.h"
#include "coati.h"
#include "event.h"
#include "types.h"

#ifndef LIBCOATIGCC_BUFFER_ALL
// Queue of event updates stored in a non-volatile buffer
__nv event_queue_t event_queue;


/*
 * @brief function to handle top half clean up for interrupts. This goes in the
 * ISR at the very end of the routine. That increment to "event_queue.count"
 * needs to be the last darn thing that happens before RETI
 */
uint8_t top_half_return (void *deferred_task) {
  TIMER_START
  //printf("Storing %x\r\n",((task_t *)deferred_task)->func);
  event_queue.tasks[((ev_state *)curctx->extra_ev_state)->count + 1] = deferred_task;
  // We can only perform this unprotected increment of count because no more
  // writes can possibly occur.
  //printf("Got %x\r\n",
  //     event_queue.tasks[((ev_state *)curctx->extra_ev_state)->count + 1]);
 ((ev_state *)curctx->extra_ev_state)->count++;
 TIMER_PAUSE
  return 0;
}

/*
 * @brief checks if there is still room in the event queue for more entries.
 */
uint8_t top_half_start() {
  TIMER_START
  //printf("count = %u\r\n",((ev_state *)curctx->extra_ev_state)->count);
  if(((ev_state *)curctx->extra_ev_state)->count + 1 > NUM_WQ_ENTRIES) {
    //printf("Too many!\r\n");
    TIMER_PAUSE
    return 1;
  }
  TIMER_PAUSE
  return 0;
}

#endif // BUFFER_ALL
