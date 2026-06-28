#include "gps_parser.h"

// This variable is restricted to this file via 'static'
static gps_parser_state_t current_state = GPS_STATE_IDLE;

void gps_parser_init(void) { current_state = GPS_STATE_IDLE; }

gps_parser_state_t gps_parser_get_current_state(void) { return current_state; }
