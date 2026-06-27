#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_log.h"

static const char *TAG = "UBX_CONTROL";

#define ESP32_RX_FROM_GPS_TX (16)
#define ESP32_TX_TO_GPS_RX   (17)
#define GPS_UART_NUM         (UART_NUM_2)
#define BUF_SIZE             (1024)

// UBX-NAV-PVT 结构体定义 (小端对齐，用于直接映射解析数据)
#pragma pack(push, 1)
typedef struct {
    uint32_t iTOW;       // GPS 毫秒时间戳
    uint16_t year;       // 年
    uint8_t  month;      // 月
    uint8_t  day;        // 日
    uint8_t  hour;       // 时
    uint8_t  min;        // 分
    uint8_t  sec;        // 秒
    uint8_t  valid;      // 有效性标志
    uint32_t tAcc;       // 时间精度
    int32_t  nano;       // 纳秒
    uint8_t  fixType;    // 定位类型 (0=无, 2=2D, 3=3D)
    uint8_t  flags;      // 导航状态标志
    uint8_t  flags2;     // 额外标志
    uint8_t  numSV;      // 参与定位的卫星数量 ⭐
    int32_t  lon;        // 经度 (缩放比例 1e-7) ⭐
    int32_t  lat;        // 纬度 (缩放比例 1e-7) ⭐
    int32_t  height;     // 椭球高 (mm)
    int32_t  hMSL;       // 海拔高度 (mm)
    uint32_t hAcc;       // 水平精度 (mm)
    uint32_t vAcc;       // 垂直精度 (mm)
    int32_t  velN;       // 北向速度 (mm/s)
    int32_t  velE;       // 东向速度 (mm/s)
    int32_t  velD;       // 地向速度 (mm/s)
    int32_t  gSpeed;     // 地速 (mm/s)
    int32_t  headMot;    // 运动航向角 (deg * 1e-5)
    uint32_t sAcc;       // 速度精度 (mm/s)
    uint32_t headAcc;    // 航向精度 (deg * 1e-5)
    uint16_t pDOP;       // 位置位置因子 (0.01)
    uint8_t  flags3;     // 额外标志3
    uint8_t  reserved1[5];
} ubx_nav_pvt_t;
#pragma pack(pop)

// 🛡️ 计算 Fletcher 校验和并填充最后两字节
void ubx_append_checksum(uint8_t *packet, uint16_t size) {
    uint8_t ck_a = 0, ck_b = 0;
    // 校验和从 Class 字段开始算起，直到 Payload 结束（不含 Sync Chars 和 Checksum 本身）
    for (uint16_t i = 2; i < size - 2; i++) {
        ck_a += packet[i];
        ck_b += ck_a;
    }
    packet[size - 2] = ck_a;
    packet[size - 1] = ck_b;
}

void gps_configure_ubx(void) {
    // 📦 工业级合并包：一条 VALSET 指令搞定 M10 的所有配置
    // 包含 3 个修改项：
    // 项 1: Key 0x20010021 (UART1 OUT) -> Value: 1 (仅 UBX)
    // 项 2: Key 0x30210001 (MEAS RATE) -> Value: 200ms (5Hz)
    // 项 3: Key 0x20910007 (⭐ M10 专属 NAV-PVT 全局输出开关) -> Value: 1 (开启)

    uint8_t cfg_combined[] = {
        0xB5, 0x62,             // Sync Chars
        0x06, 0x8A,             // Class: CFG (0x06), ID: VALSET (0x8A)
        0x14, 0x00,             // Length: 后续 Payload 共 20 字节 (小端: 0x0014)

        // --- Payload 开始 (共 20 字节) ---
        0x00,                   // Version: 0
        0x01,                   // Layer: 1 (仅写入 RAM，断电恢复默认，安全防变砖)
        0x00, 0x00,             // Reserved

        // [项 1] 禁 NMEA，转 UBX
        0x21, 0x00, 0x01, 0x20, // Key ID: 0x20010021
        0x01,                   // Value: 1 (U8)

        // [项 2] 飙 5Hz 高频
        0x01, 0x00, 0x21, 0x30, // Key ID: 0x30210001
        0xC8, 0x00,             // Value: 200 (U16, 小端: 0x00C8)

        // [项 3] ⭐ 修正后的 M10 专用 NAV-PVT 订阅开关
        0x07, 0x00, 0x91, 0x20, // Key ID: 0x20910007
        0x01,                   // Value: 1 (U8)
        // --- Payload 结束 ---

        0x00, 0x00              // Fletcher Checksum 占位
    };

    // 重新计算并注入完整的合并包校验和
    ubx_append_checksum(cfg_combined, sizeof(cfg_combined));

    // 一口气灌入串口
    uart_write_bytes(GPS_UART_NUM, cfg_combined, sizeof(cfg_combined));

    ESP_LOGI(TAG, "M10 终极合并配置包已全量注入，静待高频二进制流唤醒...");
}

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

