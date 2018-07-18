#include <msp430.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>


#ifndef LIBCOATIGCC_ENABLE_DIAGNOSTICS
#define LCG_PRINTF(...)
#else
#include <stdio.h>
#define LCG_PRINTF printf
#endif
#include "top_half.h"
#include "coati.h"
#include "tx.h"
#include "event.h"
#include "types.h"

#ifdef LIBCOATIGCC_TEST_TIMING
#pragma message "setup timing test"
// For instrumentation
__nv unsigned overflows = 0;
__nv unsigned overflows1 = 0;
unsigned transition_ticks = 0;
unsigned rw_ticks = 0;

unsigned ev_tran_ticks = 0;
unsigned overflows_ev_tran = 0;

unsigned tx_tran_ticks = 0;
unsigned overflows_tx_tran = 0;

unsigned tsk_tran_ticks = 0;
unsigned overflows_tsk_tran = 0;

unsigned tsk_in_tx_tran_ticks = 0;
unsigned overflows_tsk_in_tx_tran = 0;


unsigned errors = 0;
unsigned trans_starts= 0;
unsigned trans_stops= 0;
unsigned instrument = 1;

trans_type cur_trans = TSK_CMT;
#endif

#ifdef LIBCOATIGCC_TEST_COUNT
unsigned r_tsk_counts = 0;
unsigned r_tx_counts = 0;
unsigned r_ev_counts = 0;
unsigned w_tsk_counts = 0;
unsigned w_tx_counts = 0;
unsigned w_ev_counts = 0;
unsigned access_len = 0;
__nv unsigned total_access_count = 0;
unsigned instrument = 1;
#endif

#ifdef LIBCOATIGCC_TEST_TX_TIME
#pragma message ("Setting tx time!")
unsigned overflows_tx = 0;
unsigned tx_ticks = 0;
unsigned tx_count = 0;
unsigned wait_count = 0;
unsigned pause = 0;
#endif

#ifdef LIBCOATIGCC_TEST_EV_TIME
unsigned overflows_ev = 0;
unsigned ev_ticks = 0;
unsigned ev_count = 0;
#endif

#if defined(LIBCOATIGCC_TEST_WAIT_TIME)
unsigned overflows_wait = 0;
unsigned wait_ticks = 0;
unsigned wait_count = 0;

/*void __attribute__((interrupt(TIMER0_A1_VECTOR))) Timer0_A1_ISR(void) {
  TA0CTL = TACLR;
  overflows_wait++;
  TA0CTL = TASSEL__SMCLK | MC__CONTINUOUS | ID_3 | TACLR | TAIE;
}*/
#endif

#ifdef LIBCOATIGCC_TEST_DEF_COUNT
unsigned item_count = 0;
unsigned instrument = 1;
#endif
/* To update the context, fill-in the unused one and flip the pointer to it */
__nv context_t context_1 = {0};
__nv context_t context_0 = {
    .task = TASK_REF(_entry_task),
    .extra_state = &state_0,
    .extra_ev_state = &state_ev_0,
    .commit_state = TSK_PH1
};


__nv context_t * volatile curctx = &context_0;

// Set these up here so we can access the pointers easily
__nv context_t * volatile context_ptr0 = &context_0;
__nv context_t * volatile context_ptr1 = &context_1;

// for internal instrumentation purposes
__nv volatile unsigned _numBoots = 0;

// task dirty buffer data
__nv void * tsk_src[NUM_DIRTY_ENTRIES];
__nv void * tsk_dst[NUM_DIRTY_ENTRIES];
__nv size_t tsk_size[NUM_DIRTY_ENTRIES];
__nv uint8_t tsk_buf[BUF_SIZE];


// volatile number of task buffer entries that we want to clear on reboot
volatile uint16_t num_tbe = 0;

// nv count of the number of dirty global variables to be committed
__nv uint16_t num_dtv = 0;

// volatile record of type of commit needed
volatile commit commit_type = NO_COMMIT;
// Bundle of functions internal to the library
static void * tsk_buf_alloc(void *, size_t);
static int16_t tsk_find(const void *addr);
static void tsk_commit_ph2(void);

/**
 * @brief Function that copies data to main memory from the task buffers
 * @notes based on persistent var num_dtv
 */
void tsk_commit_ph2() {
  // Copy all commit list entries
  LCG_PRINTF("ph2: num_dtv = %u\r\n",num_dtv);
  LCG_PRINTF("commit_ph2, committing %u entries\r\n",num_dtv);
  while(num_dtv > 0)  {
    // Copy from dst in tsk buf to "home" for that variable
    LCG_PRINTF("Copying to %x\r\n",tsk_src[num_dtv-1]);
    memcpy( tsk_src[num_dtv - 1],
            tsk_dst[num_dtv - 1],
            tsk_size[num_dtv - 1]
          );
    num_dtv--;
  }
}

