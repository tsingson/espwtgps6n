#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "minmea.h"

static const char *TAG = "WT_GPS_6N";

#define GPS_UART_NUM   UART_NUM_2
#define GPS_BAUD_RATE  115200
#define BUF_SIZE       1024
#define GPS_TX_PIN     GPIO_NUM_33
#define GPS_RX_PIN     GPIO_NUM_32

#define LINE_BUF_SIZE  128
static char line_buffer[LINE_BUF_SIZE];
static int line_idx = 0;

// --- 队列与信号量句柄 ---
static QueueHandle_t gps_process_queue = NULL;
static QueueHandle_t ssd1306_queue = NULL;
static SemaphoreHandle_t sleep_sem = NULL;

// --- 动态休眠时间全局变量及互斥锁 ---
static uint32_t g_gps_sleep_interval_sec = 30;
static SemaphoreHandle_t sleep_interval_mutex = NULL;

// --- 🆕 任务工作状态标志（代替粗暴的 Suspend） ---
static volatile bool g_tasks_should_run = true;

// Task 句柄
static TaskHandle_t xGpsRxTaskHandle = NULL;
static TaskHandle_t xGpsProcTaskHandle = NULL;
static TaskHandle_t xSsdTaskHandle = NULL;

// 数据结构定义
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
    ESP_ERROR_CHECK(uart_set_pin(GPS_UART_NUM, GPS_TX_PIN, GPS_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(GPS_UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0));
}

void deinit_gps_uart(void) {
    uart_driver_delete(GPS_UART_NUM);
    // 将 RX 引脚配置为无上下拉输入，彻底隔绝物理信号和电流倒灌
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPS_RX_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);
}

static void check_and_push_data(void) {
    gps_msg_t msg;
    memset(&msg, 0, sizeof(gps_msg_t));

    if (!gga_valid || !rmc_valid || last_gga.fix_quality == 0 || !last_rmc.valid) {
        msg.type = GPS_DATA_STATUS;
        snprintf(msg.line1, sizeof(msg.line1), "GPS: 🔴 正在搜索卫星...");
        xQueueSend(gps_process_queue, &msg, 0);
        return;
    }

    msg.type = GPS_DATA_LOCATION;

    int bj_hour = (last_rmc.time.hours + 8) % 24;
    snprintf(msg.line1, sizeof(msg.line1), "时间: %02d:%02d:%02d | 卫星: %d",
             bj_hour, last_rmc.time.minutes, last_rmc.time.seconds, last_gga.satellites_tracked);

    snprintf(msg.line2, sizeof(msg.line2), "纬度:%.5f 经度:%.5f",
             minmea_tocoord(&last_gga.latitude), minmea_tocoord(&last_gga.longitude));

    float speed_kmh = minmea_tofloat(&last_rmc.speed) * 1.852f;
    snprintf(msg.line3, sizeof(msg.line3), "海拔: %.1f米 | 速度: %.1fkm/h",
             minmea_tofloat(&last_gga.altitude), speed_kmh);

    xQueueSend(gps_process_queue, &msg, portMAX_DELAY);
    xSemaphoreGive(sleep_sem);
}

static void parse_nmea_sentence(const char *line) {
    if (!minmea_check(line, false)) return;
    enum minmea_sentence_id id = minmea_sentence_id(line, false);
    if (id == MINMEA_SENTENCE_GGA) {
        if (minmea_parse_gga(&last_gga, line)) gga_valid = true;
    } else if (id == MINMEA_SENTENCE_RMC) {
        if (minmea_parse_rmc(&last_rmc, line)) rmc_valid = true;
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
        // 🆕 优雅睡眠机制：收到不工作指令时，主动等在通知信号量上，不占用任何 CPU
        if (!g_tasks_should_run) {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }

        int len = uart_read_bytes(GPS_UART_NUM, raw_buf, BUF_SIZE - 1, pdMS_TO_TICKS(40));
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
                if (line_idx < LINE_BUF_SIZE - 1) line_buffer[line_idx++] = c;
                else line_idx = 0;
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
        // 🆕 优雅睡眠机制
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
            xQueueSend(ssd1306_queue, &received_msg, 0);
        }
    }
}

