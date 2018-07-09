#ifndef HASH_H
#define HASH_H
#include <stddef.h>

#define NUM_BINS 64
#define BIN_MASK 0x3F
#define BIN_LEN  2
#define BUF_LEN 200

typedef struct table_ {
  void  * src[NUM_BINS][BIN_LEN];
  void  * dst[NUM_BINS][BIN_LEN];
  size_t size[NUM_BINS][BIN_LEN];
  uint8_t bucket_len[NUM_BINS];
} table_t;

extern uint8_t dirty_buf[BUF_LEN];
extern uint16_t buf_level;
extern table_t tsk_table;

uint16_t hash(void * address);
uint16_t add_to_table(table_t *table, uint16_t *cap, void * addr, size_t size);
uint16_t check_table(table_t *table, void * addr);
uint16_t alloc(uint8_t * buf, uint16_t *cap, void * addr, size_t size);

#endif // HASH_H
