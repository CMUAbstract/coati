#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#ifndef LIBCOATIGCC_ENABLE_DIAGNOSTICS
#define LCG_PRINTF(...)
#else
#define LCG_PRINTF printf
#endif

#ifndef LIBCOATIGCC_CONF_REPORT
#define LCG_CONF_REP(...)
#else
#define LCG_CONF_REP printf
#endif

//#define LCG_CONF_ALL printf

#include "coati.h"
#include "tx.h"
#include "filter.h"
#include "event.h"

__nv uint8_t tx_buf[BUF_SIZE];

__nv void * tx_src[NUM_DIRTY_ENTRIES];
__nv void * tx_dst[NUM_DIRTY_ENTRIES];
__nv size_t tx_size[NUM_DIRTY_ENTRIES];

#ifdef LIBCOATIGCC_BUFFER_ALL
__nv void * tx_read_list[NUM_DIRTY_ENTRIES];
__nv void * tx_write_list[NUM_DIRTY_ENTRIES];

// volatile number of reads the transaction has performed
volatile uint16_t num_txread = 0;
// volatile number of reads the transaction has performed
volatile uint16_t num_txwrite = 0;
#endif // BUFFER_ALL

// volatile flat that indicates need to commit transaction
volatile uint8_t need_tx_commit = 0;

// Pointers to state that'll get used by transactions library
__nv tx_state state_1 = {0};
__nv tx_state state_0 = {
    #ifdef LIBCOATIGCC_BUFFER_ALL
    .num_dtxv = 0,
    .num_read = 0,
    .num_write = 0,
    #endif
    .in_tx = 0,
    #ifdef LIBCOATIGCC_BUFFER_ALL
    .tx_need_commit = 0,
    .serialize_after = 0
    #endif
};



/*
 * @brief initializes state for a new tx
 * @comments Needs to take effect AFTER task prologue
 */
void tx_begin() {
    if(((tx_state *)(curctx->extra_state))->in_tx == 0) {
    #ifdef LIBCOATIGCC_BUFFER_ALL
      LCG_PRINTF("Zeroing num_dtxv!!!\r\n");
      ((tx_state *)(curctx->extra_state))->num_dtxv = 0;
    #endif
      ((tx_state *)(curctx->extra_state))->in_tx = 1;
    }
    #ifdef LIBCOATIGCC_BUFFER_ALL
    cur_tx_start = curctx->task;
    num_txread = 0;
    num_txwrite = 0;
    need_tx_commit = 0;
    ((tx_state *)(curctx->extra_state))->tx_need_commit = 0;
    #endif
    LCG_PRINTF("In tx begin!!\r\n");
}

// We only call any of these functions if full buffering is in effect
#ifdef LIBCOATIGCC_BUFFER_ALL
/*
 * @brief sets the serialize after bit
 * @comments This makes the transaction will serialize after any concurrent
 * events. This bit also has to be cleared on transaction commit so future
 * transactions that _don't_ set this bit will be ok. We're adding the update in
 * this faction so it's compatible with all the code that's been written
 * already.
 */
void set_serialize_after() {
    ((tx_state *)(curctx->extra_state))->serialize_after = 1;
}
/*
 * @brief returns the index into the tx buffers of where the data is located
 */
int16_t  tx_find(const void * addr) {
  uint16_t num_vars = ((tx_state *)curctx->extra_state)->num_dtxv;
  //LCG_PRINTF("num_vars = %x\r\n",num_vars);
  LCG_PRINTF("num_vars = %x\r\n",num_vars);
    if(num_vars) {
      for(int i = 0; i < num_vars; i++) {
        if(addr == tx_src[i]) {
          LCG_PRINTF("Found addr: %x\r\n",addr);
          return i;
        }
        else {
          LCG_PRINTF("%x != %x\r\n",addr,tx_src[i]);
        }
      }
    }
    return -1;
}

/*
 * @brief returns the pointer into the tx buf where the data is located
 */
