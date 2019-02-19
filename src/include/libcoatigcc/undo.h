#ifndef _UNDO_LOG_
#define _UNDO_LOG_
// General undo log for uint16_t's


/*
 * @brief Restores all the values in the undo log and clears internal
 * need_commit flag
 */
void restore();

/*
 * @brief Dumps the values pointed to by the array, vals, of length len into the
 * undo log
 * @return returns 0 if values were correctly added to undo log, 1, if not
 */
uint8_t log_start(uint16_t **vals, uint16_t len);

/*
 * @brief Clears need_commit flag
 */
void log_end();

#endif
