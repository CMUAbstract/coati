#ifndef _EVENT_H
#define _EVENT_H
#include <libmsp/mem.h>
#include "bloom_filter.h"
#include "coati.h"

#define PRINT(x) (#x)

#define THREAD 0
#define EV  1
#define NUM_PRIO_LEVELS 2


extern context_t *thread_ctx;
extern task_t * cur_tx_start;
extern bloom_filter filters[NUM_PRIO_LEVELS];

void event_return();
void event_handler();

#define EVENT_ENABLE_FUNC(func) void _enable_events() { func(); }
#define EVENT_DISABLE_FUNC(func) void _disable_events() { func(); }

void _enable_events();
void _disable_events();

unsigned _temp;

#define CONTEXT_SYM_NAME(name) \
        _ev_ctx_ ## name


#define EV_READ(x,type) \
    *((type *)read(&(x)))

#define EV_WRITE(x,val,type) \
    add_to_filter(filters + EV,&(x)); \
    if(sizeof(type) == 1) \
        write_byte(&(x),(uint8_t)val); \
    else if(sizeof(type) == 2)\
        write_word(&(x),(uint16_t)val) \

/*
 * @brief handles all the nasty interrupt declaration stuff for events, the
 * programmer just specifies the vector she's chasing in terms of how gcc and
 * clang refer to them
 * TODO figure out if there's a way to make these the same
          (CONTEXT_REF(name))->task->func |= 0x1; \
 */
#define EVENT(index,name) \
          void name(); \
          __nv task_t TASK_SYM_NAME(name) = { name, index, #name }; \
          __nv context_t CONTEXT_SYM_NAME(name) = { & _task_ ## name ,NULL}; 

#define CONTEXT_REF(name) \
        &CONTEXT_SYM_NAME(name)
         
#define EVENT_SETUP( name , gcc_vect, clang_vect) \
          void __attribute__(gcc_vect) clang_vect ## ISR(void) \
          { printf("In ev shell\r\n\n\n\n\n"); \
            event_handler(CONTEXT_REF(name)); } \
          __attribute__((section("interrupt ## clang_vect"),aligned(2))) \
          void(*__## clang_vect)(void) = clang_vect ## ISR;

#endif
