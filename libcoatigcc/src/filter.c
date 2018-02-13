#include "filter.h"
#include <stdio.h>
#include <assert.h>

/* mod needs to be a power of 2 for this to work */
unsigned my_modulus(unsigned value_in, unsigned mod){
  // Brian Kernigan hack to determine if a number is a power of two
  assert((mod & (mod - 1)) == 0);

  unsigned value_out;
	// Check if value is less than modulus
	if (value_in < mod)
		return value_in;
	// Otherwise, calculate
	else{
		// Get remainder by using mask of mod - 1
		// this only works b/c mod is a power of two
		value_out = value_in & (mod - 1);
	}
	return (value_out);
}

/*
 * @brief Adds an item to the bloom filter
 */
void add_to_filter(bloom_filter *filter, unsigned address){
	unsigned key, key_byte, key_bit;
	//printf("Add to filter\r\n");
  // apply all the hash functions
	bloom_hash *cur;
	for(cur = filter->hash; cur; cur = cur->next){
		key = (cur->func)(address);
	}
	// print the filter values before modification
  #if 0
  printf("Filter values: ");
  print_filter(filter);
  #endif // 0
  // modulo number of buckets in the filter
	key = my_modulus(key,filter->size);
	// turn modulo into bits
	key_byte = key >> 3;
	key_bit = key - (key_byte << 3);
	filter->bits[key_byte] |= 1 << key_bit;
  #if 0
  //printf("%x %x %x \r\n",key, key_byte, key_bit);

	printf("New filter values: ");
	print_filter(filter);
  #endif
	return;
}

/*
 * @brief compares the values in two bloom filters
 * @returns -1 on error, 1 on a conflict, 0 on no no conflicts
 */
int compare_filters(bloom_filter *A, bloom_filter *B){
	if(A->size != B->size)
		return -1;
	// cycle through each byte and compare
	for(size_t i = 0; i < A->size >> 3; i++){
		printf("A[%u] = %x B[%u] = %x\r\n",i,A->bits[i],i,B->bits[i]);
    if(A->bits[i] & B->bits[i])
			return 1;
	}
	return 0;
}

void print_filter(bloom_filter *filter) {
  for(uint8_t i = 0; i < FILTER_BYTES;i++) {
    printf("%x ",filter->bits[i]);
  }
  printf("\r\n");
  return;
}

void clear_filter(bloom_filter *filter) {
    for(uint8_t i = 0; i < FILTER_BYTES;i++) {
      filter->bits[i] = 0;
    }
#if 0
    printf("Clear: ");
    for(uint8_t i = 0; i < FILTER_BYTES;i++) {
      printf("%x ",filter->bits[i]);
    }
    printf("\r\n");
#endif
    return;
}

int compare_lists(void **ser1, void **ser2, uint16_t len1, uint16_t len2) {
  for(uint16_t i = 0; i < len1; i++) {
    for(uint16_t j = 0; j < len2; j++) {
      if((void *)ser1[i] == (void *)ser2[j]) {
        return 1;
      }
    }
  }
  return 0;
}
