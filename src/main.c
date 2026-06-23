#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

// 引入 minmea 头文件
#include "minmea.h"

static const char *TAG = "WT_GPS_6N";

#define GPS_UART_NUM UART_NUM_2
#define GPS_BAUD_RATE 115200
#define BUF_SIZE 1024

#define GPS_TX_PIN GPIO_NUM_33
#define GPS_RX_PIN GPIO_NUM_32

// 行缓冲区：最大容纳 128 字节的一行 NMEA 数据
#define LINE_BUF_SIZE 128
static char line_buffer[LINE_BUF_SIZE];
static int line_idx = 0;

void init_gps_uart(void) {
  const uart_config_t uart_config = {
      .baud_rate = GPS_BAUD_RATE,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  ESP_ERROR_CHECK(uart_param_config(GPS_UART_NUM, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(GPS_UART_NUM, GPS_TX_PIN, GPS_RX_PIN,
                               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
  ESP_ERROR_CHECK(
      uart_driver_install(GPS_UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));
  ESP_LOGI(TAG, "UART2 successfully initialized at %d baud.", GPS_BAUD_RATE);
}

// 核心解析函数
static void parse_nmea_sentence(const char *line) {
  // 1. 验证 NMEA 校验和 (必选，剔除乱码)
  if (!minmea_check(line, false)) {
    return;
  }

  enum minmea_sentence_id id = minmea_sentence_id(line, false);
  switch (id) {
  case MINMEA_SENTENCE_GGA: {
    struct minmea_sentence_gga frame;
    if (minmea_parse_gga(&frame, line)) {
      // 转换经纬度为标准的十进制浮点数
      float lat = minmea_tocoord(&frame.latitude);
      float lon = minmea_tocoord(&frame.longitude);
      float alt = minmea_tofloat(&frame.altitude);

      ESP_LOGI(TAG, "--- [GGA 定位数据] ---");
      if (frame.fix_quality > 0) {
        printf("  状态: 🟢 已定位 (模式:%d) | 卫星数: %d | 精度(HDOP): %.2f\n",
               frame.fix_quality, frame.satellites_tracked,
               minmea_tofloat(&frame.hdop));
        printf("  坐标: 纬度 %.6f° , 经度 %.6f°\n", lat, lon);
        printf("  海拔: %.1f 米\n", alt);
      } else {
        printf("  状态: 🔴 正在搜索卫星...\n");
      }
    }
    break;
  }
  case MINMEA_SENTENCE_RMC: {
    struct minmea_sentence_rmc frame;
    if (minmea_parse_rmc(&frame, line)) {
      ESP_LOGI(TAG, "--- [RMC 导航最小数据] ---");
      if (frame.valid) {
        // 转换速度（节 -> km/h）
        float speed_kmh = minmea_tofloat(&frame.speed) * 1.852f;

        // 打印北京时间 (+8小时)
        int hour = (frame.time.hours + 8) % 24;
        printf("  时间: 20%02d-%02d-%02d %02d:%02d:%02d (北京时间)\n",
               frame.date.year, frame.date.month, frame.date.day, hour,
               frame.time.minutes, frame.time.seconds);
        printf("  速度: %.2f km/h | 航向: %.1f°\n", speed_kmh,
               minmea_tofloat(&frame.course));
      } else {
        printf("  状态: 🔴 数据暂未生效\n");
      }
    }
    break;
  }
  default:
    break; // 忽略不需要的语句（如 GSV, GSA）
  }
}

void gps_rx_task(void *pvParameters) {
  uint8_t *raw_buf = (uint8_t *)malloc(BUF_SIZE);
  if (raw_buf == NULL) {
    ESP_LOGE(TAG, "Failed to allocate memory for UART buffer.");
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(TAG, "Starting NMEA stream reader...");

  while (1) {
    // 从串口读取字节流
    int len =
        uart_read_bytes(GPS_UART_NUM, raw_buf, BUF_SIZE - 1, pdMS_TO_TICKS(50));

    for (int i = 0; i < len; i++) {
      char c = raw_buf[i];

      // 数据拼装与按行切分机制（解决断帧隐患）
      if (c == '\n' || c == '\r') {
        if (line_idx > 0) {
          line_buffer[line_idx] = '\0';
          parse_nmea_sentence(line_buffer); // 交给解析器
          line_idx = 0;                     // 重置行缓冲区
        }
      } else {
        if (line_idx < LINE_BUF_SIZE - 1) {
          line_buffer[line_idx++] = c;
        } else {
          line_idx = 0; // 缓冲区溢出保护，丢弃过长坏行
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  free(raw_buf);
  vTaskDelete(NULL);
}

void app_main(void) {
  vTaskDelay(pdMS_TO_TICKS(500));
  esp_log_level_set("*", ESP_LOG_INFO);

  printf("\n--- ESP32 GPS 智能解析系统已启动 ---\n");
  init_gps_uart();

  xTaskCreatePinnedToCore(gps_rx_task, "gps_rx_task", 4096, NULL, 5, NULL, 1);
}