/*
 * @brief returns the index into the buffers of where the data is located
 */
int16_t  tsk_find(const void * addr) {
  if(num_tbe) {
    for(int i = 0; i < num_tbe; i++) {
      #ifdef LIBCOATIGCC_TEST_COUNT
      access_len++;
      #endif
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
    LCG_PRINTF("ptr check: %x %x, buf = %x\r\n",tsk_dst[num_tbe - 1], new_ptr,tsk_buf);
    LCG_PRINTF("Writing to %x, from = %x, num_tbe = %x \r\n", new_ptr,
        tsk_src[num_tbe -1], num_tbe);
    return (void *) new_ptr;
}

/*
 * @brief Returns a pointer to the value stored in the buffer a the src address
 * provided or the value in main memory
 */
void * read(const void *addr, unsigned size, acc_type acc) {
    #ifdef LIBCOATIGCC_TEST_COUNT
      access_len = 0;
      total_access_count++;
      if (((ev_state *)curctx->extra_ev_state)->in_ev) {
        r_ev_counts++;
      }
      else if(((tx_state *)curctx->extra_state)->in_tx) {
        r_tx_counts++;
      }
      else {
        r_tsk_counts++;
      }
    #endif
    int index;
    void * dst;
    uint16_t read_cnt;
    switch(acc) {
        case EVENT:
            // Only add to read list if we're buffering all updates
            #ifdef LIBCOATIGCC_BUFFER_ALL
            if(((tx_state *)thread_ctx->extra_state)->in_tx &&
               ((tx_state *)thread_ctx->extra_state)->serialize_after == 0) {
              // add the address to the read list
              read_cnt = ((ev_state *)curctx->extra_ev_state)->num_read;
              read_cnt += num_evread;
              if(read_cnt >= EV_NUM_DIRTY_ENTRIES) {
                int test = 0;
                // A bit of a cheat to keep the usual overhead low if you're
                // accessing the same memory in a task. This could all be obviated
                // by a compiler pass that determined the maximum number of reads
                // that are possible.
                test = check_list(ev_read_list, read_cnt, addr);
                if(!test) {
                  printf("Out of space in ev read list!\r\n");
                  while(1);
                }
              }
              else {
                ev_read_list[read_cnt] = (void *) addr;
                num_evread++;
              }
            }
            #endif // BUFFER_ALL

            // first check buffer for value
            index = ev_find(addr);
            // Now pull the memory from somewhere
            LCG_PRINTF("ev index = %u \r\n", index);
            if(index > -1) {
               dst = (void *) ev_dst[index];
              LCG_PRINTF("rd: index = %u, Found, Buffer vals: ", index);
            }
            // Return main memory addr
            else {
                dst = (void *) addr;
            }
            break;
        #ifdef LIBCOATIGCC_TEST_COUNT
        case NORMAL_NI:
            instrument = 0;
            // Fall through
        #endif
        case NORMAL:
            index = tsk_find(addr);
            if(index > -1) {
                LCG_PRINTF("Found addr %x at buf dst %x\r\n",
                            addr,tsk_dst[index]);
                dst = (void *) tsk_dst[index];
            }
            else {
                LCG_PRINTF("Addr %x not in buffer \r\n", addr);
                dst = (void *) addr;
            }
            break;
        #ifdef LIBCOATIGCC_TEST_COUNT
        case TX_NI:
            instrument = 0;
            // Fall through
        #endif
        case TX:
            // Add to read filter for tx
            LCG_PRINTF("TX READ %x\r\n",addr);
            // Don't record read/write lists unless we're monitoring everything
            #ifdef LIBCOATIGCC_BUFFER_ALL
            if(((tx_state *)curctx->extra_state)->serialize_after) {
              read_cnt = ((tx_state *)curctx->extra_state)->num_read;
              //printf("RC, NUM_TXR: %u %u\r\n",read_cnt, num_txread);
              read_cnt += num_txread;
              #ifndef LIBCOATIGCC_CHECK_ALL_TX
              if(read_cnt >= TX_NUM_DIRTY_ENTRIES) {
                int test = 0;
                test = check_list(tx_read_list, read_cnt, addr);
                if(!test) {
                  printf("Out of space in tx read list!\r\n");
                  while(1);
                }
              }
              else {
                tx_read_list[read_cnt] = (void *) addr;
                num_txread++;
              }
              #else
                int test = 0;
                test = check_list(tx_read_list,read_cnt,addr);
                /*if(test) {
                  printf("Seen before!\r\n");
                }*/
                if(!test && (read_cnt >= TX_NUM_DIRTY_ENTRIES)) {
                  printf("Really out of space in tx read list\r\n");
                  while(1);
                }
                if(!test) {
                  tx_read_list[read_cnt] = (void *) addr;
                  num_txread++;
                }
              #endif // CHECK_ALL_TX
            }
            #endif // BUFFER_ALL
            // check tsk buf
            index = tsk_find(addr);
            if(index > -1) {
                LCG_PRINTF("Found %x in tsk buf!\r\n",addr);
                dst = (void *) tsk_dst[index];
            }
            else {
            #ifdef LIBCOATIGCC_BUFFER_ALL // Not in tsk buf, so check tx buf
                LCG_PRINTF("Checking tx buf\r\n");
                index = tx_find(addr);
                if(index > -1) {
                    LCG_PRINTF("Found %x in tx buf!\r\n",addr);
                    dst = (void *) tx_dst[index];
                }
                // Not in tx buf either, return main memory addr
                else {
                    LCG_PRINTF("Back to main mem\r\n");
                    dst = (void *) addr;
                }
            #else
                // Go straight back to main memory
                dst = (void *) addr;
            #endif
            }
            break;
        default:
            printf("No valid type for read!\r\n");
            // Error!
            while(1);
    }
    LCG_PRINTF("Reading from %x \r\n",dst);
    #ifdef LIBCOATIGCC_TEST_COUNT
    // printf("\r\n%s: %u\r\n",curctx->task->name, access_len);
    if(instrument) {
      add_to_histogram(access_len);
    }
    else {
      instrument = 1;
    }
    #endif // TEST_COUNT
    return dst;
}


/*
 * @brief writes data from value pointer to address' location in task buf,
 * returns 0 if successful, -1 if allocation failed
 */
void write(const void *addr, unsigned size, acc_type acc, uint32_t value) {
    #ifdef LIBCOATIGCC_TEST_COUNT
      access_len = 0;
      total_access_count++;
      if (((ev_state *)curctx->extra_ev_state)->in_ev) {
        w_ev_counts++;
      }
      else if(((tx_state *)curctx->extra_state)->in_tx) {
        w_tx_counts++;
      }
      else {
        w_tsk_counts++;
      }
    #endif
    int index;
    uint16_t write_cnt;
    //LCG_PRINTF("value incoming = %i type = %i \r\n", value, acc);
    switch(acc) {
        case EVENT:
            LCG_PRINTF("Running event write!\r\n");
            #ifdef LIBCOATIGCC_BUFFER_ALL
            // check if we're in a tx and if we're *not* running 
            // serialize tx after
            if(((tx_state *)thread_ctx->extra_state)->in_tx &&
               ((tx_state *)thread_ctx->extra_state)->serialize_after) {
              // add to write list
              write_cnt = ((ev_state *)curctx->extra_ev_state)->num_write;
              write_cnt += num_evwrite;
              if(write_cnt >= EV_NUM_DIRTY_ENTRIES) {
                int test = 0;
                // A bit of a cheat to keep the usual overhead low if you're
                // accessing the same memory in a task. This could all be obviated
                // by a compiler pass that determined the maximum number of reads
                // that are possible.
                test = check_list(ev_write_list, write_cnt, addr);
                if(!test) {
                  printf("Out of space in ev write list!\r\n");
                  while(1);
                }
              }
              else {
                ev_write_list[write_cnt] = (void *) addr;
                num_evwrite++;
              }
            }
              #endif //BUFFER_ALL
            // Check if addr is already in buffer
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
            #ifdef LIBCOATIGCC_BUFFER_ALL
            // if we're not running serialize tx after
            if(((tx_state *)curctx->extra_state)->serialize_after == 0) {
              // Inc number of variables written
              write_cnt = ((tx_state *)curctx->extra_state)->num_write;
              write_cnt += num_txwrite;
              #ifndef LIBCOATIGCC_CHECK_ALL_TX
              if(write_cnt >= NUM_DIRTY_ENTRIES) {
                int test = 0;
                test = check_list(tx_write_list, write_cnt, addr);
                if(!test) {
                  printf("Out of space in tx write list!\r\n");
                  while(1);
                }
              }
              else {
                tx_write_list[write_cnt] = (void *) addr;
                num_txwrite++;
              }
              #else
                int test = 0;
                test = check_list(tx_write_list,write_cnt,addr);
                if(!test && (write_cnt >= NUM_DIRTY_ENTRIES)) {
                  printf("Really out of space in tx write %u\r\n", addr);
                  while(1);
                }
                if(!test) {
                  tx_write_list[write_cnt] = (void *) addr;
                  num_txwrite++;
                }
            
            #endif // CHECK_ALL_TX
            }
            #endif // BUFFER_ALL

            // Intentional fall through
        case NORMAL:
            index = tsk_find(addr);
            if(index > -1) {
              if (size == sizeof(char)) {
                *((uint8_t *) tsk_dst[index]) = (uint8_t) value;
              } else if (size == sizeof(uint16_t)) {
                *((unsigned *) tsk_dst[index]) = (uint16_t) value;
                    //printf("Wrote %x\r\n",*((uint16_t *)tsk_dst[index]));
              } else if (size == sizeof(uint32_t)) {
                *((uint32_t *) tsk_dst[index]) = (uint32_t) value;
              } else {
                    printf("Error! invalid size!\r\n");
                    while(1);
              }
            }
            else {
                void * dst = tsk_buf_alloc(addr, size);
                if(dst != NULL) {
                  if (size == sizeof(char)) {
                    *((uint8_t *) dst) = (uint8_t) value;
                  } else if (size == sizeof(uint16_t)) {
                    *((unsigned *) dst) = (uint16_t) value;
                    //printf("Wrote %x\r\n",*((uint16_t *)dst));
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
    #ifdef LIBCOATIGCC_TEST_COUNT
    // printf("\r\n%s: %u\r\n",curctx->task->name, access_len);
    if(instrument) {
      add_to_histogram(access_len);
    }
    else {
      instrument = 1;
    }
    #endif // TEST_COUNT
    return;
}

/*
 * @brief function invoked at beginning of transition to. It handles all of the
 * updates that need to happen atomically by handing in the next_x of all of the
 * state we're changing
 */
void commit_phase1(tx_state *new_tx, ev_state * new_ev,context_t *new_ctx) {
  LCG_PRINTF("commit_ph1 commit status: %u\r\n",curctx->commit_state);
  // First copy over the old tx_state and ev_states
  *new_tx = *((tx_state *) curctx->extra_state);
  *new_ev = *((ev_state *) curctx->extra_ev_state);
  #ifdef LIBCOATIGCC_BUFFER_ALL
  LCG_PRINTF("Sanity check1: %x %x, %x %x\r\n",new_tx->num_read,
                                ((tx_state*)curctx->extra_state)->num_read,
                                new_ev->num_read,
                                ((tx_state*)curctx->extra_state)->num_read);
  #endif // BUFFER_ALL

  switch(curctx->commit_state) {
    // We end up in the easy case if we finish a totally normal task
    case TSK_PH1:
      num_dtv = num_tbe;
      new_ev->in_ev = 0;
      new_ctx->commit_state = TSK_COMMIT;
      LCG_PRINTF("num_dtv = %u\r\n",num_dtv);
      break;
    case TX_PH1:
      num_dtv = num_tbe;
      new_ev->in_ev = 0;
      new_tx->in_tx = 0;
      #ifdef LIBCOATIGCC_BUFFER_ALL
      new_tx->num_read = ((tx_state *)curctx->extra_state)->num_read +
                          num_txread;
      new_tx->num_write = ((tx_state *)curctx->extra_state)->num_write +
                          num_txwrite;
      num_txwrite = 0;
      num_txread = 0;
      #endif // BUFFER_ALL
      new_ctx->commit_state = TX_COMMIT;
      break;
    
    #ifdef LIBCOATIGCC_BUFFER_ALL
    case TSK_IN_TX_PH1:
      num_dtv = num_tbe;
      new_ev->in_ev = 0;
      new_tx->num_read = ((tx_state *)curctx->extra_state)->num_read +
                          num_txread;
      new_tx->num_write = ((tx_state *)curctx->extra_state)->num_write +
                          num_txwrite;
      num_txwrite = 0;
      num_txread = 0;
      new_tx->num_dtxv = ((tx_state *)curctx->extra_state)->num_dtxv;
      new_ctx->commit_state = TSK_IN_TX_COMMIT;
      break;
      #endif // BUFFER_ALL

    case EV_PH1:
      num_dtv = num_tbe;

      #ifdef LIBCOATIGCC_BUFFER_ALL
      new_ev->in_ev = 0;
      new_ev->num_devv = ((ev_state *)curctx->extra_ev_state)->num_devv +
                         num_evbe;

      // Drop in transaction state from interrupted thread
      // Copy the ev contents though
      *new_tx = *((tx_state *)thread_ctx->extra_state);
      new_ctx->task = thread_ctx->task;
      if(((tx_state *)thread_ctx->extra_state)->in_tx == 0) {
        LCG_PRINTF("Only committing ev!\r\n");
        new_ctx->commit_state = EV_ONLY;
      }
      else {
        LCG_PRINTF("thread is in tx!\r\n");
        new_ev->num_read = ((ev_state *)curctx->extra_ev_state)->num_read +
                           num_evread;
        new_ev->num_write = ((ev_state *)curctx->extra_ev_state)->num_write +
                           num_evwrite;
        new_ctx->commit_state = EV_FUTURE;
      }
      #else
      new_ev->num_devv = ((ev_state *)curctx->extra_ev_state)->num_devv +
                         num_evbe;
      new_ctx->commit_state = EV_ONLY;
      // We're done walking the events, get ready to go back to the normal
      // thread.
      LCG_PRINTF("count: %u, committed: %u\r\n",((ev_state*)curctx->extra_ev_state)->count,
                                    ((ev_state*)curctx->extra_ev_state)->committed);
      if(((ev_state *)curctx->extra_ev_state)->count <= 
                            ((ev_state *)curctx->extra_ev_state)->committed + 1){
        LCG_PRINTF("Done! Heading back to %x\r\n", thread_ctx.task->func);
        new_ev->in_ev = 0;
        new_ctx->task = thread_ctx.task;
        // Clear the number of committed events
        new_ev->committed = 0;
        new_ev->count = 0;
      }
      else {
        // On to the next node!
        LCG_PRINTF("Next! %x\r\n",thread_ctx.task->func);
        new_ev->in_ev = 1;
        new_ctx->task = (task_t *)event_queue.tasks[((ev_state*)
                                          curctx->extra_ev_state)->committed + 1];
        // Increment the number of events we've committed
        new_ev->committed = ((ev_state*)curctx->extra_ev_state)->committed + 1;
      }
      new_ctx->commit_state = EV_ONLY;
      #endif // BUFFER_ALL
      break;
    default:
      printf("Wrong type for ph1 commit! %u\r\n",curctx->commit_state);
      while(1);
    }
    #ifdef LIBCOATIGCC_BUFFER_ALL
    LCG_PRINTF("Sanity check2: %x %x, %x %x\r\n",new_tx->num_dtxv,
                                  ((tx_state*)curctx->extra_state)->num_dtxv,
                                  new_ev->num_read,
                                  ((tx_state*)curctx->extra_state)->num_read);
    #endif // BUFFER_ALL
}

/**
 * @brief Function to be invoked before transferring control to a new task
 * @comments tricky change: tx commit can now manipulate curctx->task to get
 * back to the start of a transaction if it's rolling back, so yeah, take that
 * into consideration
 */
void commit_phase2() {
    while(curctx->commit_state != NO_COMMIT) {
      LCG_PRINTF("commit state, inside = %u, in ev= %u, in tx= %u\r\n",
                                                    curctx->commit_state,
                            ((ev_state *)curctx->extra_ev_state)->in_ev,
                            ((tx_state *)curctx->extra_state)->in_tx);
      LCG_PRINTF("CP2 addr %x\r\n",curctx->task->func);
      switch(curctx->commit_state) {
        case TSK_COMMIT:
          #ifdef LIBCOATIGCC_TEST_DEF_COUNT
          //printf("%u,0,0\r\n",num_dtv);
          if(instrument) {
            #ifdef LIBCOATIGCC_TSK_DEF_COUNT
            add_to_histogram(num_dtv);
            #endif
          }
          else
            instrument = 1;
          #endif
          tsk_commit_ph2();

          #ifdef LIBCOATIGCC_BUFFER_ALL
          curctx->commit_state = NO_COMMIT;
          #else
          if(((tx_state *)curctx->extra_state)->in_tx == 0) {
            LCG_PRINTF("Checking queue!\r\n");
            num_dtv = 0;
            num_tbe = 0;
            if(((ev_state *)curctx->extra_ev_state)->count > 0) {
              LCG_PRINTF("To the queue!\r\n");
              queued_event_handoff();
            }
          }
          curctx->commit_state = NO_COMMIT;
          #endif // BUFFER_ALL
          break;
        #ifdef LIBCOATIGCC_BUFFER_ALL
        case TSK_IN_TX_COMMIT:
          #ifdef LIBCOATIGCC_TEST_DEF_COUNT
          //printf("%u,0,0\r\n",num_dtv);
          if(instrument) {
            #ifdef LIBCOATIGCC_TSK_DEF_COUNT
            add_to_histogram(num_dtv);
            #endif
          }
          else
            instrument = 1;
          #endif
          tsk_in_tx_commit_ph2();
          LCG_PRINTF("dtxv = %u\r\n",((tx_state *)curctx->extra_state)->num_dtxv);
          curctx->commit_state = NO_COMMIT;
          break;
        case EV_FUTURE:
          ((ev_state *)curctx->extra_ev_state)->ev_need_commit = 1;
          curctx->commit_state = NO_COMMIT;
          break;
        #endif // BUFFER_ALL
        case TX_COMMIT:
          #ifdef LIBCOATIGCC_BUFFER_ALL
          #ifdef LIBCOATIGCC_TEST_DEF_COUNT
            #ifdef LIBCOATIGCC_TSK_DEF_COUNT
            item_count = num_dtv;
            if(!((tx_state *)curctx->extra_state)->in_tx) {
              if(instrument)
                add_to_histogram(item_count);
              else
                instrument = 1;
            }
            #endif 

          #endif
          // Finish committing current tsk
          tsk_in_tx_commit_ph2();
          
          #if defined(LIBCOATIGCC_TEST_DEF_COUNT) && \
              !defined(LIBCOATIGCC_TSK_DEF_COUNT)
          item_count = ((ev_state *)curctx->extra_ev_state)->num_devv
                      + ((tx_state *)curctx->extra_state)->num_dtxv;
          if(!((tx_state *)curctx->extra_state)->in_tx) {
            if(instrument)
              add_to_histogram(item_count);
            else
              instrument = 1;
          }
          #endif
          // Changes commit_state to any one of the following
          tx_commit_ph1_5();
          #else
          #ifdef LIBCOATIGCC_TEST_DEF_COUNT
            #ifdef LIBCOATIGCC_TSK_DEF_COUNT
            item_count = num_dtv;
            if(instrument)
              add_to_histogram(item_count);
            else
              instrument = 1;
            }
            #endif
          #endif
          // Commit last task
          tsk_commit_ph2();
          // Add new function for running outstanding events here
          // Clear all task buf entries before starting new task
          if(((ev_state *)curctx->extra_ev_state)->count > 0) {
            num_dtv = 0;
            num_tbe = 0;
            queued_event_handoff();
          }
          curctx->commit_state = NO_COMMIT;
          #endif
          break;
        case TX_ONLY:
          #ifdef LIBCOATIGCC_BUFFER_ALL
          ((tx_state *)curctx->extra_state)->num_read = 0;
          ((tx_state *)curctx->extra_state)->num_write = 0;
          ((ev_state *)curctx->extra_ev_state)->num_read = 0;
          ((ev_state *)curctx->extra_ev_state)->num_write = 0;
          ((ev_state *)curctx->extra_ev_state)->num_devv = 0;
          tx_commit_ph2();
          #else
          // Add new function for handling end of a tx
          #endif
          curctx->commit_state = NO_COMMIT;
          break;
        case EV_ONLY:
          #ifdef LIBCOATIGCC_BUFFER_ALL
          #ifdef LIBCOATIGCC_TEST_DEF_COUNT
            #ifdef LIBCOATIGCC_EV_DEF_COUNT
          if(((tx_state *)curctx->extra_state)->in_tx == 0) {
            item_count = ((ev_state *)curctx->extra_ev_state)->num_devv;
          if(instrument)
            add_to_histogram(item_count);
          else
            instrument = 1;
            //printf("0,%u,0\r\n",item_count);
          }
          #endif
          #endif
          ((tx_state *)curctx->extra_state)->num_read = 0;
          ((tx_state *)curctx->extra_state)->num_write = 0;
          ((ev_state *)curctx->extra_ev_state)->num_read = 0;
          ((ev_state *)curctx->extra_ev_state)->num_write = 0;
          #else
          #ifdef LIBCOATIGCC_TEST_DEF_COUNT
            #ifdef LIBCOATIGCC_EV_DEF_COUNT
          item_count = ((ev_state *)curctx->extra_ev_state)->num_devv;
          if(instrument)
            add_to_histogram(item_count);
          else
            instrument = 1;
          //printf("0,%u,0\r\n",item_count);
          #endif // EV_DEF_COUNT
          #endif // TEST_DEF_COUNT
          #endif //BUFFER_ALL
          ev_commit_ph2();
          curctx->commit_state = NO_COMMIT;
          LCG_PRINTF("num_devv = %u\r\n",
                              ((ev_state *)curctx->extra_ev_state)->num_devv);
          break;
        #ifdef LIBCOATIGCC_BUFFER_ALL
        case TX_EV_COMMIT:
          ((tx_state *)curctx->extra_state)->num_read = 0;
          ((tx_state *)curctx->extra_state)->num_write = 0;
          ((ev_state *)curctx->extra_ev_state)->num_read = 0;
          ((ev_state *)curctx->extra_ev_state)->num_write = 0;
          tx_commit_ph2();
          ev_commit_ph2();
          curctx->commit_state = NO_COMMIT;
          break;
        case EV_TX_COMMIT:
          ((tx_state *)curctx->extra_state)->num_read = 0;
          ((tx_state *)curctx->extra_state)->num_write = 0;
          ((ev_state *)curctx->extra_ev_state)->num_read = 0;
          ((ev_state *)curctx->extra_ev_state)->num_write = 0;
          ev_commit_ph2();
          tx_commit_ph2();
          curctx->commit_state = NO_COMMIT;
          break;
        #endif // BUFFER_ALL
        // Catch here if we didn't finish phase 1
        case TSK_PH1:
        case TX_PH1:
        case EV_PH1:
        case TSK_IN_TX_PH1:
          curctx->commit_state = NO_COMMIT;
          break;
        default:
          printf("Error! incorrect phase2 commit value: %x\r\n",
                                                  curctx->commit_state);
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
  // It'd be great to have this time include disabling events, but that throws
  // off our count of start/stops and mixes event and normal transition times 
  TRANS_TIMER_START
  LCG_PRINTF("TT commit state = %u, in tx = %u, addr %x\r\n",curctx->commit_state,
  ((tx_state *)curctx->extra_state)->in_tx, curctx->task->func);

  context_t *next_ctx;
  tx_state *new_tx_state;
  ev_state *new_ev_state;
  #ifdef LIBCOATIGCC_BUFFER_ALL
  // Point next context at the thread if we're returning from an ev
  if(((ev_state *)curctx->extra_ev_state)->in_ev) {
    LCG_PRINTF("Setting up thread!\r\n");
    next_ctx = thread_ctx;
    new_tx_state = (thread_ctx->extra_state == &state_0 ? &state_1 : &state_0);
    new_ev_state = (thread_ctx->extra_ev_state == &state_ev_0 ? &state_ev_1 :
                    &state_ev_0);
  }
  else {
    next_ctx = (curctx == &context_0 ? &context_1 : &context_0);
    new_tx_state = (curctx->extra_state == &state_0 ? &state_1 : &state_0);

    new_ev_state = (curctx->extra_ev_state == &state_ev_0 ? &state_ev_1 :
                    &state_ev_0);
  }
  #else
    next_ctx = (curctx == &context_0 ? &context_1 : &context_0);
    new_tx_state = (curctx->extra_state == &state_0 ? &state_1 : &state_0);

    new_ev_state = (curctx->extra_ev_state == &state_ev_0 ? &state_ev_1 :
                    &state_ev_0);
  #endif // BUFFER_ALL

  // Set the next task here so we don't overwrite modifications from commit_phase1
  next_ctx->task = next_task;

  // Performs first phase of the commit depending on what kind of task we're
  // in and sets up the next task n'at
  commit_phase1(new_tx_state, new_ev_state, next_ctx);

  // Now point the next context
  next_ctx->extra_state = new_tx_state;
  next_ctx->extra_ev_state = new_ev_state;
  #if defined(LIBCOATIGCC_ATOMICS) && defined(LIBCOATIGCC_TEST_TX_TIME)
  // Start timer if we're going from not in atomic region to atomic region
  if(!curctx->task->atomic && next_ctx->task->atomic && 
           !(((ev_state *)curctx->extra_ev_state)->in_ev)) {
    TX_TIMER_START;
  }
  // Stop timer if we're leaving an atomic region
  if(curctx->task->atomic && !next_ctx->task->atomic && 
          !(((ev_state *)curctx->extra_ev_state)->in_ev)) {
    TX_TIMER_STOP;
  }
  #endif
  curctx = next_ctx;
  #ifdef LIBCOATIGCC_BUFFER_ALL
  LCG_PRINTF("Got %x num_dtxv\r\n",((tx_state *)curctx->extra_state)->num_dtxv);
  #endif
  LCG_PRINTF("TT starting commit_ph1 = %u\r\n",curctx->commit_state);

  // Run the second phase of commit
  commit_phase2();

  LCG_PRINTF("Transitioning to %x\r\n",curctx->task->func);

  // Re-enable events if we're staying in the threads context, but leave them
  // disabled if we're going into an event task
  //if(((ev_state *)curctx->extra_state)->in_ev == 0){
  // Given that we never explicitly transition to and event, there shoudln't be
  // a check that we're in an event.
  // enable events (or don't because we're in an atomic region) now that
  // commit_phase2 is done
    #if defined(LIBCOATIGCC_ATOMICS) && defined(LIBCOATIGCC_ATOMICS_HW)
    if(curctx->task->atomic == 0) {
      LCG_PRINTF("not atomic\r\n");
      TRANS_TIMER_STOP
      _enable_events();
    }
    else {
      LCG_PRINTF("atomic\r\n");
      TRANS_TIMER_STOP
      _disable_events();
    }
  #else
    TRANS_TIMER_STOP
    _enable_events();
  #endif
  //}
  __asm__ volatile ( // volatile because output operands unused by C
      "mov #0x2400, r1\n"
      "br %[ntask]\n"
      :
      : [ntask] "r" (curctx->task->func)
  );

}

// Note, we're not adding timer instrumentation to main because the timers are
// only being used on continuous power
/** @brief Entry point upon reboot */
int main() {
    // Init needs to set up all the hardware, but leave interrupts disabled so
    // we can selectively enable them later based on whether we're dealing with
    // an event or not
    _init();
    _numBoots++;
    LCG_PRINTF("main commit state: %x\r\n",curctx->commit_state);
    #ifdef LIBCOATIGCC_TEST_DEF_COUNT
    #pragma message ("Delaying for def test")
    //__delay_cycles(4000000);
    #endif
    // Resume execution at the last task that started but did not finish
    #ifdef LIBCOATIGCC_BUFFER_ALL
    // Check if we're in an event
    // TODO: task the fluff out of this so it's not so bulky. Most of
    // transition_to doesn't apply in this case
    if(((ev_state *)curctx->extra_ev_state)->in_ev) {
      // Safe to manipulate the commit state b/c all of the volatile counters
      // have been cleared from the failed run
      curctx->commit_state = EV_PH1;
      // Just run this so all of the state needed for future events running
      // inside the same tx and all that will get transferred.
      transition_to(thread_ctx->task);
    }
    else {
      // Run second phase of commit
      commit_phase2();
    }
    #else
    commit_phase2();
    #endif //BUFFER_ALL
    LCG_PRINTF("Done phase 2 commit\r\n");
    // enable events (or don't because we're in an atomic region) now that
    // commit_phase2 is done
    #if defined(LIBCOATIGCC_ATOMICS) && defined(LIBCOATIGCC_ATOMICS_HW)
      if(curctx->task->atomic == 0) {
        LCG_PRINTF("not atomic\r\n");
        _enable_events();
      }
      else {
        LCG_PRINTF("atomic\r\n");
        _disable_events();
      }
    #else
      #ifdef LIBCOATIGCC_BUFFER_ALL
      _enable_events();
      #else
        if(((ev_state *)(curctx->extra_ev_state))->in_ev == 0){
          _enable_events();
        }
        else {
          _disable_events();
        }
      #endif // BUFFER_ALL
    #endif // ATOMICS

    LCG_PRINTF("transitioning to %x %x \r\n",curctx->task->func,
    (TASK_REF(_entry_task))->func);
    __asm__ volatile ( // volatile because output operands unused by C
        "br %[nt]\n"
        : /* no outputs */
        : [nt] "r" (curctx->task->func)
    );

    return 0; // TODO: write our own entry point and get rid of this
}

#if defined(LIBCOATIGCC_TEST_TIMING) || defined(LIBCOATIGCC_TEST_TX_TIME) \
  || defined(LIBCOATIGCC_TEST_EV_TIME) || defined(LIBCOATIGCC_TEST_WAIT_TIME)
void add_ticks(unsigned *overflow, unsigned *ticks, unsigned new_ticks) {
  if(new_ticks > (0xFFFF - *ticks)) {
    (*overflow)++;
    *ticks = new_ticks - (0xFFFF - *ticks);
  }
  else {
    *ticks += new_ticks;
  }
  return;
}

void __attribute__((interrupt(TIMER0_A1_VECTOR))) Timer0_A1_ISR(void) {
  TA0CTL = TACLR;
  #ifdef LIBCOATIGCC_TEST_EV_TIME
    overflows_ev++;
  #elif defined(LIBCOATIGCC_TEST_TX_TIME)
    overflows_tx++;
    /*TA0EX0 |= 0x3; */
  #elif defined(LIBCOATIGCC_TEST_WAIT_TIME)
    overflows_wait++;
    //TA0EX0 |= 0x3; 
  #elif defined(LIBCOATIGCC_TEST_TIMING)
    errors++;
  #endif
  TA0CTL = TASSEL__SMCLK | MC__CONTINUOUS | ID_3 | TACLR | TAIE;
}
#if 0
void __attribute__((interrupt(TIMER0_A0_VECTOR))) Timer0_A0_ISR(void) {
    TA0CTL = TACLR;
switch(__even_in_range(TA0IV, TA0IV_TAIFG))
  {
    case TA0IV_NONE:   break;               // No interrupt
    case TA0IV_TACCR1: break;               // CCR1 not used
    case TA0IV_TACCR2: break;               // CCR2 not used
    case TA0IV_3:      break;               // reserved
    case TA0IV_4:      break;               // reserved
    case TA0IV_5:      break;               // reserved
    case TA0IV_6:      break;               // reserved
    case TA0IV_TAIFG:                       // overflow
    #ifdef LIBCOATIGCC_TEST_EV_TIME
      overflows_ev++;
    #elif defined(LIBCOATIGCC_TEST_TX_TIME)
      overflows_tx++;
    #endif
    //TA0CTL = TASSEL__ACLK | MC__CONTINUOUS | ID_0 | TACLR | TAIE;

    default:
      break;
  }
}
#endif
#endif // TIMING,TX_TIME,EV_TIME...

