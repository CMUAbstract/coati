#ifndef _TX_H
#define _TX_H

#include <stddef.h>
#include <stdint.h>

#include <libmsp/mem.h>

extern unsigned access_len;

typedef struct _tx_state {
#ifdef LIBCOATIGCC_BUFFER_ALL
#endif
    uint8_t in_tx;
#ifdef LIBCOATIGCC_BUFFER_ALL
    uint8_t tx_need_commit;
#endif // BUFFER_ALL
} tx_state;

// Used INSIDE a task before any application code (aka not power manipulation
// code from libcapybara etc) to indicate the start of a transaction
#define TX_BEGIN \
    tx_begin();

#ifdef LIBCOATIGCC_BUFFER_ALL
// Normal transition_to macro given a different name for programming sanity
#define TX_TRANSITION_TO(task) \
    SET_TSK_IN_TX_TRANS \
    curctx->commit_state = TSK_IN_TX_PH1; \
    transition_to(TASK_REF(task))
#else
#define TX_TRANSITION_TO(task) \
    SET_TSK_IN_TX_TRANS \
    curctx->commit_state = TSK_PH1; \
    transition_to(TASK_REF(task))
#endif // BUFFER_ALL

// transition macro for end of a transaction so we don't have TX_END's hanging
// around
#define TX_END_TRANSITION_TO(task) \
    SET_TX_TRANS \
    TX_TIMER_STOP \
    /*printf("Tx end\r\n");*/ \
    curctx->commit_state = TX_PH1; \
    transition_to(TASK_REF(task))

// Extra defs for instrumentation
// These disable the transition timer so we can get accurate delay numbers
#ifdef LIBCOATIGCC_TEST_TIMING
#define NI_TX_END_TRANSITION_TO(task) \
    SET_TX_TRANS \
    TX_TIMER_STOP \
    curctx->commit_state = TX_PH1; \
    instrument = 0; \
    transition_to(TASK_REF(task))

#ifdef LIBCOATIGCC_BUFFER_ALL
#define NI_TX_TRANSITION_TO(task) \
      curctx->commit_state = TSK_IN_TX_PH1; \
      instrument = 0; \
      transition_to(TASK_REF(task))
#else
#define NI_TX_TRANSITION_TO(task) \
      curctx->commit_state = TSK_PH1; \
      instrument = 0; \
      transition_to(TASK_REF(task))
#endif // BUFFER_ALL

#else
#define NI_TX_END_TRANSITION_TO(task) \
    SET_TX_TRANS \
    TX_TIMER_STOP \
    curctx->commit_state = TX_PH1; \
    transition_to(TASK_REF(task))

#ifdef LIBCOATIGCC_BUFFER_ALL
#define NI_TX_TRANSITION_TO(task) \
      curctx->commit_state = TSK_IN_TX_PH1; \
      transition_to(TASK_REF(task))
#else
#define NI_TX_TRANSITION_TO(task) \
      curctx->commit_state = TSK_PH1; \
      transition_to(TASK_REF(task))
#endif // BUFFER_ALL

#endif // TEST_TIMING

#define TX_ST_SYM_NAME(name) _tx_state_ ## name

#define TX_ST_REF(name) \
        &TX_ST_SYM_NAME(name)

#define TX_READ(x,type) \
    *((type *)read(&(x),sizeof(type),TX))


#define TX_WRITE(x, val,type,is_ptr) \
    { type _temp_loc = val;\
      write(&(x),sizeof(type),TX,&_temp_loc);\
    }\

#ifdef LIBCOATIGCC_TEST_COUNT
#define NI_TX_WRITE(x,val,type,is_ptr) \
    { instrument = 0; \
      type _temp_loc = val;\
      write(&(x),sizeof(type),TX,&_temp_loc);\
    }

#define NI_TX_READ(x,type) \
      *((type *)read(&(x),sizeof(type),TX_NI))
#else
#define NI_TX_WRITE(x,val,type,is_ptr) \
    { type _temp_loc = val;\
      write(&(x),sizeof(type),TX,&_temp_loc);\
    }

#define NI_TX_READ(x,type) \
    *((type *)read(&(x),sizeof(type),TX))

#endif

extern tx_state state_1;
extern tx_state state_0;
void tx_begin();


#ifdef LIBCOATIGCC_BUFFER_ALL

extern __nv table_t tx_table;
extern __nv uint8_t tx_buf[];
extern __nv uint16_t tx_buf_level;

#ifdef LIBCOATIGCC_SER_TX_AFTER
extern src_table tx_read_table;
#else
extern src_table tx_write_table;
#endif // SER_TX_AFTER

void tsk_in_tx_commit_ph2();
void tx_commit_ph1_5();
void tx_commit_ph2();
#endif // BUFFER_ALL


#endif //_TX_H
