#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

#define UART_4G_NUM     UART_NUM_1
#define PIN_4G_TX       25
#define PIN_4G_RX       26
#define BUF_SIZE        2048

static const char *TAG = "ML307R_DEBUG";

// 包装发送函数，强制把发送的内容镜像打印到电脑串口
void at_send_debug(const char *cmd) {
    printf("\n[TX] ---> %s\n", cmd);
    fflush(stdout);
    uart_write_bytes(UART_4G_NUM, cmd, strlen(cmd));
    uart_write_bytes(UART_4G_NUM, "\r\n", 2);
}

void app_main(void) {
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_driver_install(UART_4G_NUM, BUF_SIZE, 0, 0, NULL, 0);
    uart_param_config(UART_4G_NUM, &uart_config);
    uart_set_pin(UART_4G_NUM, PIN_4G_TX, PIN_4G_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    uint8_t *data = (uint8_t *) malloc(BUF_SIZE);

    // 1. 深度等待上电
    ESP_LOGI(TAG, "Waiting 8 seconds for ML307R to boot and attach network...");
    vTaskDelay(pdMS_TO_TICKS(8000));

    // 2. 基础通信测试
    at_send_debug("AT");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 3. 检查 SIM 卡是否正常就绪
    at_send_debug("AT+CPIN?");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 4. 检查信号质量
    at_send_debug("AT+CSQ");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 5. 检查网络注册状态（1或5代表成功）
    at_send_debug("AT+CGREG?");
    vTaskDelay(pdMS_TO_TICKS(1500));

    // 6. 配置移动 Cat.1 核心网 APN
    at_send_debug("AT+CGDCONT=1,\"IP\",\"CMNET\"");
    vTaskDelay(pdMS_TO_TICKS(1500));

    // 7. 发起 HTTP 流程
    at_send_debug("AT+HTTPINIT");
    vTaskDelay(pdMS_TO_TICKS(1000));

    at_send_debug("AT+HTTPPARA=\"URL\",\"http://httpbin.org/get\"");
    vTaskDelay(pdMS_TO_TICKS(1000));

    at_send_debug("AT+HTTPACTION=0");
    ESP_LOGI(TAG, "HTTP GET Triggered. Waiting for modem async result...");
    vTaskDelay(pdMS_TO_TICKS(5000)); // 留足5秒让网络传输

    at_send_debug("AT+HTTPREAD");
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 8. 持续监听物理串口
    ESP_LOGI(TAG, "Entering continuous listening mode...");
    while (1) {
        int len = uart_read_bytes(UART_4G_NUM, data, BUF_SIZE - 1, pdMS_TO_TICKS(100));
        if (len > 0) {
            data[len] = '\0';
            // 给模块回传的数据打上标记，防止它和常规printf混淆
            printf("[RX] <--- %s", (char *)data);
            fflush(stdout);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    free(data);
}
