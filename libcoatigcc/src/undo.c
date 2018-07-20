#include <msp430.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <libmsp/mem.h>

#include "undo.h"

#define UNDO_SIZE 2

// TODO generalize so we can call repeatedly? Or for vars of different sizes?

static __nv uint16_t log_len;

static __nv uint16_t * log_addrs[UNDO_SIZE];

static __nv uint16_t log_vals[UNDO_SIZE];

static __nv uint8_t log_need_commit;

void restore_log() {
  if(log_need_commit) {
    for(uint16_t i = 0; i < log_len; i++) {
      *(log_addrs[i]) = log_vals[i];
      printf("%x %u\r\n",log_addrs[i], *(log_addrs[i]));
    }
  }
  log_len = 0;
  log_need_commit = 0;
  return;
}

uint8_t log_start(uint16_t **vals, uint16_t len) {
  if(len > UNDO_SIZE) {
    return 1;
  }
  for(uint16_t i = 0; i < len; i++) {
    log_addrs[i] = vals[i];
    log_vals[i] = *(vals[i]);
  }
  log_len = len;
  log_need_commit = 1;
  return 0;
}

void log_end() {
  log_need_commit = 0;
  return;
}


