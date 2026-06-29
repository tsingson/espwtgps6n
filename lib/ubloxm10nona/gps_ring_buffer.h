//
// Created by tsingson on 2026/6/28.
//

#ifndef ESPWTGPS6N_GPS_RING_BUFFER_H
#define ESPWTGPS6N_GPS_RING_BUFFER_H
//
// Created by tsingson on 2026/6/28.
//

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

/*
 * 1. Buffer configuration
 * 16 effective item capacity. In a lock-free SPSC architecture,
 * array sizing needs to be Capacity + 1 (17 slots total) to distinguish
 * between empty and full states via indexing.
 */
#define REQ_CAPACITY 16
#define BUFFER_SIZE (REQ_CAPACITY + 1)
/* 2. Core GPS Data Structure */
typedef struct {
  uint8_t fixType; // Fix Type (0=No Fix, 2=2D, 3=3D Fix)
  uint8_t numSV;   // Number of satellites used in positioning
  int32_t lon;     // Longitude (scaled by 1e-7)
  int32_t lat;     // Latitude (scaled by 1e-7)
  int32_t gSpeed;  // Ground Speed (mm/s)
} gps_location_t;

/* 3. Static Lock-Free Ring Buffer Structure */
typedef struct {
  gps_location_t buffer[BUFFER_SIZE];
  volatile uint32_t head; // volatile enforces memory read/write cycles
  volatile uint32_t tail;
} gps_spsc_ring_buffer_t;

void gps_rb_push_overwrite(const gps_location_t *new_data);
BaseType_t gps_rb_pop(gps_location_t *out_data);
uint32_t gps_rb_get_unread_count(void);

#endif // ESPWTGPS6N_GPS_RING_BUFFER_H
