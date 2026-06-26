#include "gps_init.h"
#include "minmea.h"
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

// #define GPS_UART_NUM UART_NUM_2
// #define GPS_BAUD_RATE 115200
// #define BUF_SIZE 1024
// #define GPS_TX_PIN GPIO_NUM_33
// #define GPS_RX_PIN GPIO_NUM_32

#define LINE_BUF_SIZE 128
static char line_buffer[LINE_BUF_SIZE];
static int line_idx = 0;

// --- 队列与信号量句柄 ---
static QueueHandle_t gps_process_queue = NULL;
static QueueHandle_t ssd1306_queue = NULL;
static SemaphoreHandle_t sleep_sem = NULL;

// --- 动态休眠时间全局变量及互斥锁 ---
static uint32_t g_gps_sleep_interval_sec = 30;
static SemaphoreHandle_t sleep_interval_mutex = NULL;

// --- 任务工作状态标志 ---
static volatile bool g_tasks_should_run = true;

// Task 句柄
static TaskHandle_t xGpsRxTaskHandle = NULL;
static TaskHandle_t xGpsProcTaskHandle = NULL;
static TaskHandle_t xSsdTaskHandle = NULL;

typedef enum { GPS_DATA_STATUS, GPS_DATA_LOCATION } gps_msg_type_t;
typedef struct {
  gps_msg_type_t type;
  char line1[LINE_BUF_SIZE];
  char line2[LINE_BUF_SIZE];
  char line3[LINE_BUF_SIZE];
} gps_msg_t;

static struct minmea_sentence_gga last_gga;
static struct minmea_sentence_rmc last_rmc;
static bool gga_valid = false;
static bool rmc_valid = false;

// 线程安全的休眠时间接口
void set_gps_sleep_interval(uint32_t seconds) {
  if (sleep_interval_mutex != NULL) {
    if (xSemaphoreTake(sleep_interval_mutex, portMAX_DELAY) == pdTRUE) {
      g_gps_sleep_interval_sec = seconds;
      ESP_LOGI("CONFIG", "GPS 休眠间隔调整为: %" PRIu32 " 秒", seconds);
      xSemaphoreGive(sleep_interval_mutex);
    }
  }
}

uint32_t get_gps_sleep_interval(void) {
  uint32_t seconds = 30;
  if (sleep_interval_mutex != NULL) {
    if (xSemaphoreTake(sleep_interval_mutex, portMAX_DELAY) == pdTRUE) {
      seconds = g_gps_sleep_interval_sec;
      xSemaphoreGive(sleep_interval_mutex);
    }
  }
  return seconds;
}

static void check_and_push_data(void) {
  gps_msg_t msg;
  memset(&msg, 0, sizeof(gps_msg_t));

  if (!gga_valid || !rmc_valid || last_gga.fix_quality == 0 ||
      !last_rmc.valid) {
    msg.type = GPS_DATA_STATUS;
    snprintf(msg.line1, sizeof(msg.line1), "GPS: 🔴 正在搜索卫星...");
    xQueueSend(gps_process_queue, &msg, 0);
    return;
  }

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

  xQueueSend(gps_process_queue, &msg, portMAX_DELAY);
  xSemaphoreGive(sleep_sem); // 🟢 关键：只有真正组装出 LOCATION
                             // 三行有效数据后，才给释放休眠通行证
}

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
  if (gga_valid && rmc_valid) {
    check_and_push_data();
    gga_valid = false;
    rmc_valid = false;
  }
}

