#ifndef GPS_PARSER_H
#define GPS_PARSER_H

#include <stdint.h>

// 1. The publicly accessible enum type definition
typedef enum {
  GPS_STATE_IDLE,
  GPS_STATE_SYNC2,
  GPS_STATE_CLASS,
  GPS_STATE_ID,
  GPS_STATE_LEN1,
  GPS_STATE_LEN2,
  GPS_STATE_PAYLOAD,
  GPS_STATE_CKA,
  GPS_STATE_CKB
} gps_parser_state_t;

// 2. (Optional) Expose functions that use this type
void gps_parser_init(void);
gps_parser_state_t gps_parser_get_current_state(void);

#endif // GPS_PARSER_H
