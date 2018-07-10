#ifndef HASH_H
#define HASH_H
#include <stddef.h>

#define NUM_BINS 64
#define BIN_MASK 0x3F
#define BIN_LEN  2
#define BUF_LEN 200
#define HASH_ERROR 0xffff

typedef struct table_ {
  void  * src[NUM_BINS][BIN_LEN];
  void  * dst[NUM_BINS][BIN_LEN];
  size_t size[NUM_BINS][BIN_LEN];
  uint8_t bucket_len[NUM_BINS];
  uint16_t active_bins;
} table_t;

typedef struct src_table_ {
  void *src[NUM_BINS][BIN_LEN];
  uint8_t bucket_len[NUM_BINS];
  uint16_t active_bins;
} src_table;


uint16_t hash(void * address);
uint16_t add_to_table(table_t *table, uint16_t *cap, void * addr,
                                            void * value, size_t size);
uint16_t check_table(table_t *table, void * addr);
uint16_t alloc(uint8_t * buf, uint16_t *cap, void * addr, size_t size);

uint16_t add_to_src_table(src_table *table, void * address);
uint8_t compare_src_tables(src_table *table1, src_table *table2);
//void * new_read(void *addr, size_t size, table_t *table);


// TODO these depend on vars defined in coati.h and I don't want a circular
// dependence between hash.h and coati.h
#if 0
#define NEW_READ(x, type) \
  *((type *)new_read(&(x), sizeof(type), &tsk_table))

#define NEW_WRITE(x, val, type, is_ptr) \
    { type _temp_loc = val; \
      printf("Val = %u\r\n",(uint16_t) _temp_loc);\
      add_to_table(&tsk_table, &buf_level, &(x), &_temp_loc, sizeof(type)); \
    }
#endif

#endif // HASH_H
