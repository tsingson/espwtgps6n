#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "GPS_PRO";

#define ESP32_RX_FROM_GPS_TX (16)
#define ESP32_TX_TO_GPS_RX   (17)
#define GPS_UART_NUM         (UART_NUM_2)
#define BUF_SIZE             (1024)

void init_gps_uart(void) {
    const uart_config_t uart_config = {
        .baud_rate = 38400,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(GPS_UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(GPS_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(GPS_UART_NUM, ESP32_TX_TO_GPS_RX, ESP32_RX_FROM_GPS_TX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

// 🛡️ 优化点 2：严格的 NMEA 校验和检查
bool verify_nmea_checksum(const char *nmea) {
    if (nmea[0] != '$') return false;

    int sum = 0;
    int i = 1;
    // 异或 '$' 和 '*' 之间的所有字符
    while (nmea[i] != '*' && nmea[i] != '\0') {
        sum ^= nmea[i];
        i++;
    }

    if (nmea[i] == '*') {
        int expected_sum = (int)strtol(&nmea[i + 1], NULL, 16);
        return (sum == expected_sum);
    }
    return false;
}

// 🧭 优化点 4：ddmm.mmmm 格式转换为标准地图的小数度格式
double convert_to_decimal_degrees(const char *str) {
    if (strlen(str) < 5) return 0.0;

    double raw = atof(str);
    int degrees = (int)(raw / 100);
    double minutes = raw - (degrees * 100);
    return degrees + (minutes / 60.0);
}

// 🛡️ 优化点 1：线程安全的 NMEA 解析器
void parse_nmea_gga_pro(char *line) {
    if (!verify_nmea_checksum(line)) {
        // ESP_LOGW(TAG, "校验和错误或数据不完整，已丢弃");
        return;
    }

    if (strncmp(line, "$GNGGA", 6) != 0 && strncmp(line, "$GPGGA", 6) != 0) {
        return;
    }

    char *saveptr;
    // 使用线程安全的 strtok_r
    char *token = strtok_r(line, ",", &saveptr);
    int index = 0;

    double latitude = 0.0, longitude = 0.0;
    int fix_status = 0;
    int sat_num = 0;

    while (token != NULL) {
        if (index == 2) latitude = convert_to_decimal_degrees(token);
        else if (index == 4) longitude = convert_to_decimal_degrees(token);
        else if (index == 6) fix_status = atoi(token);
        else if (index == 7) sat_num = atoi(token);

        token = strtok_r(NULL, ",", &saveptr);
        index++;
    }

    if (fix_status == 0) {
        ESP_LOGI(TAG, "搜星中... 可见卫星数: %d 颗", sat_num);
    } else {
        // ✨ 打印出可以直接复制到谷歌/高德地图里查询的精准坐标
        printf("[GPS 定位成功] 状态: %d | 卫星数: %d | 纬度(N): %.6f | 经度(E): %.6f\n",
               fix_status, sat_num, latitude, longitude);
    }
}

void gps_process_task(void *pvParameters) {
    uint8_t *buffer = (uint8_t *) malloc(BUF_SIZE);
    char line_buf[128];
    int line_idx = 0;

    init_gps_uart();
    ESP_LOGI(TAG, "工业级 GPS 解析驱动已就绪.");

    while (1) {
        // ⭐ 优化点 3：适度降低轮询频率或配合事件，此处使用非阻塞快读
        int len = uart_read_bytes(GPS_UART_NUM, buffer, BUF_SIZE - 1, 10 / portTICK_PERIOD_MS);
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                char c = buffer[i];
                if (c == '\n' || c == '\r') {
                    if (line_idx > 0) {
                        line_buf[line_idx] = '\0';
                        parse_nmea_gga_pro(line_buf);
                        line_idx = 0;
                    }
                } else {
                    if (line_idx < sizeof(line_buf) - 1) {
                        line_buf[line_idx++] = c;
                    }
                }
            }
        }
    }
    free(buffer);
}

void app_main(void) {
    // 分配到独立核心（Core 1），不干扰 Core 0 的协议栈工作
    xTaskCreatePinnedToCore(gps_process_task, "gps_process_task", 4096, NULL, 10, NULL, 1);
}
