#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "WT_GPS_6N";

// Define UART parameters
#define GPS_UART_NUM UART_NUM_2
#define GPS_BAUD_RATE 115200 // 9600
#define BUF_SIZE 1024

#define GPS_TX_PIN GPIO_NUM_33 // 对应板上标注的 D33，连接 GPS 的 RX
#define GPS_RX_PIN GPIO_NUM_32 // 对应板上标注的 D32，连接 GPS 的 TX

void init_gps_uart(void) {
  const uart_config_t uart_config = {
      .baud_rate = GPS_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  // Configure UART2 parameters
  ESP_ERROR_CHECK(uart_param_config(GPS_UART_NUM, &uart_config));

  // Set UART2 pins (TX, RX, RTS, CTS)
  ESP_ERROR_CHECK(uart_set_pin(GPS_UART_NUM, GPS_TX_PIN, GPS_RX_PIN,
                               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

  // Install UART driver with Rx buffer, no Tx buffer, no event queue
  ESP_ERROR_CHECK(
      uart_driver_install(GPS_UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));

  ESP_LOGI(TAG, "UART2 successfully initialized at %d baud.", GPS_BAUD_RATE);
}


/**
void gps_rx_task(void *pvParameters) {
    uint8_t *data = (uint8_t *) malloc(BUF_SIZE);
    if (data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for UART buffer.");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Starting NMEA stream reader...");

    while (1) {
        // Read data from WT-GPS-6N
        int len = uart_read_bytes(GPS_UART_NUM, data, BUF_SIZE - 1,
pdMS_TO_TICKS(100));

        if (len > 0) {
            data[len] = '\0'; // Null-terminate the string

            // Process the text line by line to extract standard NMEA data
            char *line = strtok((char *)data, "\r\n");
            while (line != NULL) {
                // Look for common NMEA sentences
                if (strstr(line, "$GNGGA") != NULL) {
                    ESP_LOGI(TAG, "[GGA Sentence - Global Positioning System Fix
Data]"); printf("%s\n", line); } else if (strstr(line, "$GNRMC") != NULL) {
                    ESP_LOGI(TAG, "[RMC Sentence - Recommended Minimum
Navigation Data]"); printf("%s\n", line); } else if (strstr(line, "$GN") !=
NULL) {
                    // Print any other valid multi-GNSS sentences (GSA, GSV, VTG
etc.) printf("%s\n", line);
                }
                line = strtok(NULL, "\r\n");
            }
        }
        // Yield to feed the FreeRTOS watchdog
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    free(data);
    vTaskDelete(NULL);
}

 */

void gps_rx_task(void *pvParameters) {
  uint8_t *data = (uint8_t *)malloc(BUF_SIZE);
  while (1) {
    // 无条件读取串口缓冲区
    int len =
        uart_read_bytes(GPS_UART_NUM, data, BUF_SIZE - 1, pdMS_TO_TICKS(100));
    if (len > 0) {
      data[len] = '\0';
      // 不做任何字符过滤，直接强行打印原始流
      printf("%s", (char *)data);
      fflush(stdout);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void app_main(void) {
  // 关键修复：加入延时以等待系统完全稳定，抑制上电乱码
  vTaskDelay(pdMS_TO_TICKS(500));

  // 强制将所有标签的日志输出级别调至最低的 INFO，防止被 menuconfig 过滤
  esp_log_level_set("*", ESP_LOG_INFO);

  // 强制先打印一句最基础的纯文本，不走日志框架，用来测试串口是否活着
  printf("\n--- ESP32 应用程序已成功启动 ---\n");
  ESP_LOGI("BOOT", "开始初始化 GPS 串口...");

  // 初始化 UART (此时已修改为 GPIO25/26)
  init_gps_uart();

  // 创建任务
  xTaskCreatePinnedToCore(gps_rx_task, "gps_rx_task", 4096, NULL, 5, NULL, 1);
}
