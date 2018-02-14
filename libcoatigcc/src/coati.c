#include <stdarg.h>
#include <string.h>
#include <stdio.h>


#ifndef LIBCOATIGCC_ENABLE_DIAGNOSTICS
#define LCG_PRINTF(...)
#else
#include <stdio.h>
#define LCG_PRINTF printf
#endif
#include "coati.h"
#include "tx.h"
#include "event.h"
#include "types.h"

/* To update the context, fill-in the unused one and flip the pointer to it */
__nv context_t context_1 = {0};
__nv context_t context_0 = {
    .task = TASK_REF(_entry_task),
    .extra_state = &state_0,
    .extra_ev_state = &state_ev_0
};


__nv context_t * volatile curctx = &context_0;

// Set these up here so we can access the pointers easily
__nv context_t * volatile context_ptr0 = &context_0;
__nv context_t * volatile context_ptr1 = &context_1;

// for internal instrumentation purposes
__nv volatile unsigned _numBoots = 0;

// task dirty buffer data
__nv void * tsk_src[NUM_DIRTY_ENTRIES];
__nv void * tsk__dst[NUM_DIRTY_ENTRIES];
__nv size_t tsk__size[NUM_DIRTY_ENTRIES];
__nv uint8_t tsk_buf[BUF_SIZE];


// volatile number of task buffer entries that we want to clear on reboot
volatile uint16_t num_tbe = 0;

// nv count of the number of dirty global variables to be committed
__nv uint16_t num_dtv = 0;

// volatile record of type of commit needed
volatile commit commit_type = NO_COMMIT;
// Bundle of functions internal to the library
static void commit_ph2();
static void tsk_commit_ph1();
static void * task_dirty_buf_alloc(void *, size_t);

/**
 * @brief Function that updates number of dirty variables in the persistent
 * state
 */
void tsk_commit_ph1() {
    LCG_PRINTF("new dtv = %u\r\n",num_tbe);
    num_dtv = num_tbe;
    commit_status = TSK_COMMIT;
}

/**
 * @brief Function that copies data to main memory from the task buffers
 * @notes based on persistent var num_dtv
 */
void tsk_commit_ph2() {
    // Copy all commit list entries
    while(num_dtv > 0)  {
      // Copy from dst in tsk buf to "home" for that variable
      LCG_PRINTF("Copying to %x\r\n",tsk_src[num_dtv-1]);
      memcpy( tsk_src[num_dtv - 1],
              tsk_dst[num_dtv - 1],
              tsk_size[num_dtv - 1]
            );
      num_dtv--;
    }
    commit_status = NO_COMMIT;
}


/*
 * @brief returns the index into the buffers of where the data is located
 */
int16_t  tsk_find(const void * addr) {
    if(num_tbe) {
      for(int i = 0; i < num_tbe; i++) {
          if(addr == tsk_src[i])
              return i;
      }
    }
    return -1;
}

/*
 * @brief allocs space in buffer and returns a pointer to the spot, returns NULL
 * if buf is out of space
 */
void * tsk_buf_alloc(void * addr, size_t size) {
    uint16_t new_ptr;
    // Totally valid to use volatile counter here on task level
    if(num_tbe) {
        // Set new pointer based on last pointer and the length of the var it
        // stores
        new_ptr = (uint16_t) tsk_dst[num_tbe - 1] + tsk_size[num_tbe - 1];
    }
    else {
        new_ptr = (uint16_t) tsk_buf;
    }
    // Fix alignment struggles
    if(size == 2) {
      while(new_ptr & 0x1)
        new_ptr++;
    }
    if(size == 4) {
      LCG_PRINTF("allocing 32 bit!\r\n");
      while(new_ptr & 0x11)
        new_ptr++;
    }
    // If we're out of space, throw an error
    if(new_ptr + size > (unsigned) (tsk_buf + BUF_SIZE)) {
        LCG_PRINTF("Returning null! %x > %x \r\n",
            new_ptr + size,(unsigned) (tsk_buf + BUF_SIZE));
        return NULL;
    }
    else {
        // Used to indicate how many volatile variables we've been able to store
        num_tbe++;
        tsk_src[num_tbe - 1] = addr;
        tsk_dst[num_tbe - 1] = (void *) new_ptr;
        tsk_size[num_tbe - 1] = size;
    }
    LCG_PRINTF("Writing to %x, from = %x, num_tbe = %x \r\n", new_ptr,
        tsk_src[num_tbe -1], num_tbe);
    return (void *) new_ptr;
}

/*
 * @brief Returns a pointer to the value stored in the buffer a the src address
 * provided or the value in main memory
 */
