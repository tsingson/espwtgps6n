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

#elif defined CONFIG_IDF_TARGET_ESP32C6
// ESP32-C6 引脚定义 (针对标准 DevKitC 进行工业级优化)
#define PIN_I2C_SDA 6   // LP_I2C / Generic I2C SDA 可复用引脚
#define PIN_I2C_SCL 7   // LP_I2C / Generic I2C SCL 可复用引脚
#define PIN_GPS_TX  16  // UART1 TX for GPS Stream
#define PIN_GPS_RX  17  // UART1 RX for GPS Stream
#define PIN_4G_TX   20  // UART2 TX / High-speed peripheral for 4G module
#define PIN_4G_RX   21  // UART2 RX / High-speed peripheral for 4G module

#else
#error "未知的目标芯片类型"
#endif



#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gps_ring_buffer.h"
#include "oled_ssd1306.h"
#include "ubloxm10nona.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ==============================================================================
// 6. 系统任务入口与主线程
// ==============================================================================
void process_ubx_nona_byte_ring(uint8_t byte) {
  gps_location_t fake_gps;

  static enum {
    STATE_IDLE,
    STATE_SYNC2,
    STATE_CLASS,
    STATE_ID,
    STATE_LEN1,
    STATE_LEN2,
    STATE_PAYLOAD,
    STATE_CKA,
    STATE_CKB
  } state = STATE_IDLE;

  static uint8_t u_class, u_id;
  static uint16_t payload_len, payload_idx;
  static uint8_t payload_buf[256];
  static uint8_t ck_a, ck_b;
  static uint8_t calc_ck_a, calc_ck_b;

  switch (state) {
  case STATE_IDLE:
    if (byte == UBX_SYNC_CHAR_1)
      state = STATE_SYNC2;
    break;
  case STATE_SYNC2:
    state = (byte == UBX_SYNC_CHAR_2) ? STATE_CLASS : STATE_IDLE;
    break;
  case STATE_CLASS:
    u_class = byte;
    calc_ck_a = byte;
    calc_ck_b = byte; // 复位 Fletcher 校验
    state = STATE_ID;
    break;
  case STATE_ID:
    u_id = byte;
    calc_ck_a += byte;
    calc_ck_b += calc_ck_a;
    state = STATE_LEN1;
    break;
  case STATE_LEN1:
    payload_len = byte;
    calc_ck_a += byte;
    calc_ck_b += calc_ck_a;
    state = STATE_LEN2;
    break;
  case STATE_LEN2:
    payload_len |= ((uint16_t)byte << 8);
    calc_ck_a += byte;
    calc_ck_b += calc_ck_a;
    payload_idx = 0;
    // 长度防御性限制，防止恶意长数据包撑爆本地 RAM 缓冲区
    state = (payload_len > 0 && payload_len < sizeof(payload_buf))
                ? STATE_PAYLOAD
                : STATE_IDLE;
    break;
  case STATE_PAYLOAD:
    payload_buf[payload_idx++] = byte;
    calc_ck_a += byte;
    calc_ck_b += calc_ck_a;
    if (payload_idx >= payload_len)
      state = STATE_CKA;
    break;
  case STATE_CKA:
    ck_a = byte;
    state = STATE_CKB;
    break;
  case STATE_CKB:
    ck_b = byte;
    state = STATE_IDLE; // 本帧结束，状态机复位

    // 严苛的端到端数据校验
    if (ck_a == calc_ck_a && ck_b == calc_ck_b) {
      // 成功捕获高频综合导航包 (Class: 0x01, ID: 0x07 -> UBX-NAV-PVT)
      if (u_class == 0x01 && u_id == 0x07) {
        ubx_nav_pvt_t *pvt = (ubx_nav_pvt_t *)payload_buf;

        fake_gps.fixType = pvt->fixType;
        fake_gps.numSV = pvt->numSV;
        fake_gps.lat = pvt->lat;
        fake_gps.lon = pvt->lon;
        fake_gps.gSpeed = pvt->gSpeed;

        gps_rb_push_overwrite(&fake_gps);
      }
    }
    break;
  }
}
// ==============================================================================
//
// ==============================================================================

