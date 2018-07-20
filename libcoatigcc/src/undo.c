#include <msp430.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <libmsp/mem.h>

#include "undo.h"

__nv uint16_t old_bucket_len;
__nv uint16_t old_tx_buf_len;

__nv uint8_t log_need_commit;

