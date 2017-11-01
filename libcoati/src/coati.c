#include <stdarg.h>
#include <string.h>

#ifndef LIBCHAIN_ENABLE_DIAGNOSTICS
#define LIBCHAIN_PRINTF(...)
#else
#include <stdio.h>
#define LIBCHAIN_PRINTF printf
#endif

#include "coati.h"

/* Dummy types for offset calculations */
struct _void_type_t {
    void * x;
};
typedef struct _void_type_t void_type_t;

/* To update the context, fill-in the unused one and flip the pointer to it */
__nv context_t context_1 = {0};
__nv context_t context_0 = {
    .task = TASK_REF(_entry_task),
};

__nv context_t * volatile curctx = &context_0;

// for internal instrumentation purposes
__nv volatile unsigned _numBoots = 0;

// task dirty buffer data
void * task_dirty_buf_src[NUM_DIRTY_ENTRIES];
void * task_dirty_buf_dst[NUM_DIRTY_ENTRIES];
size_t task_dirty_buf_size[NUM_DIRTY_ENTRIES];
//void volatile * task_dirty_buf_src[NUM_DIRTY_ENTRIES];
//void  volatile * task_dirty_buf_dst[NUM_DIRTY_ENTRIES];
//size_t volatile task_dirty_buf_size[NUM_DIRTY_ENTRIES];
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
static int16_t find(void *);
static void * task_dirty_buf_alloc(void *, size_t);

/**
 * @brief Function that copies data to dirty list
 */
void commit_ph1() {
    //printf("In commit_ph1 %i !\r\n",num_tbe);
    if(!num_tbe)
        return;
    //printf("here!\r\n");

    for(int i = 0; i < num_tbe; i++) {
        /*
        printf("old, new: dst: %x %x\r\n", //dst %x %x, size %x %x\r\n",
                //task_commit_list_src[i], task_dirty_buf_src[i]//,
                task_commit_list_dst[i], task_dirty_buf_dst[i]//,
                //task_commit_list_size[i], task_dirty_buf_size[i]
                );
        */
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
    //printf("Num_dtv = %i \r\n",num_dtv);
    while(num_dtv > 0)  {
      /*
      printf("Copying from %x to %x \r\n",((uint16_t *)task_commit_list_dst[num_dtv -1]), 
            (uint16_t) task_commit_list_src[num_dtv - 1]);
      */
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
        printf("%x + %i > %x + %i \r\n",new_ptr,size,task_dirty_buf,32);
        return NULL;
    }
    else {
        num_tbe++;
        task_dirty_buf_src[num_tbe - 1] = addr;
        task_dirty_buf_dst[num_tbe - 1] = new_ptr;
        task_dirty_buf_size[num_tbe - 1] = size;
        printf("src: %x dst: %x size: %x \r\n",addr,new_ptr,size);
    }
    return (void *) new_ptr;
}

/*
 * @brief Returns a pointer to the value stored in the buffer a the src address
 * provided or the value in main memory
 */

void * read(void * addr) {
    int index;
    index = find(addr);
    if(index > -1) {
        return task_dirty_buf_dst[index];
    }
    else {
        return addr;
    }
}

/*
 * @brief writes data from value pointer to address' location in task buf,
 * returns 0 if successful, -1 if allocation failed
 */
int16_t write(void *addr, void * value, size_t size) {
    int index;
    index = find(addr);
    if(index > -1) {
        memcpy(task_dirty_buf_dst[index], value, size);
    }
    else {
        void * dst = task_dirty_buf_alloc(addr, size);
        if(dst) {
            memcpy(dst, value, size);
        }
        else {
            // Error! we ran out of space
            return -1;
        }
    }
    return 0;
}



/**
 * @brief Function to be invoked at the beginning of every task
 */
void task_prologue()
{
    commit_ph2();

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
    context_t *next_ctx; // this should be in a register for efficiency
                         // (if we really care, write this func in asm)

    // Copy all of the variables into the dirty buffer
    // update current task pointer
    // reset stack pointer
    // jump to next task

    commit_ph1();

    next_ctx = (curctx == &context_0 ? &context_1 : &context_0 );
    next_ctx->task = next_task;

    curctx = next_ctx;

    task_prologue();

    __asm__ volatile ( // volatile because output operands unused by C
        "mov #0x2400, r1\n"
        "br %[ntask]\n"
        :
        : [ntask] "r" (next_task->func)
    );

}


/** @brief Entry point upon reboot */
int main() {
    _init();

    _numBoots++;

    // Resume execution at the last task that started but did not finish

    // TODO: using the raw transtion would be possible once the
    //       prologue discussed in chain.h is implemented (requires compiler
    //       support)
    // transition_to(curtask);

    task_prologue();

    __asm__ volatile ( // volatile because output operands unused by C
        "br %[nt]\n"
        : /* no outputs */
        : [nt] "r" (curctx->task->func)
    );

    return 0; // TODO: write our own entry point and get rid of this
}
