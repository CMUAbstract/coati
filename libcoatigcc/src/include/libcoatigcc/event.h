#ifndef _EVENT_H
#define _EVENT_H
#include <libmsp/mem.h>
#include "filter.h"
#include "coati.h"

#define PRINT(x) (#x)

#define THREAD 0
#define EV  1
#define NUM_PRIO_LEVELS 2

typedef struct _ev_state {
    uint16_t num_devv;
    uint16_t num_read;
    uint16_t num_write;
    uint8_t in_ev;
    uint8_t ev_need_commit;
} ev_state;

extern volatile uint16_t num_evbe;
extern volatile uint16_t num_evread;

extern ev_state state_ev_1;
extern ev_state state_ev_0;

extern context_t *thread_ctx;
extern task_t * cur_tx_start;
extern void * ev_read_list[];
extern void * ev_write_list[];

void event_handler();
void ev_commit_ph2();

int16_t  ev_find(const void * addr);
void *  ev_get_dst(void * addr);
void * ev_buf_alloc(void * addr, size_t size);

extern volatile uint16_t num_evbe;
extern volatile uint16_t num_evread;
extern volatile uint16_t num_evwrite;

extern __nv uint8_t ev_buf[BUF_SIZE];
extern __nv void * ev_src[];
extern __nv void * ev_dst[];
extern __nv size_t ev_size[];

#define EVENT_ENABLE_FUNC(func) void _enable_events() { func(); }
#define EVENT_DISABLE_FUNC(func) void _disable_events() { func(); }

void _enable_events();
void _disable_events();

unsigned _temp;
void *event_memcpy(void *dest, void *src, uint16_t num);

#define CONTEXT_SYM_NAME(name) \
        _ev_ctx_ ## name


#define EV_READ(x,type) \
    *((type *)read(&(x),sizeof(type),EVENT))

#define EV_WRITE(x,val,type,is_ptr) \
    { if(is_ptr){ \
          write(&(x),sizeof(type),EVENT,val);\
      }\
      else { \
          type _temp_loc = val;\
          write(&(x),sizeof(type),EVENT,_temp_loc);\
      } \
    }

/*
 * @brief handles all the nasty interrupt declaration stuff for events, the
 * programmer just specifies the vector she's chasing in terms of how gcc and
 * clang refer to them
 * TODO figure out if there's a way to make these the same
          (CONTEXT_REF(name))->task->func |= 0x1; \
 */
//#define EVENT() __attribute((annotate("event_begin")))
#define EV_ST_SYM_NAME(name) _ev_state_ ## name
#define EV_ST2_SYM_NAME(name) _ev_state_2 ## name

/*
 * We need the name and number here to handle the two different objects we're
 * keeping around
 */
#define EV_ST_REF(name) \
        &EV_ST_SYM_NAME(name)

#define EV_ST2_REF(name) \
        &EV_ST2_SYM_NAME(name)


#define EVENT(index,name) \
        void name(); \
        __nv task_t TASK_SYM_NAME(name) = { name, index, #name }; \
        __nv tx_state TX_ST_SYM_NAME(name) = {0,0,0,0,0,0}; \
        __nv ev_state EV_ST_SYM_NAME(name) = {0,0,0,1,0};\
        __nv ev_state EV_ST2_SYM_NAME(name) = {0,0,0,1,0};\
        __nv context_t CONTEXT_SYM_NAME(name) = { & _task_ ## name , \
                                                  TX_ST_REF(name), \
                                                  EV_ST_REF(name) \
                                                };

// Macro to handle first and second phase of commit
#define EVENT_RETURN() \
        curctx->commit_state = EV_PH1;\
        transition_to(thread_ctx->task)

#define CONTEXT_REF(name) \
        &CONTEXT_SYM_NAME(name)


#endif