void *  tx_get_dst(void * addr) {
  uint16_t num_vars = ((tx_state *)curctx->extra_state)->num_dtxv;
  if(num_vars) {
    for(int i = 0; i < num_vars; i++) {
      if(addr == tx_src[i])
        return tx_dst[i];
    }
  }
    return NULL;
}

/*
 * @brief function to do second phase of commit from task inside tx to tx buffer
 * from tsk buffer
 * @notes: based on persistent var: num_dtv
 */
void tsk_in_tx_commit_ph2() {
  uint16_t num_tx_vars =((tx_state *)(curctx->extra_state))->num_dtxv;
  uint16_t i = 0;
  //LCG_PRINTF("tx inner commit, num_dtv = %x\r\n",num_dtv);
  LCG_PRINTF("tx inner commit, num_dtv = %x\r\n",num_dtv);
  // Cycle through all the variables to commit
  while(num_dtv > 0) {
    void *dst = tx_get_dst(tsk_src[num_dtv - 1]);
    if(dst != NULL) {
        //LCG_PRINTF("Copying from %x to %x, %x bytes \r\n",
        LCG_PRINTF("Copying %x from %x to %x, %x bytes \r\n",
                    tsk_src[num_dtv -1],
                    ((uint8_t *)tsk_dst[num_dtv - 1]),
                    dst,
                    tsk_size[num_dtv-1]);
        memcpy( dst,
                tsk_dst[num_dtv - 1],
                tsk_size[num_dtv -1]
              );
    }
    else{
      LCG_PRINTF("Not found! allocing!\r\n");
      void *dst_alloc = tx_buf_alloc(tsk_src[num_dtv -1],
                                     tsk_size[num_dtv -1]);
      if(dst_alloc == NULL) {
        printf("Error allocating to tx buff\r\n");
        while(1);
      }
        //LCG_PRINTF("Copying from %x to %x, %x bytes \r\n",
        LCG_PRINTF("Copying %u %x from %x to %x, %x bytes \r\n",
                    *((uint16_t *)tsk_dst[num_dtv - 1]),
                    tsk_src[num_dtv -1],
                    ((uint16_t *)tsk_dst[num_dtv - 1]),
                    dst_alloc,
                    tsk_size[num_dtv-1]);
        LCG_PRINTF("New num dtxv = %x\r\n",
                ((tx_state *)curctx->extra_state)->num_dtxv);
      memcpy( dst_alloc,
              tsk_dst[num_dtv - 1],
              tsk_size[num_dtv - 1]
            );
    }
    num_dtv--;
  }
  return;
}

/*
 * @brief allocs space in tx buffer and returns a pointer to the spot, returns
 * NULL if buf is out of space
 */
void * tx_buf_alloc(void * addr, size_t size) {
  uint16_t new_ptr;
  uint16_t index = ((tx_state *)curctx->extra_state)->num_dtxv;
  if(index) {
    new_ptr = (uint16_t) tx_dst[index - 1] +
    tx_size[index - 1];
  }
  else {
    new_ptr = (uint16_t) tx_buf;
  }
  // Fix alignment struggles
  if(size == 2) {
    while(new_ptr & 0x1)
      new_ptr++;
  }
  if(size == 4) {
    while(new_ptr & 0x11)
      new_ptr++;
  }
  if(new_ptr + size > (unsigned) (tx_buf + BUF_SIZE)) {
    return NULL;
  }
  else {
    tx_src[index] = addr;
    tx_dst[index] = (void *) new_ptr;
    tx_size[index] = size;
    ((tx_state *)curctx->extra_state)->num_dtxv++;
  }
  return (void *) new_ptr;
}


/*  
 * @brief write back to source on transaction commit
 * @notes uhh, this sucker runs regardless... this could be bad
 */
