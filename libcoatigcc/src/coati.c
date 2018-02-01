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

__nv void * task_commit_list_src[NUM_DIRTY_ENTRIES];
__nv void * task_commit_list_dst[NUM_DIRTY_ENTRIES];
__nv size_t task_commit_list_size[NUM_DIRTY_ENTRIES];

// volatile number of task buffer entries that we want to clear on reboot
volatile uint16_t num_tbe = 0;

// nv count of the number of dirty global variables to be committed
__nv uint16_t num_dtv = 0;

// Bundle of functions internal to the library
static void commit_ph2();
static void commit_ph1();
static void * task_dirty_buf_alloc(void *, size_t);

/**
 * @brief Function that copies data to dirty list
 */
void commit_ph1() {
    if(!num_tbe)
        return;

    for(int i = 0; i < num_tbe; i++) {
        task_commit_list_src[i] = task_dirty_buf_src[i];
        task_commit_list_dst[i] = task_dirty_buf_dst[i];
        task_commit_list_size[i] = task_dirty_buf_size[i];
    }

    num_dtv = num_tbe;
}

/**
 * @brief Function that copies data to main memory from the dirty list
 */
void commit_ph2() {
    // Copy all commit list entries
    while(num_dtv > 0)  {
      // Copy from dst in tsk buf to "home" for that variable
      memcpy( task_commit_list_src[num_dtv - 1],
              task_commit_list_dst[num_dtv - 1],
              task_commit_list_size[num_dtv - 1]
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
 * // TODO confirm that the index is at the byte and not the word level
 */
void * task_dirty_buf_alloc(void * addr, size_t size) {
    uint16_t new_ptr;
    if(num_tbe) {
        new_ptr = (uint16_t) task_dirty_buf_dst[num_tbe - 1] +
        task_dirty_buf_size[num_tbe - 1];
        // Fix alignment struggles
        if(size == 2) {
          while(new_ptr & 0x1)
            new_ptr++;
        }
        if(size == 2) {
          while(new_ptr & 0x11)
            new_ptr++;
        }
    }
    else {
        new_ptr = (uint16_t) task_dirty_buf;
    }
    LCG_PRINTF("Writing to %x, buf = %x, num_tbe = %x \r\n", new_ptr,
        task_dirty_buf, num_tbe);
    if(new_ptr + size > (unsigned) (task_dirty_buf + BUF_SIZE)) {
        LCG_PRINTF("Returning null! %x > %x \r\n",
            new_ptr + size,(unsigned) (task_dirty_buf + BUF_SIZE));
        return NULL;
    }
    else {
        num_tbe++;
        task_dirty_buf_src[num_tbe - 1] = addr;
        task_dirty_buf_dst[num_tbe - 1] = (void *) new_ptr;
        task_dirty_buf_size[num_tbe - 1] = size;
    }
    return (void *) new_ptr;
}

/*
 * @brief Returns a pointer to the value stored in the buffer a the src address
 * provided or the value in main memory
 */
void * read(const void *addr, unsigned size, acc_type acc) {
    int index;
    void * dst;
    index = find(addr);
    switch(acc) {
        case EVENT:
            //TODO figure out if we can get rid of the extra search on each
            //event access
            // Not in tsk buf, so check event buf
            index = evfind(addr);
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
                add_to_filter(read_filters + EV,(unsigned)addr);
                dst = addr;
            }
            break;
        case NORMAL:
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
            // check tsk buf
            if(index > -1) {
                dst = task_dirty_buf_dst[index];
            }
            else {
                // Not in tsk buf, so check tx buf
                index = tfind(addr);
                if(index > -1) {
                    dst = tx_dirty_dst[index];
                }
                // Not in tx buf either, so add to filter and return main memory addr
                else {
                    add_to_filter(read_filters + THREAD,(unsigned)addr);
                    dst = addr;
                }
            }
            break;
        default:
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
    //LCG_PRINTF("value incoming = %i type = %i \r\n", value, acc);
    index = find(addr);
    switch(acc) {
        case EVENT:
            add_to_filter(write_filters + EV, (unsigned) addr);
            LCG_PRINTF("Running event write!\r\n");
            index = evfind(addr);
            if(index > -1) {
              if (size == sizeof(uint8_t)) {
                *((uint8_t *) ev_dirty_dst[index]) = (uint8_t) value;
              } else if(size == sizeof(uint16_t)) {
                *((uint16_t *) (ev_dirty_dst[index] + 1) ) = (uint16_t) value;
              } else if(size == sizeof(uint32_t)) {
                *((uint32_t *) (ev_dirty_dst[index] + 3)) = (uint32_t) value;
              } else {
                  LCG_PRINTF("Ev Error! invalid size!\r\n");
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
                    LCG_PRINTF("Ev Error! invalid size!\r\n");
                    while(1);
                  }
                } else {
                    // Error! we ran out of space
                    LCG_PRINTF("Ev Error! out of space!\r\n");
                    while(1);
                }
            }
            break;
        case TX:
            add_to_filter(write_filters + THREAD, (unsigned)addr);
            // Add to TX filter?
        case NORMAL:
            if(index > -1) {
              if (size == sizeof(char)) {
                *((uint8_t *) task_dirty_buf_dst[index]) = (uint8_t) value;
              } else if (size == sizeof(uint16_t)) {
                *((unsigned *) task_dirty_buf_dst[index]) = (uint16_t) value;
              } else if (size == sizeof(uint32_t)) {
                *((uint32_t *) task_dirty_buf_dst[index]) = (uint32_t) value;
              } else {
                    LCG_PRINTF("Ev Error! invalid size!\r\n");
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
                    LCG_PRINTF("Ev Error! invalid size!\r\n");
                    while(1);
                  }
                }
                else {
                    // Error! we ran out of space
                    LCG_PRINTF("Error! out of space!\r\n");
                    while(1);
                }
            }
            break;
        default:
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
    commit_ph2();
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

    context_t *next_ctx; // this should be in a register for efficiency
                         // (if we really care, write this func in asm)
    tx_state *new_tx_state;
    ev_state *new_ev_state;
    tx_state *cur_tx_state =(tx_state *)(curctx->extra_state);
    ev_state *cur_ev_state =(ev_state *)(curctx->extra_ev_state);
    // Copy all of the variables into the dirty buffer
    // update the extra state objects
    // update current task pointer
    // reset stack pointer
    // jump to next task

    new_tx_state = (curctx->extra_state == &state_0 ? &state_1 : &state_0);
    new_ev_state = (curctx->extra_ev_state == &state_ev_0 ? &state_ev_1 :
                    &state_ev_0);
    LCG_PRINTF("Transition Checking if in tx: result = %i, %i \r\n",cur_tx_state->in_tx,
           ((tx_state *)curctx->extra_state)->in_tx);

    if(cur_tx_state->in_tx) {
        LCG_PRINTF("Running tcommit phase 1\r\n");
        tcommit_ph1();
        new_tx_state->num_dtxv = cur_tx_state->num_dtxv + num_tbe;
    }
    else {
        commit_ph1();
    }

    new_ev_state->in_ev = cur_ev_state->in_ev;
    new_ev_state->ev_need_commit = need_ev_commit;

    new_tx_state->in_tx = cur_tx_state->in_tx;
    new_tx_state->tx_need_commit = need_tx_commit;


    next_ctx = (curctx == &context_0 ? &context_1 : &context_0);
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

    // TODO: using the raw transtion would be possible once the
    //       prologue discussed in chain.h is implemented (requires compiler
    //       support)
    // transition_to(curtask);
    LCG_PRINTF("transitioning to %x \r\n",curctx->task->func);
    LCG_PRINTF("tsk size = %i tx size = %i \r\n", NUM_DIRTY_ENTRIES, BUF_SIZE);

    task_prologue();

    // check for running event, disable all event interrupts if we're in one
    if(((ev_state *)curctx->extra_ev_state)->in_ev){
      _disable_events();
    }
    else {
      LCG_PRINTF("Main: Enabling events!\r\n");
      _enable_events();
    }

    __asm__ volatile ( // volatile because output operands unused by C
        "br %[nt]\n"
        : /* no outputs */
        : [nt] "r" (curctx->task->func)
    );

    return 0; // TODO: write our own entry point and get rid of this
}