// ================= TASK 1: GPS 串口数据接收 =================
void gps_rx_task(void *pvParameters) {
  uint8_t *raw_buf = (uint8_t *)malloc(BUF_SIZE);
  while (1) {
    if (!g_tasks_should_run) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
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

// ================= TASK 2: GPS 数据分发/中心处理 =================
void gps_data_process_task(void *pvParameters) {
  gps_msg_t received_msg;
  while (1) {
    if (!g_tasks_should_run) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    if (xQueueReceive(gps_process_queue, &received_msg, pdMS_TO_TICKS(500)) ==
        pdTRUE) {
      if (received_msg.type == GPS_DATA_STATUS) {
        printf("\n[GPS STATUS] %s\n", received_msg.line1);
      } else {
        printf("\n====== GPS LOCATION "
               "======\n%s\n%s\n%s\n==========================\n",
               received_msg.line1, received_msg.line2, received_msg.line3);
      }
      xQueueSend(ssd1306_queue, &received_msg, 0);
    }
  }
}

// ================= TASK 3: SSD1306 专门渲染任务 =================
void ssd_display_task(void *pvParameters) {
  gps_msg_t disp_msg;
  while (1) {
    if (!g_tasks_should_run) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    if (xQueueReceive(ssd1306_queue, &disp_msg, pdMS_TO_TICKS(500)) == pdTRUE) {
      ESP_LOGD("OLED", "Screen Buffer Updated");
    }
  }
}

// ================= 主应用流程控制 =================
void app_main(void) {
  esp_log_level_set("*", ESP_LOG_INFO);

  gps_process_queue = xQueueCreate(5, sizeof(gps_msg_t));
  ssd1306_queue = xQueueCreate(5, sizeof(gps_msg_t));
  sleep_sem = xSemaphoreCreateBinary();
  sleep_interval_mutex = xSemaphoreCreateMutex();

  // 首次启动初始化 UART
  init_gps_uart();

  xTaskCreatePinnedToCore(gps_rx_task, "gps_rx", 4096, NULL, 5,
                          &xGpsRxTaskHandle, 1);
  xTaskCreatePinnedToCore(gps_data_process_task, "gps_proc", 3072, NULL, 4,
                          &xGpsProcTaskHandle, 1);
  xTaskCreatePinnedToCore(ssd_display_task, "ssd_task", 3072, NULL, 3,
                          &xSsdTaskHandle, 1);

  while (1) {
    // 【步骤 1】动态加载用户配置的休眠间隔
    uint32_t current_interval = get_gps_sleep_interval();
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)current_interval *
                                                  1000 * 1000));

    ESP_LOGI("MAIN",
             "=== 🟢 ESP32 处于工作状态，正在等待有效 GPS 定位数据刷新... ===");
    fflush(stdout);

    // 【步骤
    // 2】业务逻辑核心等待：阻塞在这里，给子任务充裕的时间去读串口、解析并组装数据
    // 如果 5 秒内还没搜到星或者没拼齐 GGA+RMC，触发超时判定
    if (xSemaphoreTake(sleep_sem, pdMS_TO_TICKS(5000)) == pdFALSE) {
      ESP_LOGW("MAIN", "未能在超时前获取有效定位，发送通知状态...");
      gps_msg_t timeout_msg = {.type = GPS_DATA_STATUS};
      snprintf(timeout_msg.line1, sizeof(timeout_msg.line1),
               "GPS: 🔴 搜星超时，%" PRIu32 "秒后重试", current_interval);
      xQueueSend(gps_process_queue, &timeout_msg, 0);
    }

    // 【步骤
    // 3】时序保护延迟：给业务分发任务（gps_proc）留出充裕的时间把刚才获取的“定位数据”或“超时状态”打印到串口
    vTaskDelay(pdMS_TO_TICKS(80));
    fflush(stdout);

    // 【步骤 4】打印即将卧倒的提示，并再次刷干缓冲区
    ESP_LOGI("MAIN", "准备进入 %" PRIu32 " 秒低功耗休眠模式...",
             current_interval);
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(20)); // 保障上面这行提示能完整显示在屏幕上

    // 【步骤 5】通知所有工作子任务安全就地卧倒
    g_tasks_should_run = false;
    vTaskDelay(pdMS_TO_TICKS(50)); // 等待 50ms 让子任务跳出当前阻塞

    // 【步骤 6】安全卸载 GPS 的 UART2 驱动，完全隔绝休眠期间物理引脚的数据噪声
    deinit_gps_uart();

    // 【步骤
    // 7】触发硬件级轻度休眠（CPU/总线挂起，进入超低功耗，由定时器精确控制 30s
    // 唤醒）
    esp_light_sleep_start();

    // ================= 【30秒后被硬件定时器精确唤醒从此继续】
    // =================

    // 【步骤 8】唤醒第一步：无缝重建并配置 UART2 驱动，清空缓冲指针
    init_gps_uart();
    line_idx = 0;

    // 强刷缓冲区，打出苏醒日志
    ESP_LOGI("MAIN",
             "⏰ ESP32 已被定时器正确唤醒，成功重建 UART，恢复系统运行。");
    fflush(stdout);

    // 【步骤 9】将工作标志位拉回，利用 Task Notification
    // 瞬间“复活”所有人，开始新一轮高效读取
    g_tasks_should_run = true;
    xTaskNotifyGive(xGpsRxTaskHandle);
    xTaskNotifyGive(xGpsProcTaskHandle);
    xTaskNotifyGive(xSsdTaskHandle);
  }
}
