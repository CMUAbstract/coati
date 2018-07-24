#ifndef HASH_H
#define HASH_H
#include <stddef.h>
// Both NUM_BINS and BIN_LEN need to be powers of 2
#define NUM_BINS 16
#define BIN_MASK 0xF
#define BIN_LEN  16
#define LOG_BIN_LEN_PLUS 4
// BUF_LEN needs to equal 400 for cem, 400 for cuckoo, 250 is ok for rsa
#define BUF_LEN 400
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
uint16_t add_to_table(table_t *table, uint8_t *buf, uint16_t *cap, void * addr,
                                            void * value, size_t size);
uint16_t check_table(table_t *table, void * addr);
uint16_t alloc(uint8_t * buf, uint16_t *cap, void * addr, size_t size);

#ifdef LIBCOATIGCC_BUFFER_ALL
uint16_t add_to_src_table(src_table *table, void * address);
uint8_t compare_src_tables(src_table *table1, src_table *table2);
uint8_t compare_list_to_hash(src_table *table, void **list, uint16_t len);
#endif // BUFFER_ALL

#endif // HASH_H
