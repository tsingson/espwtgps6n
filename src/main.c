#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

// 硬件配置
#define GPS_UART_NUM UART_NUM_2
#define GPS_BAUD_RATE                                                          \
  115200 // 9600  // WT-GPS-6N 默认通常为 9600，若无数据请改为 115200
#define BUF_SIZE (1024)
#define PIN_GPS_TX 17 // gps tx0 ---> esp32 Rx2
#define PIN_GPS_RX 16 // gps rx0 ----> esp32 tx2

#include "driver/uart.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "GPS_DRV";

// GPS 初始化函数
void gps_init(int MY_PIN_GPS_TX, int MY_PIN_GPS_RX, int MY_GPS_UART_NUM,
              int MY_GPS_BAUD_RATE) {
  uart_config_t uart_config = {
      .baud_rate = MY_GPS_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };

  ESP_ERROR_CHECK(
      uart_driver_install(MY_GPS_UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));
  ESP_ERROR_CHECK(uart_param_config(MY_GPS_UART_NUM, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(MY_GPS_UART_NUM, MY_PIN_GPS_TX, MY_PIN_GPS_RX,
                               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

  ESP_LOGI(TAG, "GPS UART initialized.");
}

// 发送指令函数
void gps_send_cmd(const char *cmd) {
  uart_write_bytes(GPS_UART_NUM, cmd, strlen(cmd));
  uart_write_bytes(GPS_UART_NUM, "\r\n", 2);
}

// 进入休眠模式 (PMTK 指令)
void gps_enter_sleep(void) {
  // PMTK161: Standby Mode (进入待机)
  // 模块收到此指令后会停止输出数据并进入低功耗，直到下次收到串口数据唤醒
  const char *sleep_cmd = "$PMTK161,0*28";
  gps_send_cmd(sleep_cmd);
  ESP_LOGI(TAG, "GPS sleep command sent.");
}

void gps_app_main(void) {
  // 1. 配置 UART
  //   uart_config_t uart_config = {
  //     .baud_rate = GPS_BAUD_RATE,
  //     .data_bits = UART_DATA_8_BITS,
  //     .parity    = UART_PARITY_DISABLE,
  //     .stop_bits = UART_STOP_BITS_1,
  //     .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
  //     .source_clk = UART_SCLK_DEFAULT,
  // };
  //
  //   // 2. 安装驱动
  //   ESP_ERROR_CHECK(uart_driver_install(GPS_UART_NUM, BUF_SIZE * 2, 0, 0,
  //   NULL, 0)); ESP_ERROR_CHECK(uart_param_config(GPS_UART_NUM,
  //   &uart_config)); ESP_ERROR_CHECK(uart_set_pin(GPS_UART_NUM, PIN_GPS_TX,
  //   PIN_GPS_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

  gps_init(PIN_GPS_TX, PIN_GPS_RX, GPS_UART_NUM, GPS_BAUD_RATE);

  ESP_LOGI(TAG, "GPS UART initialized. Baudrate: %d", GPS_BAUD_RATE);

  // 3. 循环读取并打印
  uint8_t *data = (uint8_t *)malloc(BUF_SIZE);
  while (1) {
    // 读取串口数据，超时设置为 100ms
    int len =
        uart_read_bytes(GPS_UART_NUM, data, BUF_SIZE - 1, pdMS_TO_TICKS(100));
    if (len > 0) {
      data[len] = '\0'; // 结束符
      // 直接透传到 ESP32 自带的调试串口 (通常是 USB 虚拟串口或 UART0)
      printf("%s", (char *)data);
    }
  }
  free(data);
}

void app_main(void) { gps_app_main(); }
