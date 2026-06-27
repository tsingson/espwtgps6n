#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ubloxm10nona.h"



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
