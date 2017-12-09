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
    uint8_t in_ev;
    uint8_t ev_need_commit;
} ev_state;

extern volatile uint16_t num_evbe;
extern volatile uint8_t need_ev_commit;

extern ev_state state_ev_1;
extern ev_state state_ev_0;

extern context_t *thread_ctx;
extern task_t * cur_tx_start;
extern bloom_filter write_filters[NUM_PRIO_LEVELS];
extern bloom_filter read_filters[NUM_PRIO_LEVELS];

void event_return();
void event_handler();
void ev_commit();

int16_t  evfind(const void * addr);
void *  ev_get_dst(void * addr);
void * ev_dirty_buf_alloc(void * addr, size_t size);

extern volatile uint16_t num_evbe;
extern __nv uint8_t ev_dirty_buf[BUF_SIZE];
extern __nv void * ev_dirty_src[NUM_DIRTY_ENTRIES];
extern __nv void * ev_dirty_dst[NUM_DIRTY_ENTRIES];
extern __nv size_t ev_dirty_size[NUM_DIRTY_ENTRIES];

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
          write(&(x),sizeof(type),EVENT,&_temp_loc);\
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

#define EVENT(index,name) \
          void name(); \
          __nv task_t TASK_SYM_NAME(name) = { name, index, #name }; \
          __nv context_t CONTEXT_SYM_NAME(name) = { & _task_ ## name ,NULL};

#define CONTEXT_REF(name) \
        &CONTEXT_SYM_NAME(name)

#define EVENT_SETUP( name , gcc_vect, clang_vect) \
          void __attribute__(gcc_vect) clang_vect ## ISR(void) \
          { printf("In ev shell\r\n\n\n\n\n"); \
            event_handler(TASK_REF(name)); } \
          __attribute__((section("interrupt ## clang_vect"),aligned(2))) \
          void(*__## clang_vect)(void) = clang_vect ## ISR;

#endif
