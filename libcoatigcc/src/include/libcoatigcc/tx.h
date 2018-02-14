#ifndef _TX_H
#define _TX_H

#include <stddef.h>
#include <stdint.h>

#include <libmsp/mem.h>

typedef struct _tx_state {
    uint16_t num_dtxv;
    uint16_t num_read;
    uint16_t num_write;
    uint8_t in_tx;
    uint8_t tx_need_commit;
    uint8_t serialize_after;
} tx_state;

// Used INSIDE a task before any application code (aka not power manipulation
// code from libcapybara etc) to indicate the start of a transaction
#define TX_BEGIN \
    tx_begin();

// Used at ALL exit points of the last task in a transaction
#define TX_END \
    tx_end();

// Normal transition_to macro given a different name for programming sanity
#define TX_TRANSITION_TO(task) transition_to(TASK_REF(task))

// transition macro for end of a transaction so we don't have TX_END's hanging
// around
#define TX_END_TRANSITION_TO(task) \
    tx_end(); \
    tx_commit_ph1(); \
    transition_to(TASK_REF(task))

// Place immediately after TX_BEGIN in the first task of a transaction where the
// TX will serialize after any concurrent events. Default behavior will
// serialize the TX before the event and throw out the effects of the event if
// it conflicts with the transaction
#define SERIALIZE_AFTER \
    set_serialize_after();

#define TX_ST_SYM_NAME(name) _tx_state_ ## name

#define TX_ST_REF(name) \
        &TX_ST_SYM_NAME(name)

#define TX_READ(x,type) \
    *((type *)read(&(x),sizeof(type),TX))

#define TX_WRITE(x,val,type,is_ptr) \
    { if(is_ptr){ \
          write(&(x),sizeof(type),TX,val);\
      }\
      else { \
          type _temp_loc = val;\
          write(&(x),sizeof(type),TX,_temp_loc);\
      } \
    }

extern tx_state state_1;
extern tx_state state_0;

extern volatile uint16_t num_txbe;
extern volatile uint16_t num_txread;
extern volatile uint16_t num_txwrite;
extern volatile uint8_t need_tx_commit;


extern __nv uint8_t tx_dirty_buf[];
extern __nv void * tx_dirty_src[];
extern __nv void * tx_dirty_dst[];
extern __nv size_t tx_dirty_size[];
extern void * tx_read_list[];
extern void * tx_write_list[];

void tx_begin();
void my_tx_begin();
void tx_end();
int16_t  tx_find(const void * addr);
void *  tx_get_dst(void * addr);
void tx_commit_ph1();
void tx_inner_commit_ph2();
void * tx_dirty_buf_alloc(void * addr, size_t size);
void tx_commit();
void *tx_memcpy(void *dest, void *src, uint16_t num);

#endif //_TX_H