void * read(const void *addr, unsigned size, acc_type acc) {
    int index;
    void * dst;
    uint16_t read_cnt;
    switch(acc) {
        case EVENT:
            // first check buffer for value
            index = ev_find(addr);
            // add the address to the read list
            read_cnt = ((ev_state *)curctx->extra_ev_state)->num_read;
            read_cnt += num_evread;
            if(read_cnt >= NUM_DIRTY_ENTRIES) {
              printf("Out of space in ev read list!\r\n");
              while(1);
            }
            ev_read_list[read_cnt] = addr;
            num_evread++;
            // Now pull the memory from somewhere
            LCG_PRINTF("ev index = %u \r\n", index);
            if(index > -1) {
               dst = ev_dirty_dst[index];
              LCG_PRINTF("rd: index = %u, Found, Buffer vals: ", index);
              for(int16_t i = 0; i < 8; i++) {
                LCG_PRINTF("%x: ", ev_dirty_buf[i]);
              }
              LCG_PRINTF("\r\n");
            }
            // Return main memory addr
            else {
                dst = addr;
            }
            break;
        case NORMAL:
            index = tsk_find(addr);
            if(index > -1) {
                LCG_PRINTF("Found addr %x at buf dst %x\r\n",
                            addr,task_dirty_buf_dst[index]);
                dst = task_dirty_buf_dst[index];
            }
            else {
                LCG_PRINTF("Addr %x not in buffer \r\n", addr);
                dst = addr;
            }
            break;
        case TX:
            // Add to read filter for tx
            read_cnt = ((tx_state *)curctx->extra_state)->num_read;
            read_cnt += num_txread;
            if(read_cnt >= NUM_DIRTY_ENTRIES) {
              printf("Out of space in tx read list!\r\n");
              while(1);
            }
            tx_read_list[read_cnt] = addr;
            num_txread++;
            // check tsk buf
            index = tsk_find(addr);
            if(index > -1) {
                dst = tsk_dst[index];
            }
            else {
                // Not in tsk buf, so check tx buf
                LCG_PRINTF("Checking tx buf\r\n");
                index = tx_find(addr);
                if(index > -1) {
                    dst = tx_dst[index];
                }
                // Not in tx buf either, return main memory addr
                else {
                    dst = addr;
                }
            }
            break;
        default:
            printf("No valid type for read!\r\n");
            // Error!
            while(1);
    }
    LCG_PRINTF("Reading from %x \r\n",dst);
    return dst;
}


/*
 * @brief writes the value word to address' location in task buf,
 * returns 0 if successful, -1 if allocation failed
 */
void write_byte(void *addr, uint8_t value) {
    int index;
    index = tsk_find(addr);
    if(index > -1) {
        *((uint8_t *)(tsk_dst + index)) = value;
    }
    else {
        void * dst = tsk_buf_alloc(addr, 1);
        if(dst) {
            *((uint8_t *) dst) = value;
        }
        else {
            // Error! we ran out of space
            while(1);
            return;
        }
    }
    return;
}

/*
 * @brief writes the value word to address' location in task buf,
 * returns 0 if successful, -1 if allocation failed
 */
void write_word(void *addr, uint16_t value) {
    int index;
    index = tsk_find(addr);
    if(index > -1) {
        *((uint16_t *)(tsk_dst + index)) = value;
    }
    else {
        void * dst = tsk_alloc(addr, 2);
        if(dst) {
            *((uint16_t *) dst) = value;
        }
        else {
            // Error! we ran out of space
            while(1);
            return;
        }
    }
    return;
}

/*
 * @brief writes data from value pointer to address' location in task buf,
 * returns 0 if successful, -1 if allocation failed
 */
