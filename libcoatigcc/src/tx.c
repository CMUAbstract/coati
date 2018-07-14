#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <msp430.h>

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
__nv uint16_t tx_buf_level = 0;
__nv table_t tx_table;

#ifdef LIBCOATIGCC_BUFFER_ALL
#ifdef LIBCOATIGCC_SER_TX_AFTER
__nv src_table tx_read_table;
#else
__nv src_table tx_write_table;
#endif // SER_TX_AFTER

#endif // BUFFER_ALL


// Pointers to state that'll get used by transactions library
__nv tx_state state_1 = {0};
__nv tx_state state_0 = {
    #ifdef LIBCOATIGCC_BUFFER_ALL
    #endif
    .in_tx = 0,
    #ifdef LIBCOATIGCC_BUFFER_ALL
    .tx_need_commit = 0,
    #endif
};



/*
 * @brief initializes state for a new tx
 * @comments Needs to take effect AFTER task prologue
 */
void tx_begin() {
    if(((tx_state *)(curctx->extra_state))->in_tx == 0) {
      TX_TIMER_START
    #ifdef LIBCOATIGCC_BUFFER_ALL
      LCG_PRINTF("Zeroing num_dtxv!!!\r\n");
    #endif
      ((tx_state *)(curctx->extra_state))->in_tx = 1;
    }
    #ifdef LIBCOATIGCC_BUFFER_ALL
    cur_tx_start = curctx->task;
    ((tx_state *)(curctx->extra_state))->tx_need_commit = 0;
    #endif
    LCG_PRINTF("In tx begin!!\r\n");
}

// We only call any of these functions if full buffering is in effect
#ifdef LIBCOATIGCC_BUFFER_ALL
/*
 * @brief function to do second phase of commit from task inside tx to tx buffer
 * from tsk buffer
 * @notes: based on persistent var: num_dtv
 */
void tsk_in_tx_commit_ph2() {
  uint16_t i = 0;
  LCG_PRINTF("tsk_in_tx commit phase2\r\n");
  // Cycle through all the variables to commit and write back to tx_buff
  while(tsk_table.active_bins > 0)  {
    uint16_t bin = tsk_table.active_bins - 1;
    uint16_t slot;
    // Walk through each slot in each bin w/ at least one value slotted in
    while(tsk_table.bucket_len[bin] > 0) {
      slot = tsk_table.bucket_len[bin] - 1;
      uint16_t flag = 0;
      // Add this to the tx table
      flag = add_to_table(&tx_table, tx_buf, &tx_buf_level,
                          tsk_table.src[bin][slot], tsk_table.dst[bin][slot], 
                          tsk_table.size[bin][slot]);
      LCG_PRINTF("Inserted %x to %x val = %x\r\n",
                                *((uint16_t *)tsk_table.dst[bin][slot]),
                                (uint16_t)tsk_table.src[bin][slot],
                                *((uint16_t *)tsk_table.src[bin][slot]));
      if(flag) {
        printf("Error allocing tx buf\r\n");
        while(1);
      }
      // Decrement number of items in bin (don't worry about adding to tx bin
      // and then decrementing tsk bin, if we fail before dec, we'll just redo
      // the addition to the tx bin.
      tsk_table.bucket_len[bin]--;
    }
    // Decrement number of bins left to check
    tsk_table.active_bins--;
  }
  tsk_buf_level = 0;
}


/*  
 * @brief write back to source on transaction commit
 * @notes uhh, this sucker runs regardless... this could be bad
 */
void tx_commit_ph2() {
  LCG_PRINTF("Running tx_commit_ph2\r\n");
  // Copy all commit list entries
  while(tx_table.active_bins > 0)  {
    uint16_t bin = tx_table.active_bins - 1;
    uint16_t slot;
    // Walk through each slot in each bin w/ at least one value slotted in
    while(tx_table.bucket_len[bin] > 0) {
      slot = tx_table.bucket_len[bin] - 1;
      // Copy from dst in tsk buf to "home" for that variable
      memcpy( tx_table.src[bin][slot],
              tx_table.dst[bin][slot],
              tx_table.size[bin][slot]
            );
      LCG_PRINTF("%u.%u ",bin,slot);
      LCG_PRINTF("Inserted %x to %x val %u => %u size %u\r\n",
                                ((uint16_t *)tx_table.dst[bin][slot]),
                                (uint16_t)tx_table.src[bin][slot],
                                *((uint16_t *)tx_table.dst[bin][slot]),
                                *((uint16_t *)tx_table.src[bin][slot]),
                                ((uint16_t *)tx_table.size[bin][slot]));
      // Decrement number of items in bin
      tx_table.bucket_len[bin]--;
    }
    // Decrement number of bins left to check
    tx_table.active_bins--;
  }
  tx_buf_level = 0;
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
  uint8_t conflict = 0;
  // Default setting
  #ifndef LIBCOATIGCC_SER_TX_AFTER
    if(((ev_state *)curctx->extra_ev_state)->ev_need_commit) {
      uint16_t ev_len = ((ev_state*)curctx->extra_ev_state)->num_read;
      conflict = compare_list_to_hash(&tx_write_table, ev_read_list, ev_len);
      if(conflict) {
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
  #else
    if(((ev_state *)curctx->extra_ev_state)->ev_need_commit) {
      uint16_t ev_len = ((ev_state*)curctx->extra_ev_state)->num_write;
      conflict = compare_list_to_hash(&tx_read_table, ev_write_list, ev_len);
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
  #endif // SER_TX_AFTER
  return;
}

#endif // BUFFER_ALL

