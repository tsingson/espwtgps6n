//
// Created by tsingson on 2026/6/27.
//

#ifndef ESPWTGPS6N_UBLOXM10NONA_H
#define ESPWTGPS6N_UBLOXM10NONA_H
//
// Created by tsingson on 2026/6/27.
//

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "UBLOX_M10";

// ==============================================================================
// 1. 核心硬件与参数宏定义 (根据您的实际硬件引脚与串口进行调整)
// ==============================================================================
#define GPS_UART_NUM UART_NUM_2 // 使用 ESP32 的 UART2
#define GPS_NONA_TX_PIN 17           // ESP32 TX 引脚 (连 GPS RX)
#define GPS_NONA_RX_PIN 16           // ESP32 RX 引脚 (连 GPS TX)
#define NONA_BUF_SIZE (1024)         // 串口接收缓冲区大小
#define GPS_UBX_RATE 38400          // TTL 频率

// u-blox 协议标准同步码与类定义
#define UBX_SYNC_CHAR_1 0xB5
#define UBX_SYNC_CHAR_2 0x62
#define UBX_CLASS_CFG 0x06
#define UBX_ID_VALSET 0x8A

// 配置存储目标层 (Layers)
#define UBX_LAYER_ALL 0x07 // 同时写入 RAM, BBR 和 Flash，防掉电丢失配置

// u-blox M10 配置键值 ID (Key IDs)
#define KEY_UART1OUTPROT_UBX 0x20010021 // 端口输出协议配置
#define KEY_RATE_MEAS 0x30210001        // 测量频率配置
#define KEY_MSGOUT_NAV_PVT                                                     \
  0x20910007 // ⭐ M10 专属全局 NAV-PVT 消息主动上报控制键

void init_ubx_nona_gps_uart(void);
void ubx_nona_append_checksum(uint8_t *buffer, size_t len);
void gps_configure_ubx_nona_proc(void);

void process_ubx_nona_byte(uint8_t byte);

#endif // ESPWTGPS6N_UBLOXM10NONA_H
