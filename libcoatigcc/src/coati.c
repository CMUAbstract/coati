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
#include "hash.h"

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
__nv table_t tsk_table;
__nv uint8_t tsk_buf[BUF_LEN];
__nv uint16_t tsk_buf_level = 0;

// Bundle of functions internal to the library
static void tsk_commit_ph2(void);

/**
 * @brief Function that copies data to main memory from the task buffers
 * @notes based on persistent var num_dtv
 */
void tsk_commit_ph2() {
  // Copy all commit list entries
  LCG_PRINTF("In tsk commit\r\n");
  while(tsk_table.active_bins > 0)  {
    uint16_t bin = tsk_table.active_bins - 1;
    uint16_t slot;
    // Walk through each slot in each bin w/ at least one value slotted in
    while(tsk_table.bucket_len[bin] > 0) {
      slot = tsk_table.bucket_len[bin] - 1;
      LCG_PRINTF("Bucket %u slot %u\r\n",bin, slot);
      // Copy from dst in tsk buf to "home" for that variable
      memcpy( tsk_table.src[bin][slot],
              tsk_table.dst[bin][slot],
              tsk_table.size[bin][slot]
            );
      LCG_PRINTF("Inserted %x to %x val = %u\r\n",
                                *((uint16_t *)tsk_table.dst[bin][slot]),
                                (uint16_t)tsk_table.src[bin][slot], 
                                *((uint16_t *)tsk_table.src[bin][slot]));
      // Decrement number of items in bin
      tsk_table.bucket_len[bin]--;
      LCG_PRINTF("tsk_comm buf level:%u\r\n",tsk_buf_level);
    }
    // Decrement number of bins left to check
    tsk_table.active_bins--;
  }
  tsk_buf_level = 0;
}

/*
 * @brief Returns a pointer to the value stored in the buffer a the src address
 * provided or the value in main memory
 */
