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
__nv void * task_dirty_buf_src[NUM_DIRTY_ENTRIES];
__nv void * task_dirty_buf_dst[NUM_DIRTY_ENTRIES];
__nv size_t task_dirty_buf_size[NUM_DIRTY_ENTRIES];
__nv uint8_t task_dirty_buf[BUF_SIZE];


// volatile number of task buffer entries that we want to clear on reboot
volatile uint16_t num_tbe = 0;

// nv count of the number of dirty global variables to be committed
__nv uint16_t num_dtv = 0;

// Bundle of functions internal to the library
static void commit_ph2();
static void commit_ph1();
static void * task_dirty_buf_alloc(void *, size_t);

/**
 * @brief Function that updates number of dirty variables in the persistent
 * state
 */
void commit_ph1() {
    LCG_PRINTF("new dtv = %u\r\n",num_tbe);
    num_dtv = num_tbe;
}

/**
 * @brief Function that copies data to main memory from the task buffers
 * @notes based on persistent var num_dtv
 */
void commit_ph2() {
    // Copy all commit list entries
    while(num_dtv > 0)  {
      // Copy from dst in tsk buf to "home" for that variable
      LCG_PRINTF("Copying to %x\r\n",task_dirty_buf_src[num_dtv-1]);
      memcpy( task_dirty_buf_src[num_dtv - 1],
              task_dirty_buf_dst[num_dtv - 1],
              task_dirty_buf_size[num_dtv - 1]
            );
      num_dtv--;
    }
}


/*
 * @brief returns the index into the buffers of where the data is located
 */
int16_t  find(const void * addr) {
    if(num_tbe) {
      for(int i = 0; i < num_tbe; i++) {
          if(addr == task_dirty_buf_src[i])
              return i;
      }
    }
    return -1;
}

/*
 * @brief allocs space in buffer and returns a pointer to the spot, returns NULL
 * if buf is out of space
 */
