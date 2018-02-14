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

__nv uint8_t tx_dirty_buf[BUF_SIZE];

__nv void * tx_dirty_src[NUM_DIRTY_ENTRIES];
__nv void * tx_dirty_dst[NUM_DIRTY_ENTRIES];
__nv size_t tx_dirty_size[NUM_DIRTY_ENTRIES];

__nv void * tx_read_list[NUM_DIRTY_ENTRIES];
__nv void * tx_write_list[NUM_DIRTY_ENTRIES];

// volatile number of tx buffer entries that we want to clear on reboot
volatile uint16_t num_txbe = 0;
// volatile number of reads the transaction has performed
volatile uint16_t num_txread = 0;
// volatile number of reads the transaction has performed
volatile uint16_t num_txwrite = 0;
// volatile flat that indicates need to commit transaction
volatile uint8_t need_tx_commit = 0;

// Pointers to state that'll get used by transactions library
__nv tx_state state_1 = {0};
__nv tx_state state_0 = {
    .num_dtxv = 0,
    .num_read = 0,
    .num_write = 0,
    .in_tx = 0,
    .tx_need_commit = 0,
    .serialize_after = 0
};

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
 * @brief initializes state for a new tx
 * @comments Needs to take effect AFTER task prologue
 */
void tx_begin() {
    ((tx_state *)(curctx->extra_state))->num_dtxv = 0;
    ((tx_state *)(curctx->extra_state))->in_tx = 1;
    ((tx_state *)(curctx->extra_state))->tx_need_commit = 0;
    LCG_PRINTF("In tx begin!!\r\n");
    cur_tx_start = curctx->task;
    need_tx_commit = 0;
    num_txbe = 0;
    num_txread = 0;
    num_txwrite = 0;
}

void my_tx_begin() {
    ((tx_state *)(curctx->extra_state))->num_dtxv = 0;
    ((tx_state *)(curctx->extra_state))->in_tx = 1;
    ((tx_state *)(curctx->extra_state))->tx_need_commit = 0;
    LCG_PRINTF("In tx begin!!\r\n");
    cur_tx_start = curctx->task;
    need_tx_commit = 0;
    num_txbe = 0;
}

/*
 * @brief set flags for committing a tx
 */
void tx_end() {
    LCG_PRINTF("Setting tx_end flags!\r\n");
    need_tx_commit = 1;
}

/*
 * @brief returns the index into the tx buffers of where the data is located
 */
int16_t  tx_find(const void * addr) {
    if(((tx_state *)curctx->extra_state)->num_dtxv) {
      for(int i = 0; i < ((tx_state *)curctx->extra_state)->num_dtxv; i++) {
          if(addr == tx_dirty_src[i])
              return i;
      }
    }
    return -1;
}

/*
 * @brief returns the pointer into the tx dirty buf where the data is located
 */
void *  tx_get_dst(void * addr) {
    if(((tx_state *)curctx->extra_state)->num_dtxv) {
      for(int i = 0; i < ((tx_state *)curctx->extra_state)->num_dtxv; i++) {
          if(addr == tx_dirty_src[i])
              return tx_dirty_dst[i];
      }
    }
    return NULL;
}

/*
 * @brief function to do second phase of commit from task inside tx to tx buffer
 * from tsk buffer
 * @notes: based on persistent var: num_dtv
 */
void tx_inner_commit_ph2() {
  uint16_t num_tx_vars =((tx_state *)(curctx->extra_state))->num_dtxv; 
  uint16_t i = 0;
  printf("tx inner commit, num_dtv = %x\r\n",num_dtv);
  // Cycle through all the variables to commit
  while(num_dtv > 0) {
    void *dst = tx_get_dst(task_dirty_buf_src[num_dtv - 1]);
    if(dst != NULL) {
        printf("Copying from %x to %x, %x bytes \r\n",
                    ((uint8_t *)task_dirty_buf_dst[num_dtv - 1]),
                    dst,
                    task_dirty_buf_size[num_dtv-1]);
        memcpy( dst,
                task_dirty_buf_dst[num_dtv - 1],
                task_dirty_buf_size[num_dtv -1]
              );
    }
    else{
      printf("Not found! allocing!\r\n");
      void *dst_alloc = tx_dirty_buf_alloc(task_dirty_buf_src[num_dtv -1],
                                     task_dirty_buf_size[num_dtv - 1]);
      if(dst_alloc == NULL) {
        printf("Error allocating to tx buff\r\n");
        while(1);
      }
        printf("Copying from %x to %x, %x bytes \r\n",
                    ((uint16_t *)task_dirty_buf_dst[num_dtv - 1]),
                    dst_alloc,
                    task_dirty_buf_size[num_dtv-1]);
      memcpy( dst_alloc,
              task_dirty_buf_dst[num_dtv - 1],
              task_dirty_buf_size[num_dtv - 1]
            );
    }
    num_dtv--;
  }
  return;
}