void tx_commit_ph2() {
  LCG_PRINTF("Running tx_commit_ph2\r\n");
  while(((tx_state *)(curctx->extra_state))->num_dtxv > 0) {
    uint16_t num_dtxv =((tx_state *)(curctx->extra_state))->num_dtxv;
    LCG_PRINTF("num_dtxv =%u\r\n",num_dtxv);
    LCG_PRINTF("Copying from %x to %x \r\n",
              tx_dst[num_dtxv - 1],
              tx_src[num_dtxv-1]);
    memcpy( tx_src[num_dtxv -1],
            tx_dst[num_dtxv - 1],
            tx_size[num_dtxv - 1]
    );
    ((tx_state *)(curctx->extra_state))->num_dtxv--;
  }
  return;
}

/*
 * @brief function to check for necessary comparisons and stuff to figure out
 * what we can commit if there's an ongoing transaction (or not)
 * @notes we assume this is run after any commit of the last tsk in a
 * transaction, and the goal is to change the curctx->commit_state to something
 * else
 * TODO need to finish it, make sure that the state transfer for ev_need_commit
 * is working correctly.
 */
void tx_commit_ph1_5() {
  int conflict = 0;
  // Default setting
  if(((tx_state *)(curctx->extra_state))->serialize_after == 0) {
    if(((ev_state *)curctx->extra_ev_state)->ev_need_commit) {
      uint16_t ev_len = 0, tx_len = 0;
      ev_len = ((ev_state *)curctx->extra_ev_state)->num_read;
      tx_len = ((tx_state *)curctx->extra_state)->num_write;
      conflict = compare_lists(ev_read_list, tx_write_list, ev_len, tx_len);
      if(conflict == 1) {
        // Clear need_commit flag so we don't get in here again
        ((ev_state *)curctx->extra_ev_state)->ev_need_commit = 0;
        curctx->commit_state = TX_ONLY;
        LCG_CONF_REP("Conflict! Only committing tx\r\n");
        _numEvents_uncommitted++;

      }
      else {
        LCG_CONF_REP("No conflict! committing tx then ev\r\n");
        curctx->commit_state = TX_EV_COMMIT;
      }
    }
    else {
      curctx->commit_state = TX_ONLY;
    }
  }
  else {
    if(((ev_state *)curctx->extra_ev_state)->ev_need_commit) {
      uint16_t ev_len = 0, tx_len = 0;
      ev_len = ((ev_state *)curctx->extra_ev_state)->num_write;
      tx_len = ((tx_state *)curctx->extra_state)->num_read;
      conflict = compare_lists(ev_write_list, tx_read_list, ev_len, tx_len);
      if(conflict == 1) {
        LCG_CONF_REP("Conflict! Only committing ev\r\n");
        // Clear need_commit flag so we don't get in here again
        ((ev_state *)curctx->extra_ev_state)->ev_need_commit = 0;
        curctx->commit_state = EV_ONLY;
      }
      else {
        LCG_CONF_REP("No conflict! committing ev then tx\r\n");
        curctx->commit_state = EV_TX_COMMIT;
      }
    }
    else {
      curctx->commit_state = EV_ONLY;
    }
  }
  return;
}


void *tx_memcpy(void *dest, void *src, uint16_t num) {
  if ((uintptr_t) dest % sizeof(unsigned) == 0 &&
      (uintptr_t) dest % sizeof(unsigned) == 0) {
    unsigned *d = dest;
    unsigned tmp;
    const unsigned *s = src;
    for (unsigned i = 0; i < num/sizeof(unsigned); i++) {
      tmp = *((unsigned *) read(&s[i], sizeof(unsigned), TX));
      write(&d[i], sizeof(unsigned), TX, tmp);
    }
  } else {
    char *d = dest;
    const char *s = src;
    char tmp;
    for (unsigned i = 0; i < num; i++) {
      tmp = *((char *) read(&s[i], sizeof(char), TX));
      write(&d[i], sizeof(char), TX, tmp);
    }
  }
  return dest;
}
#endif // BUFFER_ALL

