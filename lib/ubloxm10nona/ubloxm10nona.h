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
#include "sdkconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "UBLOX_M10";

// ==========================================================================
// 1. 核心硬件与参数宏定义 (根据您的实际硬件引脚与串口进行调整)
// ==========================================================================
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
#define GPS_UART_NUM UART_NUM_1 // C3 和 C6 只有两个串口，使用 UART1
#else
#define GPS_UART_NUM UART_NUM_2 // 经典的 ESP32 继续使用 UART2
#endif

// #define GPS_UART_NUM UART_NUM_2 // 使用 ESP32 的 UART2
#define GPS_NONA_TX_PIN 17   // ESP32 TX 引脚 (连 GPS RX)
#define GPS_NONA_RX_PIN 16   // ESP32 RX 引脚 (连 GPS TX)
#define NONA_BUF_SIZE (1024) // 串口接收缓冲区大小
#define GPS_UBX_RATE 38400   // TTL 频率

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

// typedef
// 💡 确保包含了 C3、C6 的宏，甚至是后续可能扩充的其它 RISC-V 芯片
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
#define GPS_UART_NUM UART_NUM_0
#else
#define GPS_UART_NUM UART_NUM_2
#endif
// ==============================================================================
// 2. UBX-NAV-PVT 数据结构体定义 (严格 1 字节对齐，用于内存直接映射)
// ==============================================================================
#pragma pack(push, 1)
typedef struct {
  uint32_t iTOW;    // GPS 毫秒时间戳
  uint16_t year;    // 年
  uint8_t month;    // 月
  uint8_t day;      // 日
  uint8_t hour;     // 时
  uint8_t min;      // 分
  uint8_t sec;      // 秒
  uint8_t valid;    // 有效性标志
  uint32_t tAcc;    // 时间精度
  int32_t nano;     // 纳秒
  uint8_t fixType;  // 定位类型 (0=无定位, 2=2D, 3=3D定位)
  uint8_t flags;    // 导航状态标志
  uint8_t flags2;   // 额外标志
  uint8_t numSV;    // 参与定位的卫星数量 ⭐
  int32_t lon;      // 经度 (缩放比例 1e-7) ⭐
  int32_t lat;      // 纬度 (缩放比例 1e-7) ⭐
  int32_t height;   // 椭球高 (mm)
  int32_t hMSL;     // 海拔高度 (mm)
  uint32_t hAcc;    // 水平精度 (mm)
  uint32_t vAcc;    // 垂直精度 (mm)
  int32_t velN;     // 北向速度 (mm/s)
  int32_t velE;     // 东向速度 (mm/s)
  int32_t velD;     // 地向速度 (mm/s)
  int32_t gSpeed;   // 地速 (mm/s) ⭐
  int32_t headMot;  // 运动航向角 (deg * 1e-5)
  uint32_t sAcc;    // 速度精度 (mm/s)
  uint32_t headAcc; // 航向精度 (deg * 1e-5)
  uint16_t pDOP;    // 位置位置因子 (0.01)
  uint8_t flags3;   // 额外标志3
  uint8_t reserved1[5];
} ubx_nav_pvt_t;
#pragma pack(pop)

//

void init_ubx_nona_gps_uart(void);
void ubx_nona_append_checksum(uint8_t *buffer, size_t len);
void gps_configure_ubx_nona_proc(void);

void process_ubx_nona_byte(uint8_t byte);

#endif // ESPWTGPS6N_UBLOXM10NONA_H
