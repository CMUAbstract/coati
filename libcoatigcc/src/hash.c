#include <stdint.h>
#include <stdio.h>
#include "hash.h"

#ifndef LIBCOATIGCC_ENABLE_DIAGNOSTICS
#define LCG_PRINTF(...)
#else
#include <stdio.h>
#define LCG_PRINTF printf
#endif

table_t tsk_table;

/*
 * @brief simple hash function XOR's the bottom byte with the top byte
 */
uint16_t hash(void * address) {
  return ((((uint16_t)address & 0xFF00) >> 8) ^ ((uint16_t)address & 0xFF));
}

/*
 * @brief returns a pointer into the buffer if a value is in the table or an
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
        LCG_PRINTF("Found it!\r\n");
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
uint16_t add_to_table(table_t *table, uint8_t *dirty_buf, uint16_t *cap, 
                              void * addr,void * value,  size_t size) {
  uint16_t bucket;
  // Calc hash of address
  bucket = hash(addr);
  bucket &= BIN_MASK;
  LCG_PRINTF("Bucket = %u\r\n",bucket);
  int i = 0;
  uint16_t temp;
  // Check for matching address in bucket
  // We should skip this if bucket_len == 0
  while(i < table->bucket_len[bucket]) {
    if(table->src[bucket][i] == addr) {
      memcpy(table->dst[bucket][i], value, size);
      break;
    }
    i++;
  }
  // add to bucket if not present
  if(i == table->bucket_len[bucket]) {
    if(i + 1 > BIN_LEN) {
      printf("Error! overflowed bin!\r\n");
      while(1);
      return 1;
    }
    (table->src[bucket][i]) = addr;
    table->size[bucket][i] = size;
    LCG_PRINTF("Setting stuff val %x --> %x with size %u,addr %x\r\n",
                                      *((uint16_t *)value),
                                      ((uint16_t) (table->src[bucket][i])),
                                      table->size[bucket][i],
                                      (uint16_t)addr);
    temp = alloc(dirty_buf, cap, addr, size);
    if(temp == 0xFFFF) {
      printf("Alloc failed!\r\n");
      while(1);
      return 1;
    }
    table->dst[bucket][i] = dirty_buf + temp;
    table->bucket_len[bucket]++;
    // Need to have this add down here so we persist this stuff correctly
    memcpy(dirty_buf + temp, value, size);
    LCG_PRINTF("New val: %u\r\n",*((uint16_t *)(table->dst[bucket][i])));
  }
  LCG_PRINTF("final bucket len = %u\r\n",table->bucket_len[bucket]);
  return 0;
}

/*
 * @brief Return a location into buf where the new value can go or return an error
 * @args pointer to dirty buffer, pointer to buffer capacity level, address, var
 * size
 */
uint16_t alloc(uint8_t *buf, uint16_t *buf_cap, void * addr, size_t size) {
  uint16_t new_ptr;
  size_t extra = 0;
  if(*buf_cap + size > BUF_LEN) {
    return 0xFFFF;
  }
  if(*buf_cap) {
    new_ptr = (uint16_t) buf + *buf_cap;
  }
  else {
    new_ptr = (uint16_t) buf;
  }
  // TODO make more general
  // Fix alignment struggles
  if(size == 2) {
    while(new_ptr & 0x1) {
      new_ptr++;
      extra++;
    }
    // TODO get rid of this line
    if(extra) {
      LCG_PRINTF("Beefing up! %u\r\n", extra);
    }
  }
  if(size == 4) {
    while(new_ptr & 0x11) {
      new_ptr++;
      extra++;
    }
  }
  // If we're out of space, throw an error
  if(new_ptr + size > (unsigned) (buf + BUF_LEN)) {
      return NULL;
  }
  /*
  // This is potentially how the memcpy should be happening, I'm just also
  // wondering if it's problematic that it's here too...
  for(int i = 0; i < size; i++) {
    buf[*buf_cap + i] = *((uint8_t *)(addr + size - i - 1));
  }*/
  uint16_t loc = *buf_cap + extra;
  *buf_cap += size + extra;
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

/*
 * @brief Searches the table for an address and returns a pointer to the
 * object's location in the dirty buffer if it's there.
 * @details lacks the extra functionality of the read in coati.c, this one
 * doesn't have event/tx/tsk read, this is just for tasks and just a simple
 * wrapper for check_table
 */
void * new_read(void *addr, size_t size, table_t *table) {
  // First check if value is in table
  uint16_t flag;
  flag = check_table(table, addr);
  // If value is not in the table, return the main memory location
  if(flag == 0xFFFF) {
    return addr;
  }
  else {
    return flag;
  }
}

/*
 * @brief function to add an address to a basic src table structure
 */
uint16_t add_to_src_table(src_table *table, void *addr) {
  uint16_t bucket;
  // Calc hash of address
  bucket = hash(addr);
  bucket &= BIN_MASK;
  LCG_PRINTF("Bucket = %u\r\n",bucket);
  int i = 0;
  while(i < table->bucket_len[bucket]) {
    if(table->src[bucket][i] == addr) {
      break;
    }
    i++;
  }
  // add to bucket if not present
  if(i + 1 > BIN_LEN) {
    printf("Error! overflowed bin!\r\n");
    while(1);
    return 1;
  }
  (table->src[bucket][i]) = addr;
  table->bucket_len[bucket]++;
  LCG_PRINTF("final bucket len = %u\r\n",table->bucket_len[bucket]);
  return 0;
}

/*
 * @brief function to compare the contents of two src tables returns 1 if there
 * is a conflict(any overlapping values), 0 otherwise
 */
uint8_t compare_src_tables(src_table *table1, src_table *table2) {
  while(table1->active_bins > 0) {
    uint16_t bin;
    bin = table1->active_bins;
    uint16_t slot1;
    slot1 = table1->bucket_len[bin];
    while(slot1 > 0) {
      slot1 = table1->bucket_len[bin];
      uint16_t slot2;
      slot2 = table1->bucket_len[bin];
      while(slot2 > 0) {
        if(table1->src[bin][slot1] == table2->src[bin][slot2]) {
          return 1;
        }
        slot2--;
      }
      slot1--;
    }
    table1->active_bins--;
  }
  return 0;
}




