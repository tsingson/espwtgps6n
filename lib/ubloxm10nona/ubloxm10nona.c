#include "ubloxm10nona.h"

// 💡 确保包含了 C3、C6 的宏，甚至是后续可能扩充的其它 RISC-V 芯片
#if defined(CONFIG_IDF_TARGET_ESP32C3) || defined(CONFIG_IDF_TARGET_ESP32C6)
#define GPS_UART_NUM UART_NUM_1
#else
#define GPS_UART_NUM UART_NUM_2
#endif
// ==============================================================================
// 3. 基础辅助函数 (串口初始化与校验和计算)
// ==============================================================================
void init_ubx_nona_gps_uart(void)
{
  const uart_config_t uart_config = {
      .baud_rate = GPS_UBX_RATE, // M10 默认黄金波特率
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .source_clk = UART_SCLK_DEFAULT,
  };
  ESP_ERROR_CHECK(uart_driver_install(GPS_UART_NUM, NONA_BUF_SIZE * 2,
                                      NONA_BUF_SIZE * 2, 0, NULL, 0));
  ESP_ERROR_CHECK(uart_param_config(GPS_UART_NUM, &uart_config));
  ESP_ERROR_CHECK(uart_set_pin(GPS_UART_NUM, GPS_NONA_TX_PIN, GPS_NONA_RX_PIN,
                               UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

void ubx_nona_append_checksum(uint8_t *buffer, size_t len)
{
  if (len < 8)
    return;
  uint8_t ck_a = 0, ck_b = 0;
  // Fletcher 算法：从 Class 字节开始，累加到 Checksum 之前
  for (size_t i = 2; i < len - 2; i++)
  {
    ck_a += buffer[i];
    ck_b += ck_a;
  }
  buffer[len - 2] = ck_a;
  buffer[len - 1] = ck_b;
}

// ==============================================================================
// 4. 工业级持久化配置功能 (单包多规合并注入)
// ==============================================================================
void gps_configure_ubx_nona_proc(void)
{
  uint8_t cfg_packet[] = {
      UBX_SYNC_CHAR_1, UBX_SYNC_CHAR_2, UBX_CLASS_CFG, UBX_ID_VALSET, 0x14,
      0x00, // Payload 长度: 20 字节 (小端序)

      // --- Payload 开始 ---
      0x00,          // Version: 0
      UBX_LAYER_ALL, // ⭐ 全层持久化写入（RAM + BBR + Flash），断电不失忆
      0x00, 0x00,    // 保留位占位

      // [项 1] 禁 NMEA 文本，强制转为纯 UBX 二进制流模式
      (uint8_t)(KEY_UART1OUTPROT_UBX & 0xFF),
      (uint8_t)((KEY_UART1OUTPROT_UBX >> 8) & 0xFF),
      (uint8_t)((KEY_UART1OUTPROT_UBX >> 16) & 0xFF),
      (uint8_t)((KEY_UART1OUTPROT_UBX >> 24) & 0xFF),
      0x01, // Value: 1 (纯 UBX 模式)

      // [项 2] 飙 5Hz 高频定位 (测量周期 200ms)
      (uint8_t)(KEY_RATE_MEAS & 0xFF), (uint8_t)((KEY_RATE_MEAS >> 8) & 0xFF),
      (uint8_t)((KEY_RATE_MEAS >> 16) & 0xFF),
      (uint8_t)((KEY_RATE_MEAS >> 24) & 0xFF), 0xC8,
      0x00, // Value: 200 (U16 小端序)

      // [项 3] 开启 M10 全局 NAV-PVT 消息主动高频推送
      (uint8_t)(KEY_MSGOUT_NAV_PVT & 0xFF),
      (uint8_t)((KEY_MSGOUT_NAV_PVT >> 8) & 0xFF),
      (uint8_t)((KEY_MSGOUT_NAV_PVT >> 16) & 0xFF),
      (uint8_t)((KEY_MSGOUT_NAV_PVT >> 24) & 0xFF),
      0x01, // Value: 1 (每次测量输出一次)
      // --- Payload 结束 ---

      0x00, 0x00 // Checksum 占位 (CK_A, CK_B)
  };

  ubx_nona_append_checksum(cfg_packet, sizeof(cfg_packet));

  // 发送前清空输入缓冲区，防止残留 NMEA 文本污染后续解析
  uart_flush_input(GPS_UART_NUM);

  int bytes_written = uart_write_bytes(GPS_UART_NUM, (const char *)cfg_packet,
                                       sizeof(cfg_packet));
  if (bytes_written != sizeof(cfg_packet))
  {
    ESP_LOGE(TAG, "UART 发送失败，缓冲区爆满！");
    return;
  }
  ESP_LOGI(TAG, "M10 生产级持久化配置包已全量安全注入！");
}

// ==============================================================================
// 5. UBX 二进制流状态机高可靠性解包内核
// ==============================================================================
void process_ubx_nona_byte(uint8_t byte)
{
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

  switch (state)
  {
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
    if (ck_a == calc_ck_a && ck_b == calc_ck_b)
    {
      // 成功捕获高频综合导航包 (Class: 0x01, ID: 0x07 -> UBX-NAV-PVT)
      if (u_class == 0x01 && u_id == 0x07)
      {
        ubx_nav_pvt_t *pvt = (ubx_nav_pvt_t *)payload_buf;
        double lat = pvt->lat / 10000000.0;
        double lon = pvt->lon / 10000000.0;
        double speed_kh = (pvt->gSpeed / 1000.0) * 3.6; // 从 mm/s 换算为 km/h

        printf("[UBX 5Hz 高频解算] 卫星: %d | 定位类型: %d | 纬度: %.7f | "
               "经度: %.7f | 地速: %.2f km/h\n",
               pvt->numSV, pvt->fixType, lat, lon, speed_kh);
      }
    }
    break;
  }
}
