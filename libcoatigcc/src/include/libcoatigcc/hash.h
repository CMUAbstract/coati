#ifndef HASH_H
#define HASH_H
#include <stddef.h>

#define NUM_BINS 64
#define BIN_MASK 0x3F
#define BIN_LEN  2
#define BUF_LEN 200

typedef struct table_ {
  void  ***src;
  void  ***dst;
  size_t **size;
  uint8_t *bucket_len;
} table_t;

extern uint8_t dirty_buf[BUF_LEN];
extern uint16_t buf_level;
extern table_t tsk_table;

extern void * src[NUM_BINS][BIN_LEN];
extern void * dst[NUM_BINS][BIN_LEN];
extern size_t size[NUM_BINS][BIN_LEN];
extern uint8_t lens[NUM_BINS];

uint16_t hash(void * address);
uint16_t add_to_table(table_t *table, uint16_t *cap, void * addr, size_t size);
uint16_t check_table(table_t *table, void * addr);
uint16_t alloc(uint8_t * buf, uint16_t *cap, void * addr, size_t size);

#endif // HASH_H
