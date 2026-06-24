// 修改 src/gps_drv.c 的开头部分，删除 static const char *TAG 声明：
#include "gps_drv.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "gps_proc.h"
#include "minmea.h"
#include "pm_manager.h"
#include <stdlib.h>
#include <string.h>

// 🟢 已移除 static const char *TAG = "GPS_DRV"; 消除编译警告

static char line_buffer[LINE_BUF_SIZE];
static int line_idx = 0;

static TaskHandle_t xGpsRxTaskHandle = NULL;
static struct minmea_sentence_gga last_gga;
static struct minmea_sentence_rmc last_rmc;
static bool gga_valid = false;
static bool rmc_valid = false;

static void gps_rx_task(void *pvParameters);
static void parse_nmea_sentence(const char *line);
//
// void gps_drv_init(void) {
//     const uart_config_t uart_config = {
//         .baud_rate = GPS_BAUD_RATE,
//         .data_bits = UART_DATA_8_BITS,
//         .parity = UART_PARITY_DISABLE,
//         .stop_bits = UART_STOP_BITS_1,
//         .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
//         .source_clk = UART_SCLK_DEFAULT,
//     };
//     ESP_ERROR_CHECK(uart_param_config(GPS_UART_NUM, &uart_config));
//     ESP_ERROR_CHECK(uart_set_pin(GPS_UART_NUM, GPS_TX_PIN, GPS_RX_PIN,
//     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
//     ESP_ERROR_CHECK(uart_driver_install(GPS_UART_NUM, BUF_SIZE * 2, 0, 0,
//     NULL, 0)); line_idx = 0;
// }

// 确保 src/gps_drv.c 中的初始化函数名字为 gps_drv_init
void gps_drv_init(void) {
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
  line_idx = 0;
}

void gps_drv_deinit(void) {
  uart_driver_delete(GPS_UART_NUM);
  gpio_config_t io_conf = {.pin_bit_mask = (1ULL << GPS_RX_PIN),
                           .mode = GPIO_MODE_INPUT,
                           .pull_up_en = GPIO_PULLUP_DISABLE,
                           .pull_down_en = GPIO_PULLDOWN_DISABLE,
                           .intr_type = GPIO_INTR_DISABLE};
  gpio_config(&io_conf);
}

void gps_drv_create_rx_task(void) {
  xTaskCreatePinnedToCore(gps_rx_task, "gps_rx", 4096, NULL, 5,
                          &xGpsRxTaskHandle, 1);
}

void gps_drv_resume_rx_task(void) {
  if (xGpsRxTaskHandle) {
    xTaskNotifyGive(xGpsRxTaskHandle);
  }
}

// 替换 src/gps_drv.c 中的 gps_rx_task 函数
static void gps_rx_task(void *pvParameters) {
  uint8_t *raw_buf = (uint8_t *)malloc(BUF_SIZE);
  if (raw_buf == NULL) {
    ESP_LOGE("GPS_DRV", "Failed to allocate rx buffer");
    vTaskDelete(NULL);
    return;
  }

  while (1) {
    // 🆕 增强自我锁防线：如果业务被拉低不让运行，先在这里死等复活通知
    if (!g_tasks_should_run) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    // 🆕 双重保险：进入串口前再次检查标志位，防止极速切换时时序偷跑
    if (!g_tasks_should_run) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    int len =
        uart_read_bytes(GPS_UART_NUM, raw_buf, BUF_SIZE - 1, pdMS_TO_TICKS(40));
    if (len < 0) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    for (int i = 0; i < len; i++) {
      char c = raw_buf[i];
      if (c == '\n' || c == '\r') {
        if (line_idx > 0) {
          line_buffer[line_idx] = '\0';
          parse_nmea_sentence(line_buffer);
          line_idx = 0;
        }
      } else {
        if (line_idx < LINE_BUF_SIZE - 1)
          line_buffer[line_idx++] = c;
        else
          line_idx = 0;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  free(raw_buf);
}

// 替换 src/gps_drv.c 中大约第 115 行的 parse_nmea_sentence 函数：
static void parse_nmea_sentence(const char *line) {
  if (!minmea_check(line, false))
    return;
  enum minmea_sentence_id id = minmea_sentence_id(line, false);
  if (id == MINMEA_SENTENCE_GGA) {
    if (minmea_parse_gga(&last_gga, line))
      gga_valid = true;
  } else if (id == MINMEA_SENTENCE_RMC) {
    if (minmea_parse_rmc(&last_rmc, line))
      rmc_valid = true;
  }

  // 当这一秒的 GGA 和 RMC 都到齐后，进行统一判定与打包分发
  if (gga_valid && rmc_valid) {
    gps_msg_t msg;
    memset(&msg, 0, sizeof(gps_msg_t));

    if (last_gga.fix_quality == 0 || !last_rmc.valid) {
      // 状态 A: 数据齐了，但尚未成功定位（搜星中）
      msg.type = GPS_DATA_STATUS;
      snprintf(msg.line1, sizeof(msg.line1), "GPS: 🔴 正在搜索卫星...");
    } else {
      // 状态 B: 数据齐了，且成功捕获有效卫星定位
      msg.type = GPS_DATA_LOCATION;
      int bj_hour = (last_rmc.time.hours + 8) % 24;
      snprintf(msg.line1, sizeof(msg.line1), "时间: %02d:%02d:%02d | 卫星: %d",
               bj_hour, last_rmc.time.minutes, last_rmc.time.seconds,
               last_gga.satellites_tracked);
      snprintf(msg.line2, sizeof(msg.line2), "纬度:%.5f 经度:%.5f",
               minmea_tocoord(&last_gga.latitude),
               minmea_tocoord(&last_gga.longitude));
      float speed_kmh = minmea_tofloat(&last_rmc.speed) * 1.852f;
      snprintf(msg.line3, sizeof(msg.line3), "海拔: %.1f米 | 速度: %.1fkm/h",
               minmea_tofloat(&last_gga.altitude), speed_kmh);
    }

    // 1. 将打包好的消息（无论是状态还是定位）统一推入业务中台队列
    gps_proc_push_data(&msg);
    // 🟢
    // 工业级优化：只有当真正捕获到有效卫星定位（LOCATION）时，才发放通行证允许立刻秒睡！
    // 如果处于 STATUS（搜星中），则不发通行证，主线程将坚守 5 秒超时，给予 GPS
    // 模块充裕的硬件搜星时间。
    if (msg.type == GPS_DATA_LOCATION) {
      pm_give_sleep_permit();
    }

    gga_valid = false;
    rmc_valid = false;
  }
}
