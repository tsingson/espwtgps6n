#include "gps_proc.h"
#include "pm_manager.h"
#include "esp_log.h"
#include <string.h>
#include <inttypes.h>

static const char *TAG = "GPS_PROC";

static QueueHandle_t gps_process_queue = NULL;
QueueHandle_t g_ssd1306_queue = NULL;
static SemaphoreHandle_t sleep_interval_mutex = NULL;
static uint32_t g_gps_sleep_interval_sec = 30;

static TaskHandle_t xGpsProcTaskHandle = NULL;
static TaskHandle_t xSsdTaskHandle = NULL;

static void gps_data_process_task(void *pvParameters);
static void ssd_display_task(void *pvParameters);

void set_gps_sleep_interval(uint32_t seconds) {
    if (sleep_interval_mutex && xSemaphoreTake(sleep_interval_mutex, portMAX_DELAY) == pdTRUE) {
        g_gps_sleep_interval_sec = seconds;
        ESP_LOGI("CONFIG", "GPS 休眠间隔在线更新为: %" PRIu32 " 秒", seconds);
        xSemaphoreGive(sleep_interval_mutex);
    }
}

uint32_t get_gps_sleep_interval(void) {
    uint32_t seconds = 30;
    if (sleep_interval_mutex && xSemaphoreTake(sleep_interval_mutex, portMAX_DELAY) == pdTRUE) {
        seconds = g_gps_sleep_interval_sec;
        xSemaphoreGive(sleep_interval_mutex);
    }
    return seconds;
}

void gps_proc_create_tasks(void) {
    gps_process_queue = xQueueCreate(5, sizeof(gps_msg_t));
    g_ssd1306_queue = xQueueCreate(5, sizeof(gps_msg_t));
    sleep_interval_mutex = xSemaphoreCreateMutex();

    xTaskCreatePinnedToCore(gps_data_process_task, "gps_proc", 3072, NULL, 4, &xGpsProcTaskHandle, 1);
    xTaskCreatePinnedToCore(ssd_display_task, "ssd_task", 3072, NULL, 3, &xSsdTaskHandle, 1);
}

void gps_proc_resume_tasks(void) {
    if (xGpsProcTaskHandle) xTaskNotifyGive(xGpsProcTaskHandle);
    if (xSsdTaskHandle) xTaskNotifyGive(xSsdTaskHandle);
}

void gps_proc_push_data(gps_msg_t *msg) {
    if (gps_process_queue) {
        xQueueSend(gps_process_queue, msg, portMAX_DELAY);
    }
}

void gps_proc_push_timeout_msg(uint32_t interval) {
    gps_msg_t timeout_msg = { .type = GPS_DATA_STATUS };
    snprintf(timeout_msg.line1, sizeof(timeout_msg.line1), "GPS: 🔴 搜星超时，%" PRIu32 "秒后重试", interval);
    gps_proc_push_data(&timeout_msg);
}

void gps_proc_flush_console_buffer(void) {
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(30)); // 优雅确保移位寄存器排空字符
}

static void gps_data_process_task(void *pvParameters) {
    gps_msg_t received_msg;
    while (1) {
        if (!g_tasks_should_run) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
        if (xQueueReceive(gps_process_queue, &received_msg, pdMS_TO_TICKS(500)) == pdTRUE) {
            if (received_msg.type == GPS_DATA_STATUS) {
                printf("\n[GPS STATUS] %s\n", received_msg.line1);
            } else {
                printf("\n====== GPS LOCATION ======\n%s\n%s\n%s\n==========================\n",
                       received_msg.line1, received_msg.line2, received_msg.line3);
            }
            if (g_ssd1306_queue) {
                xQueueSend(g_ssd1306_queue, &received_msg, 0);
            }
        }
    }
}

static void ssd_display_task(void *pvParameters) {
    gps_msg_t disp_msg;
    while (1) {
        if (!g_tasks_should_run) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
        if (xQueueReceive(g_ssd1306_queue, &disp_msg, pdMS_TO_TICKS(500)) == pdTRUE) {
            // 对接真实 SSD1306 OLED 液晶渲染接口
            ESP_LOGD(TAG, "SSD1306 Queue consumed");
        }
    }
}
