#ifndef _UNDO_LOG_
#define _UNDO_LOG_
// General undo log for uint16_t's

extern uint16_t old_bucket_len;
extern uint16_t old_tx_buf_len;
extern uint8_t log_need_commit;

#define LOG_START(bucket, buf) \
  old_bucket_len = bucket; \
  old_tx_buf_len = buf; \
  log_need_commit = 1;

#define LOG_END \
  log_need_commit = 0;

#define LOG_RESTORE \
  if(log_need_commit) { \
    tsk_table.bucket_len[tsk_table.active_bins - 1] = old_bucket_len;\
    tx_buf_level = old_tx_buf_len;\
  }\
  log_need_commit = 0;

#endif