void * task_dirty_buf_alloc(void * addr, size_t size) {
    uint16_t new_ptr;
    // Totally valid to use volatile counter here on task level
    if(num_tbe) {
        // Set new pointer based on last pointer and the length of the var it
        // stores
        new_ptr = (uint16_t) task_dirty_buf_dst[num_tbe - 1] +
        task_dirty_buf_size[num_tbe - 1];
        // Fix alignment struggles
    }
    else {
        // TODO figure out if the pointers ever need to be shifted, i.e. for a
        // 32 bit write at the start
        new_ptr = (uint16_t) task_dirty_buf;
    }
    if(size == 2) {
      while(new_ptr & 0x1)
        new_ptr++;
    }
    if(size == 4) {
      LCG_PRINTF("allocing 32 bit!\r\n");
      while(new_ptr & 0x11)
        new_ptr++;
    }
    if(new_ptr + size > (unsigned) (task_dirty_buf + BUF_SIZE)) {
        LCG_PRINTF("Returning null! %x > %x \r\n",
            new_ptr + size,(unsigned) (task_dirty_buf + BUF_SIZE));
        return NULL;
    }
    else {
        // Used to indicate how many volatile variables we've been able to store
        num_tbe++;
        task_dirty_buf_src[num_tbe - 1] = addr;
        task_dirty_buf_dst[num_tbe - 1] = (void *) new_ptr;
        task_dirty_buf_size[num_tbe - 1] = size;
    }
    LCG_PRINTF("Writing to %x, from = %x, num_tbe = %x \r\n", new_ptr,
        task_dirty_buf_src[num_tbe -1], num_tbe);
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
            //TODO figure out if we can get rid of the extra search on each
            //event access
            //printf("EV read: %x\r\n",addr);
            // Not in tsk buf, so check event buf
            index = evfind(addr);
            read_cnt = ((ev_state *)curctx->extra_ev_state)->num_read;
            read_cnt += num_evread;
            if(read_cnt >= NUM_DIRTY_ENTRIES) {
              printf("Out of space in ev read list!\r\n");
              while(1);
            }
            ev_read_list[read_cnt] = addr;
            num_evread++;
            LCG_PRINTF("ev index = %u \r\n", index);
            if(index > -1) {
               dst = ev_dirty_dst[index];
              LCG_PRINTF("rd: index = %u, Found, Buffer vals: ", index);
              for(int16_t i = 0; i < 8; i++) {
                LCG_PRINTF("%x: ", ev_dirty_buf[i]);
              }
              LCG_PRINTF("\r\n");
            }
            // Not in tx buf either, so add to filter and return main memory addr
            else {
                //add_to_filter(read_filters + EV,(unsigned)addr);
                dst = addr;
            }
            break;
        case NORMAL:
            index = find(addr);
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
            index = find(addr);
            read_cnt = ((tx_state *)curctx->extra_state)->num_read;
            read_cnt += num_txread;
            if(read_cnt >= NUM_DIRTY_ENTRIES) {
              printf("Out of space in tx read list!\r\n");
              while(1);
            }
            tx_read_list[read_cnt] = addr;
            num_txread++;
            // check tsk buf
            if(index > -1) {
                dst = task_dirty_buf_dst[index];
            }
            else {
                // Not in tsk buf, so check tx buf
                LCG_PRINTF("Checking tx buf\r\n");
                index = tfind(addr);
                if(index > -1) {
                    dst = tx_dirty_dst[index];
                }
                // Not in tx buf either, so add to filter and return main memory addr
                else {
                    // TODO make this a function
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
    index = find(addr);
    if(index > -1) {
        *((uint8_t *)(task_dirty_buf_dst + index)) = value;
    }
    else {
        void * dst = task_dirty_buf_alloc(addr, 1);
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
    index = find(addr);
    if(index > -1) {
        *((uint16_t *)(task_dirty_buf_dst + index)) = value;
    }
    else {
        void * dst = task_dirty_buf_alloc(addr, 2);
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
            //add_to_filter(write_filters + EV, (unsigned) addr);
            LCG_PRINTF("Running event write!\r\n");
            index = evfind(addr);
            write_cnt = ((ev_state *)curctx->extra_ev_state)->num_read;
            write_cnt += num_evread;
            if(write_cnt >= NUM_DIRTY_ENTRIES) {
              printf("Out of space in write list!\r\n");
              while(1);
            }
            ev_write_list[write_cnt] = addr;
            num_evwrite++;
            if(index > -1) {
              if (size == sizeof(uint8_t)) {
                *((uint8_t *) ev_dirty_dst[index]) = (uint8_t) value;
              } else if(size == sizeof(uint16_t)) {
                *((uint16_t *) (ev_dirty_dst[index]) ) = (uint16_t) value;
              } else if(size == sizeof(uint32_t)) {
                *((uint32_t *) (ev_dirty_dst[index])) = (uint32_t) value;
              } else {
                  printf("Ev Error! invalid size!\r\n");
                  while(1);
              }
              LCG_PRINTF("index = %u, Found, Buffer vals: ", index);
              for(int16_t i = 0; i < 8; i++) {
                LCG_PRINTF("%x: ", ev_dirty_buf[i]);
              }
              LCG_PRINTF("\r\n");
            }
            else {
                void * dst = ev_dirty_buf_alloc(addr, size);
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
            index = find(addr);
            if(index > -1) {
              if (size == sizeof(char)) {
                *((uint8_t *) task_dirty_buf_dst[index]) = (uint8_t) value;
              } else if (size == sizeof(uint16_t)) {
                *((unsigned *) task_dirty_buf_dst[index]) = (uint16_t) value;
              } else if (size == sizeof(uint32_t)) {
                *((uint32_t *) task_dirty_buf_dst[index]) = (uint32_t) value;
              } else {
                    printf("Error! invalid size!\r\n");
                    while(1);
              }
            }
            else {
                void * dst = task_dirty_buf_alloc(addr, size);
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


/**
 * @brief Function to be invoked before transferring control to a new task
 * @comments tricky change: tx commit can now manipulate curctx->task to get
 * back to the start of a transaction if it's rolling back, so yeah, take that
 * into consideration
 */
void task_prologue()
{
    LCG_PRINTF("Prologue: Checking if in tx: result = %i \r\n",
           ((tx_state *)curctx->extra_state)->in_tx);
    
    // check if we're committing a task inside a transaction
    if(((tx_state *)curctx->extra_state)->in_tx) {
      LCG_PRINTF("Running tx inner commit\r\n");
      tx_inner_commit_ph2();
    }
    else {
      LCG_PRINTF("Running normal commit\r\n");
      commit_ph2();
    }
    // Now check if there's a commit here
    if(((tx_state *)curctx->extra_state)->tx_need_commit) {
        LCG_PRINTF("Running tx commit!\r\n");
        tx_commit();
        
    }
    // Clear all task buf entries before starting new task
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
    LCG_PRINTF("Disabled events");
     
    tx_state *cur_tx_state =(tx_state *)(curctx->extra_state);
    ev_state *cur_ev_state =(ev_state *)(curctx->extra_ev_state);

    context_t *next_ctx; // this should be in a register for efficiency
                         // (if we really care, write this func in asm)
    next_ctx = (curctx == &context_0 ? &context_1 : &context_0);
    
    tx_state *new_tx_state;
    new_tx_state = (curctx->extra_state == &state_0 ? &state_1 : &state_0);
    
    ev_state *new_ev_state;
    new_ev_state = (curctx->extra_ev_state == &state_ev_0 ? &state_ev_1 :
                    &state_ev_0);    

    
    LCG_PRINTF("Transition Checking if in tx: result = %i, %i \r\n",cur_tx_state->in_tx,
           ((tx_state *)curctx->extra_state)->in_tx);

    if(cur_tx_state->in_tx) {
        LCG_PRINTF("Running tcommit phase 1\r\n");
        tx_commit_ph1(new_tx_state);
    }
    else {
        LCG_PRINTF("Running normal commit phase 1\r\n");
        commit_ph1();
    }

    new_ev_state->in_ev = cur_ev_state->in_ev;
    new_ev_state->ev_need_commit = cur_ev_state->ev_need_commit;

    new_tx_state->in_tx = cur_tx_state->in_tx;
    new_tx_state->tx_need_commit = need_tx_commit;

    next_ctx->extra_state = new_tx_state;
    next_ctx->extra_ev_state = new_ev_state;
    next_ctx->task = next_task;
    curctx = next_ctx;

    task_prologue();
    // Re-enable events if we're staying in the threads context, but leave them
    // disabled if we're going into an event task
    if(((ev_state *)curctx->extra_state)->in_ev == 0){
      LCG_PRINTF("TT: Enabling events\r\n");
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

    task_prologue();

    // check for running event, disable all event interrupts
    if(((ev_state *)curctx->extra_ev_state)->in_ev){
      _disable_events();
      // if we've latched the need_commit flag, that means we failed somewhere
      // in ev_return, so finish ev_return
      // Note, this function will transfer control to the threadctx->task
      if(((ev_state *)curctx->extra_ev_state)->ev_need_commit == LOCAL_COMMIT) {
        event_return();
      }
    }
    else {
     // LCG_PRINTF("Main: Enabling events!\r\n");
      _enable_events();
    }

    __asm__ volatile ( // volatile because output operands unused by C
        "br %[nt]\n"
        : /* no outputs */
        : [nt] "r" (curctx->task->func)
    );

    return 0; // TODO: write our own entry point and get rid of this
}