// Consumer Task: Simulates disappearing for 2s, then catches up aggressively
void vConsumerTask(void *pvParameters) {
  gps_location_t received_data;

  char star[32] = {0};
  char tp[32] = {0};
  char latt[32] = {0};

  char lonn[32] = {0};
  char spt[32] = {0};
  //

  ESP_LOGW(TAG, "[CONSUMER] Simulating Disconnected Status (Sleep 2s)...");
  vTaskDelay(pdMS_TO_TICKS(2000)); // 2-second sleep forces buffer overruns
  ESP_LOGI(TAG, "[CONSUMER] Now Online! Starting to consume data...");

  while (1) {
    if (gps_rb_pop(&received_data) == pdTRUE) {
      // ESP_LOGE(
      //     TAG,
      //     "    -> [CONSUMER] Pop Success! Lat:%ld | SVs:%u | Remaining:%lu",
      //     (long)received_data.lat, (unsigned int)received_data.numSV,
      //     (unsigned long)gps_rb_get_unread_count());

      double lat = received_data.lat / 10000000.0;
      double lon = received_data.lon / 10000000.0;
      double speed_kh =
          (received_data.gSpeed / 1000.0) * 3.6; // 从 mm/s 换算为 km/h

      printf("[UBX 5Hz 高频解算] 卫星: %d | 定位类型: %d | 纬度: %.7f | "
             "经度: %.7f | 地速: %.2f km/h\n",
             received_data.numSV, received_data.fixType, lat, lon, speed_kh);

      oled_clear();

      snprintf(star, sizeof(star), "star:%d", received_data.numSV);
      oled_show_string(0, 0, star);

      snprintf(tp, sizeof(tp), "type:%d", received_data.fixType);
      oled_show_string(0, 13, tp);

      snprintf(latt, sizeof(latt), "lat:%.7f", lat);
      oled_show_string(0, 26, latt);

      snprintf(lonn, sizeof(lonn), "kib:%.7f", lon);
      oled_show_string(0, 39, lonn);

      snprintf(spt, sizeof(spt), "soeed:%.3f km/h", speed_kh);
      oled_show_string(0, 52, spt);

      oled_refresh();

      // Fast loop handling interval when resolving backlogged elements
      vTaskDelay(pdMS_TO_TICKS(1));
    } else {
      oled_clear();
      // Buffer empty, catch-up achieved, enter relaxed polling mode
      ESP_LOGW(
          TAG,
          "    -> [CONSUMER] Buffer fully cleared. Waiting for new data...");
      vTaskDelay(pdMS_TO_TICKS(200));
    }
  }
}

//

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
        //     process_ubx_nona_byte(buffer[i]); // 字节流不间断喂给状态机解析
        process_ubx_nona_byte_ring(buffer[i]); // 字节流不间断喂给状态机解析
      }
    }
  }
  free(buffer);
}

void app_main(void) {
  vTaskDelay(pdMS_TO_TICKS(200));

  if (oled_init(PIN_I2C_SCL, PIN_I2C_SDA) != ESP_OK) {
    ESP_LOGE(TAG, "OLED Core Engine Init Failed!");
    return;
  }
  {
    oled_clear();
    oled_show_string_ex(0, 0, "GPS ublox m10", 0);
    oled_show_string_ex(0, 20, "initial...", 0);

    oled_refresh();

    vTaskDelay(pdMS_TO_TICKS(200));
  }
  // 独立分配到核心 1 运行，使其完全脱离核心 0 的 Wi-Fi
  // 协议栈调度，保障高频串口的高实时性
  xTaskCreatePinnedToCore(gps_ubx_task, "gps_ubx_task", 4096, NULL, 10, NULL,
                          1);
  xTaskCreate(vConsumerTask, "ConsumerTask", 3072, NULL, 4, NULL);
}
