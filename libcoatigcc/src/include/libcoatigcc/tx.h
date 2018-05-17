#ifndef _TX_H
#define _TX_H

#include <stddef.h>
#include <stdint.h>

#include <libmsp/mem.h>

typedef struct _tx_state {
#ifdef LIBCOATIGCC_BUFFER_ALL
    uint16_t num_dtxv;
    uint16_t num_read;
    uint16_t num_write;
#endif
    uint8_t in_tx;
#ifdef LIBCOATIGCC_BUFFER_ALL
    uint8_t tx_need_commit;
    uint8_t serialize_after;
#endif // BUFFER_ALL
} tx_state;

// Used INSIDE a task before any application code (aka not power manipulation
// code from libcapybara etc) to indicate the start of a transaction
#define TX_BEGIN \
    tx_begin();


#ifdef LIBCOATIGCC_BUFFER_ALL
// Normal transition_to macro given a different name for programming sanity
#define TX_TRANSITION_TO(task) \
    TIMER_START \
    curctx->commit_state = TSK_IN_TX_PH1; \
    transition_to(TASK_REF(task))
#else
#define TX_TRANSITION_TO(task) \
    TIMER_START \
    curctx->commit_state = TSK_PH1; \
    transition_to(TASK_REF(task))
#endif // BUFFER_ALL

// transition macro for end of a transaction so we don't have TX_END's hanging
// around
#define TX_END_TRANSITION_TO(task) \
    TIMER_START \
    curctx->commit_state = TX_PH1; \
    transition_to(TASK_REF(task))



#ifdef LIBCOATIGCC_BUFFER_ALL
// Place immediately after TX_BEGIN in the first task of a transaction where the
// TX will serialize after any concurrent events. Default behavior will
// serialize the TX before the event and throw out the effects of the event if
// it conflicts with the transaction
#define SERIALIZE_AFTER \
    set_serialize_after();
#endif // BUFFER_ALL

#define TX_ST_SYM_NAME(name) _tx_state_ ## name

#define TX_ST_REF(name) \
        &TX_ST_SYM_NAME(name)

#define TX_READ(x,type) \
    *((type *)read(&(x),sizeof(type),TX))


#if 0
#define TX_WRITE(x,val,type,is_ptr) \
    TIMER1_START \
    { if(is_ptr){ \
          write(&(x),sizeof(type),TX,val);\
      }\
      else { \
          type _temp_loc = val;\
          write(&(x),sizeof(type),TX,_temp_loc);\
      } \
    } \
    TIMER1_PAUSE

#else
#define TX_WRITE(x, val,type,is_ptr) \
    TIMER1_START \
    { type _temp_loc = val;\
      write(&(x),sizeof(type),TX,_temp_loc);\
    }\
    TIMER1_PAUSE
#endif

extern tx_state state_1;
extern tx_state state_0;
extern volatile uint8_t need_tx_commit;
void tx_begin();


#ifdef LIBCOATIGCC_BUFFER_ALL
extern volatile uint16_t num_txread;
extern volatile uint16_t num_txwrite;


extern __nv uint8_t tx_buf[];
extern __nv void * tx_src[];
extern __nv void * tx_dst[];
extern __nv size_t tx_size[];
extern void * tx_read_list[];
extern void * tx_write_list[];

int16_t  tx_find(const void * addr);
void *  tx_get_dst(void * addr);
void * tx_buf_alloc(void * addr, size_t size);
void tsk_in_tx_commit_ph2();
void tx_commit_ph1_5();
void tx_commit_ph2();
void *tx_memcpy(void *dest, void *src, uint16_t num);
#endif // BUFFER_ALL


#endif //_TX_H
