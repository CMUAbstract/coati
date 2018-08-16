#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <libmsp/mem.h>
#include <msp430.h>

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
  //printf("Storing %x\r\n",((task_t *)deferred_task)->func);
  event_queue.tasks[((ev_state *)curctx->extra_ev_state)->count + 1] = deferred_task;
  // We can only perform this unprotected increment of count because no more
  // writes can possibly occur.
  //printf("Got %x\r\n",
  //     event_queue.tasks[((ev_state *)curctx->extra_ev_state)->count + 1]);
 ((ev_state *)curctx->extra_ev_state)->count++;
  TRANS_TIMER_STOP;
  return 0;
}

/*
 * @brief checks if there is still room in the event queue for more entries.
 */
uint8_t top_half_start() {
  //printf("count = %u\r\n",((ev_state *)curctx->extra_ev_state)->count);
  if(((ev_state *)curctx->extra_ev_state)->count + 1 >= NUM_WQ_ENTRIES) {
    //printf("Too many!\r\n");
    return 1;
  }
  return 0;
}

__nv task_t *__sleep_return_task = NULL;

/*
 * @brief Library task for sleeping so that we force the programmer to make this
 * a separate task
 * TODO this function could differ depending on the interrupt and the board
 * version
 */
//TASK(0xFF, __task_sleep)
/*void __task_sleep() {
  //printf("Sleeping!\r\n");
  //__delay_cycles(4000);
  // go to sleep
  __bis_SR_register(LPM3_bits + GIE);
  //printf("Awake!\r\n");
  // Transition to the task pointer the programmer set on entering sleep
  if(__sleep_return_task == NULL){
    printf("Error! sleep_return not defined\r\n");
  }
  transition_to(__sleep_return_task);
}
*/
#endif // BUFFER_ALL
