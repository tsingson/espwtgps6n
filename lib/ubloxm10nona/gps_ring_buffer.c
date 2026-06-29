#include "gps_ring_buffer.h"

// Instantiate the global static ring buffer
static gps_spsc_ring_buffer_t gps_rb = {.head = 0, .tail = 0};

/* ================= 4. Lock-Free SPSC Core Functions ================= */

/**
 * @brief Producer push data: Copies a structure into the ring buffer.
 * @note  If the buffer is full (consumer not online), it automatically
 *        overwrites and discards the oldest entry to keep the 16 newest
 * records.
 */
void gps_rb_push_overwrite(const gps_location_t *new_data) {
  if (new_data == NULL)
    return;

  // Cache to local variables to prevent race condition issues from mid-way
  // modifications
  uint32_t current_head = gps_rb.head;
  uint32_t current_tail = gps_rb.tail;

  // 1. Copy data into current head index safely
  memcpy((void *)&gps_rb.buffer[current_head], (const void *)new_data,
         sizeof(gps_location_t));

  // 2. Hardware Memory Barrier for Xtensa Architecture
  // Instructs the ESP32 CPU pipeline to flush the memcpy execution to RAM
  // BEFORE updating index variables. Prevents instruction reordering errors.
  // asm volatile("memw" : : : "memory");

  uint32_t next_head = (current_head + 1) % BUFFER_SIZE;

  // 3. Check if buffer overflows (head catches tail)
  if (next_head == current_tail) {
    // Enforce the push: move tail forward, throwing away the oldest record
    gps_rb.tail = (current_tail + 1) % BUFFER_SIZE;
  }

  // 4. Finally, update head. The consumer task can now see the fresh item
  gps_rb.head = next_head;
}

/**
 * @brief Consumer pop data: Reads a structure out of the ring buffer.
 * @return BaseType_t Returns pdTRUE on success, pdFALSE if buffer is empty.
 */
BaseType_t gps_rb_pop(gps_location_t *out_data) {
  if (out_data == NULL)
    return pdFALSE;

  uint32_t current_head = gps_rb.head;
  uint32_t current_tail = gps_rb.tail;

  // Head matching tail indicates empty buffer condition
  if (current_head == current_tail) {
    return pdFALSE;
  }

  // 1. Fetch data from current tail index via copy
  memcpy((void *)out_data, (const void *)&gps_rb.buffer[current_tail],
         sizeof(gps_location_t));

  // 2. Memory Barrier for data ordering security
  // asm volatile("memw" : : : "memory");

  // 3. Shift tail index to open up space slot
  gps_rb.tail = (current_tail + 1) % BUFFER_SIZE;

  return pdTRUE;
}

/**
 * @brief Evaluates current unread element count
 * @return uint32_t Quantity of buffered records (0 to 16)
 */
uint32_t gps_rb_get_unread_count(void) {
  uint32_t h = gps_rb.head;
  uint32_t t = gps_rb.tail;

  if (h >= t) {
    return h - t;
  } else {
    return BUFFER_SIZE - (t - h);
  }
}
