#include <stdint.h>
#include <stdio.h>
#include "hash.h"

void *src[NUM_BINS][BIN_LEN] = {0};
void *dst[NUM_BINS][BIN_LEN] = {0};
size_t size[NUM_BINS][BIN_LEN] = {0};
uint8_t lens[NUM_BINS] = {0};

uint8_t dirty_buf[BUF_LEN];
uint16_t buf_level = 0;

/*table_t tsk_table = { &src[0][0],
                      &dst[0][0],
                      &size[0][0],
                      &lens[0]
                    };
                    */
table_t tsk_table = { src,
                      dst,
                      size,
                      lens
                    };
/*
 * @brief simple hash function XOR's the bottom byte with the top byte
 */
uint16_t hash(void * address) {
  return ((((uint16_t)address & 0xFF00) >> 8) ^ ((uint16_t)address & 0xFF));
}

/*
 * @brief returns an index into the buffer if a value is in the table or an
 * error code (0xFFFF) if not
 */
uint16_t check_table(table_t *table,void * addr) {
  uint16_t bucket;
  // Calc hash of address
  bucket = hash(addr);
  bucket &= BIN_MASK;
  if(table->bucket_len[bucket]) {
    for(int i = 0; i < table->bucket_len[bucket]; i++) {
      if(table->src[bucket][i] == addr) {
        printf("Found it!\r\n");
        return table->dst[bucket][i];
      }
    }
  }
  // Intentional fall through
  return 0xFFFF;
}

/*
 * @brief Adds a value to the table
 * @ars pointer to table, pointer to table capacity, address to locate size of
 * var
 */
uint16_t add_to_table(table_t *table, uint16_t *cap, void * addr, size_t size) {
  uint16_t bucket;
  // Calc hash of address
  bucket = hash(addr);
  bucket &= BIN_MASK;
  printf("Bucket = %u\r\n",bucket);
  // Optimization for empty buckets
  if(table->bucket_len[bucket]) {
    int i = 0;
    for(i = 0; i < table->bucket_len[bucket]; i++) {
      if(table->src[bucket][i] == addr) {
        break;
      }
    }
    // add to bucket if not present
    if(i == table->bucket_len[bucket]) {
      if(i + 1 > BIN_LEN) {
        printf("Error! overflowed bin!\r\n");
        return 1;
      }
      (table->src[bucket][i]) = addr;
      table->size[bucket][i] = size;
      printf("Setting src %x --> %x with size %u\r\n",
                                        (uint16_t)addr,
                                        ((uint16_t) (table->src[bucket][i])),
                                        table->size[bucket][i]);
      // TODO add check
      uint16_t temp;
      temp = alloc(dirty_buf, cap, addr, size);
      if(temp == 0xFFFF) {
        printf("Alloc failed!\r\n");
        return 1;
      }
      table->dst[bucket][i] = dirty_buf + temp;
      table->bucket_len[bucket]++;
    }
  }
  else {
    (table->src[bucket][0]) = addr;
    table->size[bucket][0] = size;
    printf("Setting src %x --> %x with size %u\r\n",
                                (uint16_t)addr,
                                ((uint16_t) (table->src[bucket][0])),
                                table->size[bucket][0]);
    uint16_t temp;
    temp = alloc(dirty_buf, cap, addr, size);
    if(temp == 0xFFFF) {
      printf("Alloc failed!\r\n");
      return 1;
    }
    table->dst[bucket][0] = dirty_buf + temp;
    table->bucket_len[bucket]++;
  }
  printf("final bucket len = %u\r\n",table->bucket_len[bucket]);
  return 0;
}

/*
 * @brief Return a location into buf where the new value can go or return an error
 * @args pointer to dirty buffer, pointer to buffer capacity level, address, var
 * size
 */
uint16_t alloc(uint8_t *buf, uint16_t *buf_cap, void * addr, size_t size) {
  if(*buf_cap + size > BUF_LEN) {
    return 0xFFFF;
  }
  // TODO confirm that this indexing works
  for(int i = 0; i < size; i++) {
    buf[*buf_cap + i] = *((uint8_t *)(addr + size - i - 1));
  }
  uint16_t loc = *buf_cap;
  *buf_cap += size;
  return loc;
}

/*
 * @brief Zeros out all of the values in the table by clearing the length of
 * each bucket
 */
void clear_table(table_t *table) {
  for(int i = 0; i < NUM_BINS; i++) {
      table->bucket_len[i] = 0;
  }
  return;
}

