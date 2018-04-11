#ifndef _TOP_HALF_H
#define _TOP_HALF_H
#include "event.h"
#include "coati.h"

#ifdef LIBCOATIGCC_BUFFER_ALL

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

#define TH_WRITE(var, val) \
  _TH_PRIV_ ## var[((ev_state *)curctx->extra_ev_state)->count + 1] = val

#define TH_WRITE_ARR(var,val,index) \
  _TH_PRIV_ ## var[((ev_state *)curctx->extra_ev_state)->count +1][index] = val

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

#endif // BUFFER_ALL

#endif /// _TOP_HALF_H
