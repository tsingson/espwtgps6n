# u-blox m10 nona 飞控 GPS 模块(自带天线)

```c++
//
// Created by tsingson on 2026/6/26.
//

#include "sdkconfig.h"

#ifdef CONFIG_IDF_TARGET_ESP32
// ESP32 经典款引脚定义
#define PIN_I2C_SDA 21
#define PIN_I2C_SCL 22
#define PIN_GPS_TX 17
#define PIN_GPS_RX 16
#define PIN_4G_TX 25
#define PIN_4G_RX 26
#elif defined CONFIG_IDF_TARGET_ESP32C3
// ESP32-C3 引脚定义
#define PIN_I2C_SDA 4
#define PIN_I2C_SCL 5
#define PIN_GPS_TX 6
#define PIN_GPS_RX 7
#define PIN_4G_TX 18
#define PIN_4G_RX 19
#else
#error "未知的目标芯片类型"
#endif

#include "ubloxm10nona.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==============================================================================
// 6. 系统任务入口与主线程
// ==============================================================================
void gps_ubx_task(void *pvParameters) {
  uint8_t *buffer = (uint8_t *)malloc(NONA_BUF_SIZE);
  if (buffer == NULL) {
    ESP_LOGE(TAG, "任务内存分配失败");
    vTaskDelete(NULL);
  }

  init_ubx_nona_gps_uart();
  vTaskDelay(pdMS_TO_TICKS(500)); // 留出时间等待硬件和驱动层完全就绪

  gps_configure_ubx_nona_proc(); // 执行动态产品化配置

  while (1) {
    // 5Hz 数据流读取要求极低的响应延迟，阻塞等待超时设为 10ms
    int len = uart_read_bytes(GPS_UART_NUM, buffer, NONA_BUF_SIZE - 1,
                              10 / portTICK_PERIOD_MS);
    if (len > 0) {
      for (int i = 0; i < len; i++) {
        process_ubx_nona_byte(buffer[i]); // 字节流不间断喂给状态机解析
      }
    }
  }
  free(buffer);
}

void app_main(void) {
  // 独立分配到核心 1 运行，使其完全脱离核心 0 的 Wi-Fi
  // 协议栈调度，保障高频串口的高实时性
  xTaskCreatePinnedToCore(gps_ubx_task, "gps_ubx_task", 4096, NULL, 10, NULL,
                          1);
}

```
