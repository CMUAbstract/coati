#include "filter.h"
#include <stdio.h>
#include <assert.h>

#ifndef LIBCOATIGCC_ENABLE_DIAGNOSTICS
#define LCG_PRINTF(...)
#else
#include <stdio.h>
#define LCG_PRINTF printf
#endif

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
		LCG_PRINTF("A[%u] = %x B[%u] = %x\r\n",i,A->bits[i],i,B->bits[i]);
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
  LCG_PRINTF("incoming lengths = %u %u\r\n",len1,len2);
  for(uint16_t i = 0; i < len1; i++) {
    for(uint16_t j = 0; j < len2; j++) {
      if((void *)ser1[i] == (void *)ser2[j]) {
        return 1;
      }
    }
  }
  return 0;
}

int check_list(void **list, uint16_t len, void * addr) {
  for(uint16_t i = 0; i < len; i++) {
    #ifdef LIBCOATIGCC_TEST_COUNT
    access_len++;
    #endif
    if((void *)list[i] == addr)
      return 1;
  }
  return 0;
}

#if defined(LIBCOATIGCC_TEST_DEF_COUNT) || defined(LIBCOATIGCC_TEST_COUNT)

#if LIBCOATIGCC_HIST_LEN == 16
#pragma message ("hist len = 16")
#define NUM_HIST_BUCKETS 16
#define LOGNUM_BUCKETS 4
unsigned buckets[NUM_HIST_BUCKETS] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};

#elif LIBCOATIGCC_HIST_LEN == 32
#pragma message ("hist len = 32")
#define NUM_HIST_BUCKETS 32
#define LOGNUM_BUCKETS 5
unsigned buckets[NUM_HIST_BUCKETS] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
                                16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
#elif LIBCOATIGCC_HIST_LEN == 128
#pragma message ("hist len = 128")
#define NUM_HIST_BUCKETS 128
#define LOGNUM_BUCKETS 7
unsigned buckets[NUM_HIST_BUCKETS] = {
0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,
31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,
58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,
85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,
109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127};

#elif LIBCOATIGCC_HIST_LEN == 256
#pragma message ("hist len = 256")
#define NUM_HIST_BUCKETS 256
#define LOGNUM_BUCKETS 8
unsigned buckets[NUM_HIST_BUCKETS] = {
0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,
31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,
59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,84,85,86,
87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,
111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,128,129,130,131,
132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,152,
153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,169,170,171,172,173,
174,175,176,177,178,179,180,181,182,183,184,185,186,187,188,189,190,191,192,193,194,
195,196,197,198,199,200,201,202,203,204,205,206,207,208,209,210,211,212,213,214,215,
216,217,218,219,220,221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,
237,238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253,254,255
};
#else
#error ("Undefined hist len!")
#endif // case statement of hist len
unsigned counts[NUM_HIST_BUCKETS] = {0};
unsigned max = 0;
// Functions for diagnostics

// Function to add a value to the histogram
void add_to_histogram(unsigned val) {
  unsigned i = 0;
  if (val > max)
    max = val;
  unsigned mid = (NUM_HIST_BUCKETS >> 1) - 1;
  for(i = 1; i < LOGNUM_BUCKETS; i++) {
    if(val > buckets[mid]){
      mid = mid + (NUM_HIST_BUCKETS >> (i+1));
    }
    else if(val < buckets[mid]){
      mid = mid - (NUM_HIST_BUCKETS >> (i+1));
    }
    else {
      counts[mid]++;
      return;
    }
  }
  if(val > buckets[mid])
    counts[mid+1]++;
  else if (val < buckets[mid])
    counts[mid-1]++;
  else
    counts[mid]++;
  return;
}

// Function to print histogram

void print_histogram() {
  unsigned i = 0;
  unsigned sum = 0, overflow_sum = 0;
  for(i = 0; i < NUM_HIST_BUCKETS; i++) {
    printf("%u, %u\r\n",buckets[i],counts[i]);
    if(counts[i] > (0xFFFF - sum)) {
      overflow_sum++;
      sum = counts[i] - (0xFFFF - sum);
    }
    else {
      sum += counts[i];
    }
  }
  printf("Total counts = %u + %u / 65535, Max = %u\r\n",overflow_sum, sum, max);
}
#endif // COUNT_TEST || DEF_COUNT_TEST
