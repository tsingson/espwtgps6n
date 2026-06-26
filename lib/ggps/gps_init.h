//
// Created by tsingson on 2026/6/26.
//

#ifndef ESPWTGPS6N_GPS_INIT_H
#define ESPWTGPS6N_GPS_INIT_H
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// static const char *TAG = "WT_GPS_6N";

#define GPS_UART_NUM UART_NUM_2
#define GPS_BAUD_RATE 115200
#define BUF_SIZE 1024
#define GPS_TX_PIN GPIO_NUM_33
#define GPS_RX_PIN GPIO_NUM_32

void init_gps_uart(void);

void deinit_gps_uart(void);

#endif // ESPWTGPS6N_GPS_INIT_H