void write(const void *addr, unsigned size, acc_type acc, uint32_t value) {
    int index;
    uint16_t write_cnt;
    //LCG_PRINTF("value incoming = %i type = %i \r\n", value, acc);
    switch(acc) {
        case EVENT:
            LCG_PRINTF("Running event write!\r\n");
            // add to write list
            write_cnt = ((ev_state *)curctx->extra_ev_state)->num_write;
            write_cnt += num_evwrite;
            if(write_cnt >= NUM_DIRTY_ENTRIES) {
              printf("Out of space in write list!\r\n");
              while(1);
            }
            ev_write_list[write_cnt] = addr;
            num_evwrite++;
            index = ev_find(addr);
            if(index > -1) {
              if (size == sizeof(uint8_t)) {
                *((uint8_t *) ev_dst[index]) = (uint8_t) value;
              } else if(size == sizeof(uint16_t)) {
                *((uint16_t *) (ev_dst[index]) ) = (uint16_t) value;
              } else if(size == sizeof(uint32_t)) {
                *((uint32_t *) (ev_dst[index])) = (uint32_t) value;
              } else {
                  printf("Ev Error! invalid size!\r\n");
                  while(1);
              }
            }
            else {
                void * dst = ev_buf_alloc(addr, size);
                if(dst) {
                  if (size == sizeof(char)) {
                    *((uint8_t *) dst) = (uint8_t) value;
                  } else if(size == sizeof(uint16_t)) {
                    *((uint16_t *) dst) = (uint16_t) value;
                  } else if(size == sizeof(uint32_t)) {
                    *((uint32_t *) dst) = (uint32_t) value;
                  } else {
                    printf("Ev Error! invalid size!\r\n");
                    while(1);
                  }
                } else {
                    // Error! we ran out of space
                    printf("Ev Error! out of space!\r\n");
                    while(1);
                }
            }
            break;
        case TX:
            // Inc number of variables written
            write_cnt = ((tx_state *)curctx->extra_state)->num_write;
            write_cnt += num_txwrite;
            if(write_cnt >= NUM_DIRTY_ENTRIES) {
              printf("Out of space in read buff!\r\n");
              while(1);
            }
            tx_write_list[write_cnt] = addr;
            num_txwrite++;
            // Intentional fall through
        case NORMAL:
            index = tsk_find(addr);
            if(index > -1) {
              if (size == sizeof(char)) {
                *((uint8_t *) tsk_dst[index]) = (uint8_t) value;
              } else if (size == sizeof(uint16_t)) {
                *((unsigned *) tsk_dst[index]) = (uint16_t) value;
              } else if (size == sizeof(uint32_t)) {
                *((uint32_t *) tsk_dst[index]) = (uint32_t) value;
              } else {
                    printf("Error! invalid size!\r\n");
                    while(1);
              }
            }
            else {
                void * dst = tsk_buf_alloc(addr, size);
                if(dst) {
                  if (size == sizeof(char)) {
                    *((uint8_t *) dst) = (uint8_t) value;
                  } else if (size == sizeof(uint16_t)) {
                    *((unsigned *) dst) = (uint16_t) value;
                  } else if (size == sizeof(uint32_t)) {
                    *((uint32_t *) dst) = (uint32_t) value;
                  } else {
                    printf("Error! invalid size!\r\n");
                    while(1);
                  }
                }
                else {
                    // Error! we ran out of space
                    printf("Error! out of space!\r\n");
                    while(1);
                }
            }
            break;
        default:
            printf("Invalid type for write!\r\n");
            // Error!
            while(1);
    }
    return;
}

void *internal_memcpy(void *dest, void *src, uint16_t num) {
  if ((uintptr_t) dest % sizeof(unsigned) == 0 &&
      (uintptr_t) dest % sizeof(unsigned) == 0) {
    unsigned *d = dest;
    unsigned tmp;
    const unsigned *s = src;
    for (unsigned i = 0; i < num/sizeof(unsigned); i++) {
      tmp = *((unsigned *) read(&s[i], sizeof(unsigned), NORMAL));
      write(&d[i], sizeof(unsigned), NORMAL, tmp);
    }
  } else {
    char *d = dest;
    const char *s = src;
    char tmp;
    for (unsigned i = 0; i < num; i++) {
      tmp = *((char *) read(&s[i], sizeof(char), NORMAL));
      write(&d[i], sizeof(char), NORMAL, tmp);
    }
  }
  return dest;
}

/*
 * @brief function invoked at beginning of transition to. It handles all of the
 * updates that need to happen atomically by handing in the next_x of all of the
 * state we're changing
 */
void commit_phase1(tx_state *new_tx, ev_state * new_ev,context_t *new_ctx) {
  // First, if we're in an event, figure out if there's an ongoing transaction
  switch(commit_type) {
    // We end up in the easy case if we finish a totally normal task
    case TSK_PH1:
      num_dtv = num_tbe;
      new_ctx->commit_state = TSK_COMMIT;
      break;
    case TX_PH1:
      num_dtv = num_tbe;
      new_tx->num_read = ((tx_state *)curctx->extra_state)->num_read +
                          num_txread;
      new_tx->num_write = ((tx_state *)curctx->extra_state)->num_read +
                          num_txwrite;
      if(((tx_state *)curctx->extra_state)->tx_need_commit) {
        new_ctx->commit_state = TX_COMMIT;
      }
      else {
        new_ctx->commit_state = TSK_IN_TX_COMMIT;
      }
      break;
    case EV_PH1:
      num_dtv = num_tbe;
      new_ev->num_devv = ((ev_state *)curctx->extra_ev_state)->num_devv +
                         num_tbe;
      if(((tx_state *)thread_ctx->extra_state)->in_tx) {
        new_ctx->commit_state = EV_ONLY;
      }
      else {
        new_ev->num_read = ((ev_state *)curctx->extra_ev_state)->num_read +
                           num_evread;
        new_ev->num_write = ((ev_state *)curctx->extra_ev_state)->num_write +
                           num_evwrite;
        new_ctx->commit_state = EV_FUTURE;
      }
      break;
    default:
      printf("Wrong type for ph1 commit!\r\n");
      while(1);
    }
}

