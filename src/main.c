#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "GPS_APP";

#define ESP32_RX_FROM_GPS_TX (16)
#define ESP32_TX_TO_GPS_RX   (17)
#define GPS_UART_NUM         (UART_NUM_2)
#define BUF_SIZE             (1024)

void init_gps_uart(void) {
    const uart_config_t uart_config = {
        .baud_rate = 38400, // ⭐ 锁定黄金波特率 38400
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

// 简单的 NMEA $GNGGA 语句极简解析器
void parse_nmea_gga(char *line) {
    // $GNGGA,时间,纬度,N/S,经度,E/W,质量(0=未定位,1=GPS,2=DGPS),卫星数,...
    if (strncmp(line, "$GNGGA", 6) == 0 || strncmp(line, "$GPGGA", 6) == 0) {
        char *token;
        char *search = ",";
        int index = 0;

        char lat[15] = "0.0";
        char lon[15] = "0.0";
        char fix_status[2] = "0";
        char sat_num[3] = "00";

        token = strtok(line, search);
        while (token != NULL) {
            if (index == 2) strcpy(lat, token);          // 纬度
            else if (index == 4) strcpy(lon, token);     // 经度
            else if (index == 6) strcpy(fix_status, token); // 定位状态
            else if (index == 7) strcpy(sat_num, token); // 卫星数量

            token = strtok(NULL, search);
            index++;
        }

        if (fix_status[0] == '0') {
            printf("[GPS 状态] ❌ 未定位 | 正在搜星... | 当前可见卫星数: %s\n", sat_num);
        } else {
            printf("[GPS 状态]  已定位! | 纬度: %s | 经度: %s | 参与定位卫星数: %s\n", lat, lon, sat_num);
        }
    }
}

void gps_process_task(void *pvParameters) {
    uint8_t *buffer = (uint8_t *) malloc(BUF_SIZE);
    char line_buf[128];
    int line_idx = 0;

    init_gps_uart();
    ESP_LOGI(TAG, "GPS 业务解析驱动已就绪 (38400 bps).");

    while (1) {
        int len = uart_read_bytes(GPS_UART_NUM, buffer, BUF_SIZE - 1, 20 / portTICK_PERIOD_MS);
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                char c = buffer[i];
                if (c == '\n' || c == '\r') {
                    if (line_idx > 0) {
                        line_buf[line_idx] = '\0';
                        parse_nmea_gga(line_buf); // 解析一行完整的 NMEA
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
    xTaskCreatePinnedToCore(gps_process_task, "gps_process_task", 4096, NULL, 10, NULL, 1);
}
