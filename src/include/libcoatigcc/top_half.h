#ifndef _TOP_HALF_H
#define _TOP_HALF_H
#include "event.h"
#include "coati.h"
#include "tx.h"

#ifndef LIBCOATIGCC_BUFFER_ALL

#ifndef LIBCOATIGCC_NUM_WQ_ENTRIES
  #pragma message "NUM_WQ_ENTRIES UNDEF"
  #define NUM_WQ_ENTRIES 16
#else
  #pragma message "NUM_WQ_ENTRIES DEF"
  #define NUM_WQ_ENTRIES LIBCOATIGCC_NUM_WQ_ENTRIES
#endif

typedef struct _event_queue_t {
  void * tasks[NUM_WQ_ENTRIES];
} event_queue_t;

extern event_queue_t event_queue;

uint8_t top_half_return(void *deferred_task);
uint8_t top_half_start(void);
void __task_sleep(void);

extern task_t *__sleep_return_task;

#define TH_WRITE(var, val) \
  _TH_PRIV_ ## var[((ev_state *)curctx->extra_ev_state)->count + 1] = val

#define TH_WRITE_ARR(var,val,index) \
  _TH_PRIV_ ## var[((ev_state *)curctx->extra_ev_state)->count +1][index] = val

// Note: this next primitive returns the address of the starting element of the
// array- use with care!! Only write to this memory o/w you may end up with
// inconsistent values.
#define TH_ARR_ADDR(var) \
  &(_TH_PRIV_ ## var[((ev_state *)curctx->extra_ev_state)->count +1][0])

#define TH_VAR(var, type) \
  TH_VAR_INNER(var, type, NUM_WQ_ENTRIES)

#define TH_VAR_INNER(var, type, num) \
  __nv type _TH_PRIV_ ## var[num]

#define TH_ARRAY(var, type, count) \
  TH_ARRAY_INNER(var, type, count, NUM_WQ_ENTRIES)

#define TH_ARRAY_INNER(var, type, count, num) \
  __nv type _TH_PRIV_ ##var[num][count]

#define BH_READ(var) \
  _TH_PRIV_ ## var[((ev_state *)curctx->extra_ev_state)->committed + 1]

#define BH_READ_ARR(var, index) \
  _TH_PRIV_ ## var[((ev_state *)curctx->extra_ev_state)->committed +1][index]

/*
 * @brief macro wrapper for returning from the top half of an event
 */
#define TOP_HALF_RETURN(task) \
  top_half_return(task)

/*
 * @brief a quick release exit if we're out of space in the buffer
 */
#define TOP_HALF_START() \
  if(top_half_start()){\
    return;\
  }

#define TOP_HALF_CHECK_START() \
  top_half_start()
#endif // BUFFER_ALL

#define SLEEP_THEN_GOTO(task) \
  { __sleep_return_task = TASK_REF(task); \
    TRANSITION_TO(__task_sleep); \
  }

#define TX_SLEEP_THEN_GOTO(task) \
  { __sleep_return_task = TASK_REF(task); \
    TX_END_TRANSITION_TO(__task_sleep); \
  }

#define SLEEP void __task_sleep ()

#define SLEEP_RETURN \
  curctx->commit_state = TSK_PH1; \
  transition_to(__sleep_return_task)

#define SLEEP_TASK(idx) \
    void __task_sleep(); \
    __nv task_t TASK_SYM_NAME(__task_sleep) = {__task_sleep, idx, "__task_sleep" }; \

#endif /// _TOP_HALF_H
