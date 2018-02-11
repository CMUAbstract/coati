#ifndef BLOOM_FILTER_H
#define BLOOM_FILTER_H
#include <stdint.h>
#include <stddef.h>

// TODO have these decided by the compiler.
#define FILTER_SIZE 16
#define LOG2_FILTER_SIZE 4
#define FILTER_BYTES (FILTER_SIZE >> 3)

typedef unsigned (*hash_func)(unsigned);

typedef struct bloom_hash_{
	hash_func func;
	struct bloom_hash_ *next;
} bloom_hash;

typedef struct bloom_filter_{
	uint8_t size;
	uint8_t bits[FILTER_BYTES];
	bloom_hash *hash;
} bloom_filter;

void add_to_filter(bloom_filter *, unsigned);

int compare_filters(bloom_filter *, bloom_filter *);

void clear_filter(bloom_filter *);

void print_filter(bloom_filter *);
#endif //BLOOM_FILTER_H