void * read(const void *addr, size_t size, acc_type acc) {
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
    uint16_t flag;
    uint16_t test = 0;
    void * dst;
    switch(acc) {
        case EVENT:
            // Only add to read list if we're buffering all updates
            #ifdef LIBCOATIGCC_BUFFER_ALL
            #ifndef LIBCOATIGCC_SER_TX_AFTER
            // add the address to the read list
            test = add_to_src_table(&ev_read_table, addr);
            if(test) {
              printf("Error adding to ev_read_table!\r\n");
            }
            #endif // SER_TX_AFTER
            #endif // BUFFER_ALL
            // first check buffer for value
            flag = check_table(&ev_table, addr);
            if(flag != HASH_ERROR) {
               dst = (void *) flag;
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
            flag = check_table(&tsk_table, addr);
            if(flag != HASH_ERROR) {
                dst = (void *) flag;
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
            #ifdef LIBCOATIGCC_SER_TX_AFTER
            test = add_to_src_table(&tx_read_table, addr);
            if(test) {
              printf("Error adding to tx_read_table!\r\n");
            }
            #endif // SER_TX_AFTER
            // check tsk buf
            flag = check_table(&tsk_table, addr);
            if(flag != HASH_ERROR) {
                LCG_PRINTF("Found %x in tsk buf!\r\n",addr);
                dst = (void *) flag;
            }
            else {
            #ifdef LIBCOATIGCC_BUFFER_ALL // Not in tsk buf, so check tx buf
                LCG_PRINTF("Checking tx buf\r\n");
                flag = check_table(&tx_table, addr);
                if(flag != HASH_ERROR) {
                    LCG_PRINTF("Found %x in tx buf!\r\n",addr);
                    dst = (void *) flag;
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
    LCG_PRINTF("Reading %x from %x \r\n",addr, dst);
    #ifdef LIBCOATIGCC_TEST_COUNT
    // printf("\r\n%s: %u\r\n",curctx->task->name, access_len);
    if(instrument) {
      add_to_histogram(access_len);
    }
    else {
      instrument = 1;
    }
    #endif // TEST_COUNT
    LCG_PRINTF("TBR:%u ev:%u %u %u",tsk_buf_level, events_noted, _numEvents_missed,
                                  ((ev_state *)curctx->extra_ev_state)->count);
    return dst;
}


/*
 * @brief writes data from value pointer to address' location in task buf,
 * returns 0 if successful, -1 if allocation failed
 */
void write(const void *addr, size_t size, acc_type acc, void *value) {
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
    uint16_t test = 0;
    //LCG_PRINTF("value incoming = %i type = %i \r\n", value, acc);
    switch(acc) {
        case EVENT:
            LCG_PRINTF("Running event write!\r\n");
            #ifdef LIBCOATIGCC_BUFFER_ALL
            #ifdef LIBCOATIGCC_SER_TX_AFTER
            test = add_to_src_table(&ev_write_table, addr);
            if(test) {
              printf("Error adding to ev_write_table!\r\n");
              while(1);
            }
            #endif // SER_TX_AFTER
            #endif //BUFFER_ALL
            test = add_to_table(&ev_table, ev_buf, &ev_buf_level, addr, value, size);
            if(test) {
              printf("Error writing to ev buffer\r\n");
              while(1);
            }
            break;
        case TX:
            #ifdef LIBCOATIGCC_BUFFER_ALL
            #ifndef LIBCOATIGCC_SER_TX_AFTER
            test = add_to_src_table(&tx_write_table, addr);
            if(test) {
              printf("Error adding to tx_write_table!\r\n");
              while(1);
            }
            #endif // SER_TX_AFTER
            #endif // BUFFER_ALL
            // Intentional fall through
        case NORMAL:
            test = add_to_table(&tsk_table, tsk_buf, &tsk_buf_level, addr, value, size);
            if(test) {
              printf("Error writing to tsk buffer\r\n");
              P3OUT |= BIT7;
              P3DIR |= BIT7;
              P3OUT &= ~BIT7;
              while(1);
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
    LCG_PRINTF("TBW:%u ev:%u %u %u ",tsk_buf_level, events_noted, _numEvents_missed,
                                ((ev_state *)curctx->extra_ev_state)->count);
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

  switch(curctx->commit_state) {
    // We end up in the easy case if we finish a totally normal task
    // for TSK_PH1, just make sure in_ev=0, and we move on to tsk_commit
    case TSK_PH1:
      new_ev->in_ev = 0;
      tsk_table.active_bins = NUM_BINS;
      new_ctx->commit_state = TSK_COMMIT;
      break;
    // For TX_PH1 we disable both in_ev and in_tx and move on to TX_COMMIT since
    // the buffer lengths are latched by virtue of setting the commit state to
    // TX_PH1
    case TX_PH1:
      new_ev->in_ev = 0;
      new_tx->in_tx = 0;
      tsk_table.active_bins = NUM_BINS;
      #ifdef LIBCOATIGCC_BUFFER_ALL
      tx_table.active_bins = NUM_BINS;
      ev_table.active_bins = NUM_BINS;
      #endif // BUFFER_ALL
      new_ctx->commit_state = TX_COMMIT;
      break;

    #ifdef LIBCOATIGCC_BUFFER_ALL
    case TSK_IN_TX_PH1:
      tsk_table.active_bins = NUM_BINS;
      new_ctx->commit_state = TSK_IN_TX_COMMIT;
      break;
    #endif // BUFFER_ALL

    case EV_PH1:
      // TODO did this latch the updates from last time?
      ev_table.active_bins = NUM_BINS;
      #ifdef LIBCOATIGCC_BUFFER_ALL
      new_ev->in_ev = 0;
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
        new_ctx->commit_state = EV_FUTURE;
      }
      #else
      new_ctx->commit_state = EV_ONLY;
      // We're done walking the events, get ready to go back to the normal
      // thread.
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
}

/**
 * @brief Function to be invoked before transferring control to a new task
 * @comments tricky change: tx commit can now manipulate curctx->task to get
 * back to the start of a transaction if it's rolling back, so yeah, take that
 * into consideration
 */
void commit_phase2() {
  LCG_PRINTF("Phase2 state = %u\r\n",curctx->commit_state);
  while(curctx->commit_state != NO_COMMIT) {
    switch(curctx->commit_state) {
      case TSK_COMMIT:
        #ifdef LIBCOATIGCC_TEST_DEF_COUNT
          #error "TEST_DEF_COUNT broken"
        if(instrument)
          add_to_histogram(num_dtv);
        else
          instrument = 1;
        #endif
        tsk_commit_ph2();

        #ifdef LIBCOATIGCC_BUFFER_ALL
        curctx->commit_state = NO_COMMIT;
        #else
        if(((tx_state *)curctx->extra_state)->in_tx == 0) {
          LCG_PRINTF("Checking queue!\r\n");
          if(((ev_state *)curctx->extra_ev_state)->count > 0) {
            LCG_PRINTF("To the queue!\r\n");
            queued_event_handoff();
          }
          LCG_PRINTF("Empty queue!\r\n");
        }
        curctx->commit_state = NO_COMMIT;
        #endif // BUFFER_ALL
        break;
      #ifdef LIBCOATIGCC_BUFFER_ALL
      case TSK_IN_TX_COMMIT:
        #ifdef LIBCOATIGCC_TEST_DEF_COUNT
          #error "TEST_DEF_COUNT broken"
        if(instrument)
          add_to_histogram(num_dtv);
        else
          instrument = 1;
        #endif
        tsk_in_tx_commit_ph2();
        curctx->commit_state = NO_COMMIT;
        break;
      case EV_FUTURE:
        ((ev_state *)curctx->extra_ev_state)->ev_need_commit = 1;
        // Now clean out task buffers since we're returning
        for(int i = 0; i <  NUM_BINS; i++) {
          tsk_buf.bucket_len[i] = 0;
        }
        tsk_buf_level = 0;
        curctx->commit_state = NO_COMMIT;
        break;
      #endif // BUFFER_ALL
      case TX_COMMIT:
        #ifdef LIBCOATIGCC_BUFFER_ALL
        #ifdef LIBCOATIGCC_TEST_DEF_COUNT
          #error "TEST_DEF_COUNT broken"
        item_count = ((ev_state *)curctx->extra_ev_state)->num_devv + num_dtv
                    + ((tx_state *)curctx->extra_state)->num_dtxv;
        if(!((tx_state *)curctx->extra_state)->in_tx) {
          //printf("0,0,%u\r\n",item_count);
        if(instrument)
          add_to_histogram(item_count);
        else
          instrument = 1;
        }

        #endif
        // Finish committing current tsk
        tsk_in_tx_commit_ph2();
        // Changes commit_state to any one of the following
        tx_commit_ph1_5();
        #else
        #ifdef LIBCOATIGCC_TEST_DEF_COUNT
          #error "TEST_DEF_COUNT broken"
        item_count = num_dtv;
        if(instrument)
          add_to_histogram(item_count);
        else
          instrument = 1;
        }
        //printf("0,0,%u\r\n",item_count);
        #endif
        // Commit last task
        tsk_commit_ph2();
        // Add new function for running outstanding events here
        // Clear all task buf entries before starting new task
        if(((ev_state *)curctx->extra_ev_state)->count > 0) {
          queued_event_handoff();
        }
        curctx->commit_state = NO_COMMIT;
        #endif
        break;
      case TX_ONLY:
        #ifdef LIBCOATIGCC_BUFFER_ALL
        // Clear ev buffers
        ((ev_state *)curctx->extra_ev_state)->perm_buf_level = 0;
        for(int i = 0; i < NUM_BINS; i++) {
          ((ev_state *)curctx->extra_ev_state)->perm_sizes[i] = 0;
        }
        for(int i = 0; i < NUM_BINS; i++) {
          ((ev_state *)curctx->extra_ev_state)->src_sizes[i] = 0;
        }
        tx_commit_ph2();
        #else
        // Add new function for handling end of a tx
        #endif
        curctx->commit_state = NO_COMMIT;
        break;
      case EV_ONLY:
        #ifdef LIBCOATIGCC_BUFFER_ALL
        #ifdef LIBCOATIGCC_TEST_DEF_COUNT
          #error "TEST_DEF_COUNT broken"
        if(((tx_state *)curctx->extra_state)->in_tx == 0) {
          item_count = ((ev_state *)curctx->extra_ev_state)->num_devv;
        if(instrument)
          add_to_histogram(item_count);
        else
          instrument = 1;
          //printf("0,%u,0\r\n",item_count);
        }
        #endif// TEST_DEF_COUNT
        // Need to clear tsk_buf since we're restarting the interrupted task
        for(int i = 0; i < NUM_BINS; i++) {
          tsk_table.bucket_len[i] = 0;
        }
        tsk_buf_level = 0;
        #else // BUFFER_ALL
        #ifdef LIBCOATIGCC_TEST_DEF_COUNT
          #error "TEST_DEF_COUNT broken"
        item_count = ((ev_state *)curctx->extra_ev_state)->num_devv;
        if(instrument)
          add_to_histogram(item_count);
        else
          instrument = 1;
        #endif
        #endif //BUFFER_ALL
        ev_commit_ph2();
        curctx->commit_state = NO_COMMIT;
        break;
      #ifdef LIBCOATIGCC_BUFFER_ALL
      case TX_EV_COMMIT:
        tx_commit_ph2();
        ev_commit_ph2();
        curctx->commit_state = NO_COMMIT;
        break;
      case EV_TX_COMMIT:
        ev_commit_ph2();
        tx_commit_ph2();
        curctx->commit_state = NO_COMMIT;
        break;
      #endif // BUFFER_ALL
      // Catch here if we didn't finish phase 1
      case EV_PH1:
      #ifndef LIBCOATIGCC_BUFFER_ALL
      // If we're running split phase, wipe the ev bins to 0 and restart the
      // deferred event
        for(int i = 0; i < NUM_BINS; i++) {
          ev_table.bucket_len[i] = 0;
        }
        ev_buf_level = 0;
        curctx->commit_state = NO_COMMIT;
      #else
      // If we're in fully buffered we should never end up in here since fully
      // bufferd handles in_ev cases earlier.
        printf("Error! In EV_PH1 commit_phase2 after reboot!\r\n");
        while(1);
      #endif
        break;
      // For TSK, TX, TSK_IN_TX, wipe the bins and start the task again
      case TSK_PH1:
      case TX_PH1:
      case TSK_IN_TX_PH1:
        for(int i = 0; i < NUM_BINS; i++) {
          tsk_table.bucket_len[i] = 0;
        }
        tsk_buf_level = 0;
        curctx->commit_state = NO_COMMIT;
        break;
        curctx->commit_state = NO_COMMIT;
        break;
      default:
        printf("Error! incorrect phase2 commit value: %x\r\n",
                                                curctx->commit_state);
        while(1);
    }
  }
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
  LCG_PRINTF("Buf levels: %u %u\r\n", tsk_buf_level, ev_buf_level);
  // Now transition off ot the next task
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
    #ifdef LIBCOATIGCC_ENABLE_DIAGNOSTICS
    __delay_cycles(4000000);
    #endif
    LCG_PRINTF("main commit state: %x\r\n",curctx->commit_state);
    // Resume execution at the last task that started but did not finish
    #ifdef LIBCOATIGCC_BUFFER_ALL
    // Check if we're in an event
    // TODO: task the fluff out of this so it's not so bulky. Most of
    // transition_to doesn't apply in this case
    if(((ev_state *)curctx->extra_ev_state)->in_ev) {
      // Restore old counters and run with it (buf_level, table sizes, source
      // sizes)
      ev_buf_level = ((ev_state *)(curctx->extra_ev_state))->perm_buf_level;
      for(int i = 0; i < NUM_BINS; i++) {
        ev_table.bucket_len[i] = 
                ((ev_state*)(curctx->extra_ev_state))->perm_sizes[i];
      }
      #ifdef LIBCOATIGCC_SER_TX_AFTER
      for(int i = 0; i < NUM_BINS; i++) {
        ev_write_table.bucket_len[i] = 
                ((ev_state*)(curctx->extra_ev_state))->src_sizes[i];
      }
      #else
      for(int i = 0; i < NUM_BINS; i++) {
        ev_read_table.bucket_len[i] = 
                ((ev_state*)(curctx->extra_ev_state))->src_sizes[i];
      }
      #endif
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