/*
 * @brief does the first phase of updates to the actual tx state when a tx
 * commits, not just when a task inside a tx commits.
 */
void tx_commit_ph1(tx_state * new_tx_state) {
    // Kind of wrong now that we're really just doing this update in tx
    // commit... I think this first assignment needs to be removed
    //new_tx_state->num_dtxv = ((tx_state *)(curctx->extra_state))->num_dtxv +
    //                                                              num_txbe;
    // Update length of read/write lists
    new_tx_state->num_read = ((tx_state *)(curctx->extra_state))->num_read +
                                                                  num_txread;
    new_tx_state->num_write = ((tx_state *)(curctx->extra_state))->num_write +
                                                                  num_txwrite;
    // Latch number of items in tsk buffer
    num_dtv = num_tbe;
    
}

/*
 * @brief allocs space in tx buffer and returns a pointer to the spot, returns
 * NULL if buf is out of space
 */
void * tx_dirty_buf_alloc(void * addr, size_t size) {
    uint16_t new_ptr;
    uint16_t index = ((tx_state *)curctx->extra_state)->num_dtxv; 
    if(index) {
        new_ptr = (uint16_t) tx_dirty_dst[index - 1] +
        tx_dirty_size[index - 1];
    }
    else {
        new_ptr = (uint16_t) tx_dirty_buf;
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
    if(new_ptr + size > (unsigned) (tx_dirty_buf + BUF_SIZE)) {
        return NULL;
    }
    else {
        tx_dirty_src[index] = addr;
        tx_dirty_dst[index] = (void *) new_ptr;
        tx_dirty_size[index] = size;
        ((tx_state *)curctx->extra_state)->num_dtxv++; 
    }
    return (void *) new_ptr;
}

/*
 * @brief an internal function for committing if the transaction is defined as
 * serialize before (the default)
 */
static void tx_commit_txsb() {
    // Copy all tx buff entries to main memory no matter what
    printf("In tx_commit ser before!\r\n");
#ifdef LCG_CONF_ALL
    uint16_t num_dtxv_start = 0;
#endif
    while(((tx_state *)(curctx->extra_state))->num_dtxv > 0) {
        uint16_t num_dtxv =((tx_state *)(curctx->extra_state))->num_dtxv;
#ifdef LCG_CONF_ALL
        num_dtxv_start = num_dtxv;
#endif
        printf("Copying from %x to %x \r\n",
                    tx_dirty_dst[num_dtxv - 1],
                    tx_dirty_src[num_dtxv-1]);
        memcpy( tx_dirty_src[num_dtxv -1],
                tx_dirty_dst[num_dtxv - 1],
                tx_dirty_size[num_dtxv - 1]
        );
        ((tx_state *)(curctx->extra_state))->num_dtxv--;
    }
    // Now compare filters to see if we can safely merge in any ongoing events
    // but only if the events still need to be committed
    // don't worry- the last thing ev_commit does is flip the state to COMMIT_DONE
    if(((ev_state *)(curctx->extra_ev_state))->ev_need_commit == FUTURE_COMMIT) {
        int conflict = 0;
        // Only a conflict if event read a value written by the transaction
        // since in all other cases we can serialize the tx before the event
        //conflict |= compare_filters(read_filters + EV, write_filters + THREAD);
        uint16_t ev_len = ((ev_state *)curctx->extra_ev_state)->num_read;
        uint16_t tx_len = ((tx_state *)curctx->extra_state)->num_write;
        conflict |= compare_lists(ev_read_list, tx_write_list, ev_len, tx_len);
        if(!conflict){
            LCG_CONF_REP("committing event accesses!\r\n");
            ev_commit();
        }
        else {
            LCG_CONF_REP("Conflict! Cannot commit\r\n");
            // For debugging, print off everything:
#ifdef LCG_CONF_ALL
            uint16_t num_devv = ((ev_state*)(curctx->extra_ev_state))->num_devv;
            LCG_CONF_ALL("Event contents:\r\n");
            for(uint16_t i = 0; i < num_devv ; i++) {
              LCG_CONF_ALL("%x\t", ev_dirty_src[i]);
            }
            LCG_CONF_ALL("\r\n");
            LCG_CONF_ALL("Tx contents:\r\n");
            for(uint16_t i = 0; i < num_dtxv_start; i++) {
              LCG_CONF_ALL("%x\t", tx_dirty_src[i]);
            }
            LCG_CONF_ALL("\r\n");
#endif
        }
    }
    else {
        LCG_PRINTF("No concurrent event \r\n");
    }
}

/*
 * @brief an internal function for committing if the transaction is defined as
 * serialize after
 * @comments my apologies for the spaghetti code, this function does have
 * multiple exit points...
 */
static void tx_commit_txsa() {
    LCG_PRINTF("In tx_commit ser after!\r\n");
    if(((ev_state *)(curctx->extra_ev_state))->ev_need_commit == FUTURE_COMMIT) {
        // Commit event accesses back to main memory

        // Now compare filters to see if any tx read a value written by the
        // concurrent event(s), making it impossible to serialize after
        int conflict = 0;
        uint16_t ev_len = ((ev_state *)curctx->extra_ev_state)->num_write;
        uint16_t tx_len = ((tx_state *)curctx->extra_state)->num_read;
        conflict |= compare_lists(tx_read_list, ev_write_list, tx_len, ev_len);
        //conflict |= compare_filters(read_filters + THREAD, write_filters +
        //EV);
        if(conflict){
            LCG_CONF_REP("Conflict! Must rollback transaction\r\n");
            // Resetting the curctx task so we go back to the top of the tx
            // We won't set any other state here for clarity since we don't
            // touch it in the other commit function, but we do risk some
            // pathological cases where you slosh back and forth between
            // checking for conflicts and restarting the transaction. Clearing
            // the tx_need_commit flag would probably help, but also makes
            // things more confusing IMHO.
            curctx->task = cur_tx_start;
            LCG_PRINTF("committing event accesses!\r\n");
            // Artificially set num_dtxv to 0 so we can always run through the
            // commit phase.
            ((tx_state *)(curctx->extra_state))->num_dtxv = 0;
            // Run ev_commit so we set the ev_need_commit flag to commit_done
            // instead of future commit
            ev_commit();

            return;
        }
        else {
            LCG_PRINTF("committing event accesses!\r\n");
            ev_commit();
        }
    }
    else {
        LCG_PRINTF("No concurrent event \r\n");
    }
    // Copy all tx buff entries to main memory
    while(((tx_state *)(curctx->extra_state))->num_dtxv > 0) {
        uint16_t num_dtxv =((tx_state *)(curctx->extra_state))->num_dtxv;
        LCG_PRINTF("Copying %x from %x to %x \r\n", 
                    *((uint16_t *)tx_dirty_dst[num_dtxv - 1]),
                    tx_dirty_dst[num_dtxv - 1],
                    tx_dirty_src[num_dtxv-1]);
        memcpy( tx_dirty_src[num_dtxv -1],
                tx_dirty_dst[num_dtxv - 1],
                tx_dirty_size[num_dtxv - 1]
        );
        ((tx_state *)(curctx->extra_state))->num_dtxv--;
    }
}

/*
 * @brief write back to source on transaction commit
 * @notes uhh, this sucker runs regardless... this could be bad
 */
void tx_commit() {

    // Choose correct commit function depending on serialize condition in tx
    if(((tx_state *)(curctx->extra_state))->serialize_after == 0) {
      tx_commit_txsb();
    }
    else {
      tx_commit_txsa();
    }
    // Clear out persistent filter data if we know comparison with ongoing event
    // is done
    if(((ev_state *)(curctx->extra_ev_state))->ev_need_commit == COMMIT_DONE) {
      ((ev_state *)(curctx->extra_ev_state))->num_read = 0;
      ((tx_state *)(curctx->extra_state))->num_read = 0;
      ((ev_state *)(curctx->extra_ev_state))->num_write = 0;
      ((tx_state *)(curctx->extra_state))->num_write = 0;
    }

    // zeroing need_tx_commit MUST come after removing in_tx condition since we
    // perform tx_commit based on the need_tx_commit flag
    // But we clear the serialize after flag before in_tx so that we're
    // guaranteed to clear it now that we're done committing
    ((tx_state *)(curctx->extra_state))->serialize_after = 0;
    ((tx_state *)(curctx->extra_state))->in_tx = 0;
    ((tx_state *)(curctx->extra_state))->tx_need_commit = 0;
    ((ev_state *)(curctx->extra_ev_state))->ev_need_commit= NO_COMMIT;

    need_tx_commit = 0;
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