/**
 * @brief Function to be invoked before transferring control to a new task
 * @comments tricky change: tx commit can now manipulate curctx->task to get
 * back to the start of a transaction if it's rolling back, so yeah, take that
 * into consideration
 */
void commit_phase2() {
    while(curctx->commit_status != NO_COMMIT) {
      switch(curctx->commit_status) {
        case TSK_COMMIT:
          tsk_commit_ph2();
          curctx->commit_state = NO_COMMIT;
          break;
        case TSK_IN_TX_COMMIT:
          tsk_in_tx_commit_ph2();
          curctx->commit_state = NO_COMMIT;
          break;
        case EV_FUTURE:
          ((ev_state *)curctx->extra_ev_state)->ev_need_commit = 1;
          curctx->commit_state = NO_COMMIT;
        case TX_COMMIT:
          // Finish committing current tsk
          tsk_in_tx_commit_ph2();
          // Changes commit_status to any one of the following
          tx_commit_ph1_5();
          break;
        case EV_ONLY:
          ev_commit_ph2();
          curctx->commit_state = NO_COMMIT;
          break;
        case TX_EV_COMMIT:
          tx_commit_ph2();
          ev_commit_ph2();
          curctx->commit_state = NO_COMMIT;
          break;
        case EV_TX_commit:
          ev_commit_ph2();
          tx_commit_ph2();
          curctx->commit_state = NO_COMMIT;
          break;
        default:
          printf("Error! incorrect phase2 commit value\r\n");
          while(1);
      }
    }
    // Clear all task buf entries before starting new task
    num_dtv = 0;
    num_tbe = 0;
}

/**
 * @brief Transfer control to the given task
 * @details Finalize the current task and jump to the given task.
 *          This function does not return.
 *
 *  TODO: mark this function as bare (i.e. no prologue) for efficiency
 */
void transition_to(task_t *next_task)
{
   // disable event interrupts so we don't have to deal with them during
   // transition
    _disable_events();

    context_t *next_ctx;
    next_ctx = (curctx == &context_0 ? &context_1 : &context_0);

    tx_state *new_tx_state;
    new_tx_state = (curctx->extra_state == &state_0 ? &state_1 : &state_0);

    ev_state *new_ev_state;
    new_ev_state = (curctx->extra_ev_state == &state_ev_0 ? &state_ev_1 :
                    &state_ev_0);

    // Performs first phase of the commit depending on what kind of task we're
    // in and sets up the next task n'at
    commit_phase1(new_ev_state, new_tx_state,next_ctx);

    // Now plop those into the next context
    next_ctx->extra_state = new_tx_state;
    next_ctx->extra_ev_state = new_ev_state;
    curctx = next_ctx;

    // Run the second phase of commit
    commit_phase2();

    // Re-enable events if we're staying in the threads context, but leave them
    // disabled if we're going into an event task
    if(((ev_state *)curctx->extra_state)->in_ev == 0){
      _enable_events();
    }

    __asm__ volatile ( // volatile because output operands unused by C
        "mov #0x2400, r1\n"
        "br %[ntask]\n"
        :
        : [ntask] "r" (curctx->task->func)
    );

}


/** @brief Entry point upon reboot */
int main() {
    // Init needs to set up all the hardware, but leave interrupts disabled so
    // we can selectively enable them later based on whether we're dealing with
    // an event or not
    _init();

    _numBoots++;

    // Resume execution at the last task that started but did not finish

    LCG_PRINTF("transitioning to %x \r\n",curctx->task->func);
    LCG_PRINTF("tsk size = %i tx size = %i \r\n", NUM_DIRTY_ENTRIES, BUF_SIZE);

    // Run second phase of commit
    commit_phase2();

    // enable events now that commit_phase2 is done
    _enable_events();

    __asm__ volatile ( // volatile because output operands unused by C
        "br %[nt]\n"
        : /* no outputs */
        : [nt] "r" (curctx->task->func)
    );

    return 0; // TODO: write our own entry point and get rid of this
}