// ================= TASK 3: SSD1306 专门渲染任务 =================
void ssd_display_task(void *pvParameters) {
    gps_msg_t disp_msg;
    while (1) {
        // 🆕 优雅睡眠机制
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

    init_gps_uart();

    xTaskCreatePinnedToCore(gps_rx_task, "gps_rx", 4096, NULL, 5, &xGpsRxTaskHandle, 1);
    xTaskCreatePinnedToCore(gps_data_process_task, "gps_proc", 3072, NULL, 4, &xGpsProcTaskHandle, 1);
    xTaskCreatePinnedToCore(ssd_display_task, "ssd_task", 3072, NULL, 3, &xSsdTaskHandle, 1);

    while (1) {
        uint32_t current_interval = get_gps_sleep_interval();
        ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)current_interval * 1000 * 1000));

        ESP_LOGI("MAIN", "=== 正在等待有效的 GPS 数据刷新 (当前配置间隔: %" PRIu32 "秒)... ===", current_interval);

        // 5秒防卡死超时
        if (xSemaphoreTake(sleep_sem, pdMS_TO_TICKS(5000)) == pdFALSE) {
            ESP_LOGW("MAIN", "未能在超时前获取有效定位，发送通知并准备休眠");
            gps_msg_t timeout_msg = { .type = GPS_DATA_STATUS };
            snprintf(timeout_msg.line1, sizeof(timeout_msg.line1), "GPS: 🔴 搜星超时，%" PRIu32 "秒后重试", current_interval);
            xQueueSend(gps_process_queue, &timeout_msg, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        ESP_LOGI("MAIN", "准备进入 %" PRIu32 " 秒低功耗休眠模式...", current_interval);
        vTaskDelay(pdMS_TO_TICKS(100));



        // 1. 通知子任务安全卧倒
        g_tasks_should_run = false;
        vTaskDelay(pdMS_TO_TICKS(50)); // 给 50ms 时间让各个 Task 进入卧倒状态

        // 2. 🆕 关键修复：强制等待主串口（UART_NUM_0）将所有日志完全吐完，防止截断
        // pdMS_TO_TICKS(100) 表示最多等待 100 毫秒，超时会自动跳出，绝对不卡死
        uart_wait_tx_done(UART_NUM_0, pdMS_TO_TICKS(100));

        // 3. 此时没有任何任务占用 UART 或队列锁，彻底销毁 GPS 的 UART2 驱动
        deinit_gps_uart();

        // 4. 启动轻度休眠（CPU与总线停止，100%可被硬件定时器唤醒）
        esp_light_sleep_start();


        // 1. 通知子任务安全卧倒
        g_tasks_should_run = false;
        vTaskDelay(pdMS_TO_TICKS(50));

        // 2. 🟢 强行刷新系统的标准输出缓冲区（将 printf 里的数据立刻推给外设硬件）
        fflush(stdout);

        // 3. 🟢 严谨的硬件延迟：在 115200 波特率下，发送一个字符只需 86 微秒。
        // 我们刚刚打印的数据最多只有几十个字节，15毫秒的延迟足够硬件 FIFO
        // 100% 把所有电平信号全部安全、完整地发送到物理引脚上。
        vTaskDelay(pdMS_TO_TICKS(15));

        // 4. 彻底销毁 GPS 的 UART2 驱动
        deinit_gps_uart();

        // 5. 启动轻度休眠
        esp_light_sleep_start();







        // --- 【被唤醒后自动从此处恢复执行】 ---

        // 4. 唤醒后第一件事，重建 UART
        init_gps_uart();
        line_idx = 0;

        ESP_LOGI("MAIN", "🟢 ESP32 已被定时器正确唤醒，成功重建 UART。");

        // 🆕 5. 恢复工作标志，并通过 Task Notification 唤醒所有人
        g_tasks_should_run = true;
        xTaskNotifyGive(xGpsRxTaskHandle);
        xTaskNotifyGive(xGpsProcTaskHandle);
        xTaskNotifyGive(xSsdTaskHandle);
    }
}
