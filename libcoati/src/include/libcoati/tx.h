#ifndef _TX_H
#define _TX_H

#include <stddef.h>
#include <stdint.h>

#include <libmsp/mem.h>

typedef struct _tx_state {
    uint16_t num_dtxv;
    uint8_t in_tx;
    uint8_t tx_need_commit;
} tx_state;

//#define TX_BEGIN \
//    tx_begin();
//
//#define TX_END \
//    tx_end();

#define TX_BEGIN() __attribute((annotate("tx_begin")))
#define TX_END()__attribute((annotate("tx_end")))

#define TX_READ(x,type) \
    *((type *)read(&(x),sizeof(type),TX))

#define TX_WRITE(x,val,type,is_ptr) \
    { if(is_ptr){ \
          write(&(x),sizeof(type),TX,val);\
      }\
      else { \
          type _temp_loc = val;\
          write(&(x),sizeof(type),TX,&_temp_loc);\
      } \
    }

extern tx_state state_1;
extern tx_state state_0;

extern volatile uint16_t num_txbe;
extern volatile uint8_t need_tx_commit;


extern __nv uint8_t tx_dirty_buf[BUF_SIZE];
extern __nv void * tx_dirty_src[NUM_DIRTY_ENTRIES];
extern __nv void * tx_dirty_dst[NUM_DIRTY_ENTRIES];
extern __nv size_t tx_dirty_size[NUM_DIRTY_ENTRIES];


void tx_begin();
void tx_end();
int16_t  tfind(const void * addr);
void *  t_get_dst(void * addr);
void * tread(void * addr);
void tcommit_ph1();
void * tx_dirty_buf_alloc(void * addr, size_t size);
void tx_commit();
void *tx_memcpy(void *dest, void *src, uint16_t num);

#endif //_TX_H
