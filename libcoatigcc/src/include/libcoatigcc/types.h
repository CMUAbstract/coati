#ifndef _TYPES_H
#define _TYPES_H

typedef enum _acc_type {
    NORMAL=0,
    TX=1,
    EVENT=2,
    #ifdef LIBCOATIGCC_TEST_COUNT
    NORMAL_NI=3,
    TX_NI=4
    #endif
} acc_type;


#endif
