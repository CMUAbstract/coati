#include <stdarg.h>
#include <string.h>

#ifndef LIBCHAIN_ENABLE_DIAGNOSTICS
#define LIBCHAIN_PRINTF(...)
#else
#include <stdio.h>
#define LIBCHAIN_PRINTF printf
#endif

#include "coati.h"
#include "tx.h"
#include "event.h"
#include "types.h"

/* To update the context, fill-in the unused one and flip the pointer to it */
__nv context_t context_1 = {0};
__nv context_t context_0 = {
    .task = TASK_REF(_entry_task),
    .extra_state = &state_0
};

__nv context_t * volatile curctx = &context_0;

// for internal instrumentation purposes
__nv volatile unsigned _numBoots = 0;

// task dirty buffer data
void * task_dirty_buf_src[NUM_DIRTY_ENTRIES];
void * task_dirty_buf_dst[NUM_DIRTY_ENTRIES];
size_t task_dirty_buf_size[NUM_DIRTY_ENTRIES];
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
int16_t  find(void * addr) {
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
    }
    else {
        new_ptr = task_dirty_buf;
    }
    if(new_ptr + size > task_dirty_buf + BUF_SIZE) {
        return NULL;
    }
    else {
        num_tbe++;
        task_dirty_buf_src[num_tbe - 1] = addr;
        task_dirty_buf_dst[num_tbe - 1] = new_ptr;
        task_dirty_buf_size[num_tbe - 1] = size;
    }
    return (void *) new_ptr;
}

/*
 * @brief Returns a pointer to the value stored in the buffer a the src address
 * provided or the value in main memory
 */
void * read(void * addr, unsigned size, acc_type acc) {
    int index;
    void * dst;
    index = find(addr);
    switch(acc) {
        case EVENT:
            // Add to filter?
        case NORMAL:
            if(index > -1) {
                dst = task_dirty_buf_dst[index];
            }
            else {
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
                    add_to_filter(filters + THREAD,addr);
                    dst = addr;
                }
            }
            break;
        default:
            // Error!
            while(1);
    }
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
 * @comments DEPRACATED
 */

void write(void *addr, unsigned size, acc_type acc, unsigned value) {
    int index;
    index = find(addr);
    switch(acc) {
        case EVENT:
            add_to_filter(filters + EV, addr);
        case TX:
            // Add to TX filter?
        case NORMAL:
            if(index > -1) {
              if (size == sizeof(char)) {
                *((uint8_t *) task_dirty_buf_dst[index]) = (uint8_t) value;
              } else {
                *((unsigned *) task_dirty_buf_dst[index]) = value;
              }
            }
            else {
                void * dst = task_dirty_buf_alloc(addr, size);
                if(dst) {
                  if (size == sizeof(char)) {
                    *((uint8_t *) dst) = (uint8_t) value;
                  } else {
                    *((unsigned *) dst) = value;
                  }
                }
                else {
                    // Error! we ran out of space
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



/**
 * @brief Function to be invoked at the beginning of every task
 */
void task_prologue()
{
    commit_ph2();
    // Now check if there's a commit here
    if(((tx_state *)curctx->extra_state)->tx_need_commit) {

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

    context_t *next_ctx; // this should be in a register for efficiency
                         // (if we really care, write this func in asm)
    tx_state *new_tx_state;
    tx_state *cur_tx_state =(tx_state *)(curctx->extra_state);
    // Copy all of the variables into the dirty buffer
    // update the extra state objects
    // update current task pointer
    // reset stack pointer
    // jump to next task

    if(cur_tx_state->in_tx) {
        tcommit_ph1();
    }
    else {
        commit_ph1();
    }

    new_tx_state = (curctx->extra_state == &state_0 ? &state_1 : &state_0);

    new_tx_state->num_dtxv = cur_tx_state->num_dtxv + num_tbe;
    new_tx_state->in_tx = cur_tx_state->in_tx;
    new_tx_state->tx_need_commit = need_tx_commit;

    next_ctx = (curctx == &context_0 ? &context_1 : &context_0);
    next_ctx->extra_state = new_tx_state;
    next_ctx->task = next_task;
    curctx = next_ctx;

    task_prologue();
    // Re-enable events if we're staying in the threads context, but leave them
    // disabled if we're going into an event task
    if(!((uint16_t)(curctx->task->func) & 0x1)){
      _enable_events();
    }

    __asm__ volatile ( // volatile because output operands unused by C
        "mov #0x2400, r1\n"
        "br %[ntask]\n"
        :
        : [ntask] "r" (next_task->func)
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

    task_prologue();

    // check for running event, disable all event interrupts if we're in one
    if((uint16_t)(curctx->task->func) & 0x1){
      _disable_events();
    }
    else {
      _enable_events();
    }

    __asm__ volatile ( // volatile because output operands unused by C
        "br %[nt]\n"
        : /* no outputs */
        : [nt] "r" (curctx->task->func)
    );

    return 0; // TODO: write our own entry point and get rid of this
}
