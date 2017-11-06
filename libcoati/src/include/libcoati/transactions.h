#ifndef _TX_H
#define _TX_H

typedef struct _tx_state {
    uint16_t num_dtxv;
    uint8_t in_tx;
    uint8_t tx_need_commit;
} tx_state;

#define TX_BEGIN \
    tx_begin()


#endif //_TX_H
