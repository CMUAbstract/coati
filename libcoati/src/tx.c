#include <stdarg.h>
#include <string.h>
#include <stdio.h>

#ifndef LIBCHAIN_ENABLE_DIAGNOSTICS
#define LIBCHAIN_PRINTF(...)
#else
#define LIBCHAIN_PRINTF printf
#endif

#include "coati.h"
#include "tx.h"
#include "filter.h"
#include "event.h"

__nv uint8_t tx_dirty_buf[BUF_SIZE];

__nv void * tx_dirty_src[NUM_DIRTY_ENTRIES];
__nv void * tx_dirty_dst[NUM_DIRTY_ENTRIES];
__nv size_t tx_dirty_size[NUM_DIRTY_ENTRIES];

// volatile number of tx buffer entries that we want to clear on reboot
volatile uint16_t num_txbe = 0;
// volatile flat that indicates need to commit transaction
volatile uint8_t need_tx_commit = 0;

// Pointers to state that'll get used by transactions library
__nv tx_state state_1 = {0};
__nv tx_state state_0 = {
    .num_dtxv = 0,
    .in_tx = 0,
    .tx_need_commit = 0
};

/*
 * @brief initializes state for a new tx
 * @comments Needs to take effect AFTER task prologue
 */
void tx_begin() {
    ((tx_state *)(curctx->extra_state))->num_dtxv = 0;
    ((tx_state *)(curctx->extra_state))->in_tx = 1;
    ((tx_state *)(curctx->extra_state))->tx_need_commit = 0;
    cur_tx_start = curctx->task;
    need_tx_commit = 0;
    num_txbe = 0;
}

/*
 * @brief set flags for committing a tx
 */
void tx_end() {
    need_tx_commit = 1;
}

/*
 * @brief returns the index into the tx buffers of where the data is located
 */
int16_t  tfind(const void * addr) {
    if(num_txbe) {
      for(int i = 0; i < num_txbe; i++) {
          if(addr == tx_dirty_src[i])
              return i;
      }
    }
    return -1;
}

/*
 * @brief returns the pointer into the tx dirty buf where the data is located
 */
void *  t_get_dst(void * addr) {
    if(num_txbe) {
      for(int i = 0; i < num_txbe; i++) {
          if(addr == tx_dirty_src[i])
              return tx_dirty_dst[i];
      }
    }
    return NULL;
}


/*
 * @brief Returns a pointer to the value stored in the task buffer or the
 * transaction buffer or (finally) the value in main memory
 */

void * tread(void * addr) {
    int index;
    index = find(addr);
    // check tsk buf
    if(index > -1) {
        return task_dirty_buf_dst[index];
    }
    else {
        // Not in tsk buf, so check tx buf
        index = tfind(addr);
        if(index > -1) {
            return tx_dirty_dst[index];
        }
        // Not in tx buf either, so add to filter and return main memory addr
        else {
            add_to_filter(filters + THREAD,(unsigned) addr);
            return addr;
        }
    }
}

/*
 * @brief sets the write back locations to the tx buf instead of main memory
 * @comments warning, this involves multiple linear searches all at once which
 * means it's super expensive but frankly is just as bad access-count-wise as
 * doing it on the fly in the middle of the task and at least we amortize the
 * function overheads
 */
void tcommit_ph1() {
    printf("In tcommit! \r\n");
    // check for new stuff to add to tx buf from tsk buf
    if(!num_tbe)
        return;

    for(int i = 0; i < num_tbe; i++) {
        // Step in and find the tx_buf and change the spot where commit_ph2 will
        // write back to
        // TODO add in some optimizations here for when we're committing the
        // transaction too. No need to write back to tx if committing after
        // active task

        // hunt for addr in tx buf and return new dst if it's in there
        void *tx_dst = t_get_dst(task_dirty_buf_src[i]);
        if(!tx_dst) {
            tx_dst = tx_dirty_buf_alloc(task_dirty_buf_src[i],
                      task_dirty_buf_size[i]);
            if(!tx_dst) {
                // Error! We ran out of space in tx buf
                return;
            }
        }
        task_commit_list_src[i] = tx_dst;
        task_commit_list_dst[i] = task_dirty_buf_dst[i];
        task_commit_list_size[i] = task_dirty_buf_size[i];

    }
    num_dtv = num_tbe;
}

/*
 * @brief allocs space in tx buffer and returns a pointer to the spot, returns
 * NULL if buf is out of space
 */
void * tx_dirty_buf_alloc(void * addr, size_t size) {
    uint16_t new_ptr;
    if(num_txbe) {
        new_ptr = (uint16_t) tx_dirty_dst[num_txbe - 1] +
        tx_dirty_size[num_txbe - 1];
    }
    else {
        new_ptr = (uint16_t) tx_dirty_buf;
    }
    if(new_ptr + size > (unsigned) (tx_dirty_buf + BUF_SIZE)) {
        return NULL;
    }
    else {
        num_txbe++;
        tx_dirty_src[num_txbe - 1] = addr;
        tx_dirty_dst[num_txbe - 1] = (void *) new_ptr;
        tx_dirty_size[num_txbe - 1] = size;
    }
    return (void *) new_ptr;
}



/*
 * @brief write back to source on transaction commit
 */
void tx_commit() {
    // Copy all tx buff entries to main memory
    while(((tx_state *)(curctx->extra_state))->num_dtxv > 0) {
        uint16_t num_dtxv =((tx_state *)(curctx->extra_state))->num_dtxv;
        memcpy( tx_dirty_src[num_dtxv -1],
                tx_dirty_dst[num_dtxv - 1],
                tx_dirty_size[num_dtxv - 1]
        );
        ((tx_state *)(curctx->extra_state))->num_dtxv--;
    }
    // zeroing need_tx_commit MUST come after removing in_tx condition since we
    // perform tx_commit based on the need_tx_commit flag
    ((tx_state *)(curctx->extra_state))->in_tx = 0;
    ((tx_state *)(curctx->extra_state))->tx_need_commit = 0;
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