// 🛡️ UBX 状态机解析内核
void process_ubx_byte(uint8_t byte) {
    static enum { STATE_IDLE, STATE_SYNC2, STATE_CLASS, STATE_ID, STATE_LEN1, STATE_LEN2, STATE_PAYLOAD, STATE_CKA, STATE_CKB } state = STATE_IDLE;
    static uint8_t u_class, u_id;
    static uint16_t payload_len, payload_idx;
    static uint8_t payload_buf[256];
    static uint8_t ck_a, ck_b;
    static uint8_t calc_ck_a, calc_ck_b;

    // 状态机流转
    switch (state) {
        case STATE_IDLE:
            if (byte == 0xB5) state = STATE_SYNC2;
            break;
        case STATE_SYNC2:
            state = (byte == 0x62) ? STATE_CLASS : STATE_IDLE;
            break;
        case STATE_CLASS:
            u_class = byte;
            calc_ck_a = byte; calc_ck_b = byte; // 初始化校验
            state = STATE_ID;
            break;
        case STATE_ID:
            u_id = byte;
            calc_ck_a += byte; calc_ck_b += calc_ck_a;
            state = STATE_LEN1;
            break;
        case STATE_LEN1:
            payload_len = byte;
            calc_ck_a += byte; calc_ck_b += calc_ck_a;
            state = STATE_LEN2;
            break;
        case STATE_LEN2:
            payload_len |= ((uint16_t)byte << 8);
            calc_ck_a += byte; calc_ck_b += calc_ck_a;
            payload_idx = 0;
            state = (payload_len > 0 && payload_len < sizeof(payload_buf)) ? STATE_PAYLOAD : STATE_IDLE;
            break;
        case STATE_PAYLOAD:
            payload_buf[payload_idx++] = byte;
            calc_ck_a += byte; calc_ck_b += calc_ck_a;
            if (payload_idx >= payload_len) state = STATE_CKA;
            break;
        case STATE_CKA:
            ck_a = byte;
            state = STATE_CKB;
            break;
        case STATE_CKB:
            ck_b = byte;
            state = STATE_IDLE; // 解析结束，重置状态

            // 验证校验和
            if (ck_a == calc_ck_a && ck_b == calc_ck_b) {
                // 成功抓取到目标高频导航包：UBX-NAV-PVT (Class: 0x01, ID: 0x07)
                if (u_class == 0x01 && u_id == 0x07) {
                    ubx_nav_pvt_t *pvt = (ubx_nav_pvt_t *)payload_buf;
                    double lat = pvt->lat / 10000000.0;
                    double lon = pvt->lon / 10000000.0;
                    double speed_kh = (pvt->gSpeed / 1000.0) * 3.6; // mm/s 转换为 km/h

                    printf("[UBX 5Hz 高频解算] 卫星: %d | 定位类型: %d | 纬度: %.7f | 经度: %.7f | 地速: %.2f km/h\n",
                           pvt->numSV, pvt->fixType, lat, lon, speed_kh);
                }
            }
            break;
    }
}

void gps_ubx_task(void *pvParameters) {
    uint8_t *buffer = (uint8_t *) malloc(BUF_SIZE);

    init_gps_uart();
    vTaskDelay(pdMS_TO_TICKS(500)); // 等待串口驱动稳定

    // 🔥 发动双向控制：改协议、飙高频
    gps_configure_ubx();

    while (1) {
        // 二进制流读取必须追求低延迟，超时设为 10ms
        int len = uart_read_bytes(GPS_UART_NUM, buffer, BUF_SIZE - 1, 10 / portTICK_PERIOD_MS);
        if (len > 0) {
            for (int i = 0; i < len; i++) {
                process_ubx_byte(buffer[i]); // 喂给二进制状态机
            }
        }
    }
    free(buffer);
}

void app_main(void) {
    xTaskCreatePinnedToCore(gps_ubx_task, "gps_ubx_task", 4096, NULL, 10, NULL, 1);
}
