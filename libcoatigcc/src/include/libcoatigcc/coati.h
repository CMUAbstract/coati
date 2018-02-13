#ifndef COATI_H
#define COATI_H

#include <stddef.h>
#include <stdint.h>

#include <libmsp/mem.h>
#include "types.h"
#include "repeat.h"

#define TASK_NAME_SIZE 32

#ifndef LIBCOATIGCC_PER_TSK_BUF_SIZE
  #define NUM_DIRTY_ENTRIES 16
#else
  #define NUM_DIRTY_ENTRIES LIBCOATIGCC_PER_TSK_BUF_SIZE
#endif

#ifndef LIBCOATIGCC_CTX_BUF_SIZE
  #define BUF_SIZE 32
#else
  #define BUF_SIZE LIBCOATIGCC_CTX_BUF_SIZE
#endif

typedef void (task_func_t)(void);
typedef uint32_t task_mask_t;
typedef uint16_t field_mask_t;
typedef unsigned task_idx_t;

extern void *task_dirty_buf_src[];
extern void *task_dirty_buf_dst[];
extern size_t task_dirty_buf_size[];
extern uint8_t task_dirty_buf[];


extern uint16_t volatile num_tbe;
extern uint16_t num_dtv;

typedef struct {
    task_func_t *func;
    task_idx_t idx;
    char name[TASK_NAME_SIZE];
} task_t;


/** @brief Execution context */
typedef struct _context_t {
    /** @brief Pointer to the most recently started but not finished task */
    task_t *task;
    /** @brief Pointer to the extra state we need to swap on context switch */
    void *extra_state;
    /** @brief Another one! We need this one to handle events */
    void *extra_ev_state;

} context_t;

extern context_t * volatile curctx;
//extern context_t * volatile context_ptr0; 
//extern context_t * volatile context_ptr1;
extern context_t * volatile context_ptr0; 
extern context_t * volatile context_ptr1;

/** @brief Internal macro for constructing name of task symbol */
#define TASK_SYM_NAME(func) _task_ ## func

/** @brief Declare a task
 *
 *  @param idx      Global task index, zero-based
 *  @param func     Pointer to task function
 *
 *   TODO: These do not need to be stored in memory, could be resolved
 *         into literal values and encoded directly in the code instructions.
 *         But, it's not obvious how to implement that with macros (would
 *         need "define inside a define"), so for now create symbols.
 *         The compiler should actually optimize these away.
 *
 *   TODO: Consider creating a table in form of struct and store
 *         for each task: mask, index, name. That way we can
 *         have access to task name for diagnostic output.
 */
#define TASK(idx, func) \
    void func(); \
    __nv task_t TASK_SYM_NAME(func) = { func, idx, #func }; \

#define TASK_REF(func) &TASK_SYM_NAME(func)

/** @brief Function called on every reboot
 *  @details This function usually initializes hardware, such as GPIO
 *           direction. The application must define this function.
 */
extern void init();

/** @brief First task to run when the application starts
 *  @details Symbol is defined by the ENTRY_TASK macro.
 *           This is not wrapped into a delaration macro, because applications
 *           are not meant to declare tasks -- internal only.
 *
 *  TODO: An alternative would be to have a macro that defines
 *        the curtask symbol and initializes it to the entry task. The
 *        application would be required to have a definition using that macro.
 *        An advantage is that the names of the tasks in the application are
 *        not constrained, and the whole thing is less magical when reading app
 *        code, but slightly more verbose.
 */
extern task_t TASK_SYM_NAME(_entry_task);

/** @brief Declare the first task of the application
 *  @details This macro defines a function with a special name that is
 *           used to initialize the current task pointer.
 *
 *           This does incur the penalty of an extra task transition, but it
 *           happens only once in application lifetime.
 *
 *           The alternatives are to force the user to define functions
 *           with a special name or to define a task pointer symbol outside
 *           of the library.
 */
#define ENTRY_TASK(task) \
    TASK(0, _entry_task) \
    void _entry_task() { TRANSITION_TO(task); }

/** @brief Init function prototype
 *  @details We rely on the special name of this symbol to initialize the
 *           current task pointer. The entry function is defined in the user
 *           application through a macro provided by our header.
 */
void _init();

/** @brief Declare the function to be called on each boot
 *  @details The same notes apply as for entry task.
 */
#define INIT_FUNC(func) void _init() { func(); }

void task_prologue();
void transition_to(task_t *task);


/** @brief Transfer control to the given task
 *  @param task     Name of the task function
 *  */
#define TRANSITION_TO(task) transition_to(TASK_REF(task))

void * read(const void * addr, unsigned size, acc_type acc);
void  write(const void *addr, unsigned size, acc_type acc, uint32_t value);
int16_t find(const void *addr);
void *internal_memcpy(void *dest, void *src, uint16_t num);

/**
 *  @brief returns the value of x after finding it in dirty buf
 */
#define READ(x,type) \
    *((type *)read(&(x),sizeof(type),NORMAL))

/**
 * @brief writes a value to x based on the size of the variable
 */
#define WRITE(x,val,type,is_ptr) \
    { if(is_ptr){ \
          write(&(x),sizeof(type),NORMAL,val);\
      }\
      else { \
          type _temp_loc = val;\
          write(&(x),sizeof(type),NORMAL,_temp_loc);\
      } \
    }

#endif // COATI_H
