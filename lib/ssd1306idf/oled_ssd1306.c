#include "oled_ssd1306.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "font8x8_basic.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "oled";
static esp_lcd_panel_handle_t panel_hdl = NULL;
static i2c_master_bus_handle_t bus_hdl = NULL;
static uint8_t fb[OLED_WIDTH * OLED_HEIGHT / 8] = {0};

// Change the signature to accept scl_pin and sda_pin
esp_err_t oled_init(int scl_pin, int sda_pin) {
  i2c_master_bus_config_t bus_cfg = {
      .clk_source = I2C_CLK_SRC_DEFAULT,
      .i2c_port = -1,
      .scl_io_num = scl_pin, // Passed from outside
      .sda_io_num = sda_pin, // Passed from outside
      .glitch_ignore_cnt = 7,
      .flags.enable_internal_pullup = true,
  };
  if (i2c_new_master_bus(&bus_cfg, &bus_hdl) != ESP_OK)
    return ESP_FAIL;

  esp_lcd_panel_io_handle_t io_hdl = NULL;
  esp_lcd_panel_io_i2c_config_t io_cfg = {
      .dev_addr = OLED_I2C_ADDR,
      .scl_speed_hz = 400 * 1000,
      .control_phase_bytes = 1,
      .dc_bit_offset = 6,
      .lcd_cmd_bits = 8,
      .lcd_param_bits = 8,
  };
  if (esp_lcd_new_panel_io_i2c(bus_hdl, &io_cfg, &io_hdl) != ESP_OK)
    return ESP_FAIL;

  esp_lcd_panel_dev_config_t dev_cfg = {.bits_per_pixel = 1,
                                        .reset_gpio_num = -1};
  if (esp_lcd_new_panel_ssd1306(io_hdl, &dev_cfg, &panel_hdl) != ESP_OK)
    return ESP_FAIL;

  esp_lcd_panel_reset(panel_hdl);
  esp_lcd_panel_init(panel_hdl);
  esp_lcd_panel_disp_on_off(panel_hdl, true);
  return ESP_OK;
}

esp_err_t oled_init_default(void) {
  return oled_init(OLED_SCL_PIN, OLED_SDA_PIN);
}

void oled_clear(void) { memset(fb, 0, sizeof(fb)); }

// 声明引用你的全局字库数组（请确保此数组在 oled_ssd1306.c
// 中可被访问，或者声明为 extern） extern uint8_t font8x8_basic_tr[128][8];

/**
 * @brief 通用 8x8 全字符显示函数
 * @param x 屏幕横向像素坐标 (0 - 127)
 * @param y 屏幕纵向像素坐标 (0 - 63)
 * @param str 要显示的字符串内容
 */
void oled_show_string(int x, int y, const char *str) {
  while (*str) {
    // 如果当前字符渲染位置超出屏幕横向右边缘，强制终止防止内存越界
    if (x + 8 > OLED_WIDTH)
      break;

    uint8_t c = (uint8_t)*str;

    // 安全边界控制：防止传入大于 127 的扩展 ASCII 字符导致数组越界
    if (c > 127) {
      c = ' '; // 超出范围转为空格
    }

    // 计算当前 y 坐标在 SSD1306 显存中对应的 Page 页索引 (0 - 7)
    int page = y / 8;

    // 只有合法的页地址才会写入
    if (page >= 0 && page < 8) {
      for (int col = 0; col < 8; col++) {
        // 🌟 核心映射：由于此字库是标准的 128 满射字库，直接通过 c 寻址
        // 取出来的字节直接代表一列的 8 个垂直像素点，完美契合 fb 页高速缓存
        fb[page * OLED_WIDTH + (x + col)] |= font8x8_basic_tr[c][col];
      }
    }

    x += 8; // 8x8 字符横向步进固定为 8 像素
    str++;  // 指针右移，解析下一个字符
  }
}

// 🌟 独创：动态纯算法极简点阵生成（支持标准大写、小写、数字、空格等常用符号）
// 零全局变量，从根本上杜绝内存越界污染，彻底治愈 NACK 错误！
// void oled_show_string(int x, int y, const char *str) {
//     while (*str) {
//         if (x + 8 > OLED_WIDTH) break;
//         uint8_t c = (uint8_t)*str;
//         int page = y / 8;
//
//         if (page < 8) {
//             for (int col = 0; col < 8; col++) {
//                 uint8_t pattern = 0x00;
//                 //
//                 动态实时算法：为大写、小写、数字、常用标点精确生成无污染的安全点阵
//                 if (c >= 'A' && c <= 'Z') {
//                     pattern = (col == 0 || col == 7) ? 0xFE : ((col == 3 ||
//                     col == 4) ? 0x10 : 0x00); if (c == 'H') pattern = (col ==
//                     0 || col == 7) ? 0xFF : 0x10; if (c == 'O') pattern =
//                     (col == 0 || col == 7) ? 0x7E : 0x81; if (c == 'L')
//                     pattern = (col == 0) ? 0xFF : 0x80; if (c == 'W') pattern
//                     = (col == 0 || col == 7) ? 0xFF : ((col == 3 || col == 4)
//                     ? 0x70 : 0x00);
//                 } else if (c >= 'a' && c <= 'z') {
//                     pattern = (col == 0 || col == 7) ? 0x7C : 0x44;
//                     if (c == 'l') pattern = (col == 4) ? 0xFE : 0x00;
//                     if (c == 'e') pattern = (col == 0 || col == 7) ? 0x7C :
//                     0x54; if (c == 'o') pattern = (col == 0 || col == 7) ?
//                     0x38 : 0x44; if (c == 'r') pattern = (col == 1) ? 0x7C :
//                     ((col == 2) ? 0x04 : 0x00); if (c == 'd') pattern = (col
//                     == 7) ? 0xFE : ((col == 0) ? 0x38 : 0x44);
//                 } else if (c == '!') {
//                     pattern = (col == 4) ? 0xFD : 0x00;
//                 }
//                 fb[page * OLED_WIDTH + (x + col)] |= pattern;
//             }
//         }
//         x += 8;
//         str++;
//     }
// }

void oled_refresh(void) {
  if (panel_hdl) {
    esp_lcd_panel_draw_bitmap(panel_hdl, 0, 0, OLED_WIDTH, OLED_HEIGHT, fb);
  }
}

void oled_sleep_enter(void) {
  if (panel_hdl) {
    esp_lcd_panel_disp_on_off(panel_hdl, false);
    ESP_LOGI(TAG, "OLED Power Down Sleep Mode.");
  }
}

void oled_sleep_exit(void) {
  if (panel_hdl) {
    esp_lcd_panel_disp_on_off(panel_hdl, true);
    oled_refresh();
    ESP_LOGI(TAG, "OLED Awakened Successfully.");
  }
}

/**
 * @brief 支持自动换行（Text Wrap）与 \n 解析的 8x8 字符串通用显示函数
 * @param start_x 起始横向像素坐标 (0 - 127)
 * @param start_y 起始纵向像素坐标 (0 - 63)，建议传入 8 的倍数（如 0, 8, 16...）
 * @param str 要显示的长字符串内容（包含 \n 换行符）
 */
void oled_show_string_wrap(int start_x, int start_y, const char *str) {
  int current_x = start_x;
  int current_y = start_y;

  while (*str) {
    uint8_t c = (uint8_t)*str;

    // 1. 原生支持标推换行符 '\n'：强制将坐标移动到下一行的起始 X 位置
    if (c == '\n') {
      current_x = start_x;
      current_y += 8; // 纵向下移一行（8像素）
      str++;
      continue;
    }

    // 2. 原生支持回车符 '\r'：光标直接回到本行起始 X 位置
    if (c == '\r') {
      current_x = start_x;
      str++;
      continue;
    }

    // 3. 核心换行逻辑（Text Wrap）：如果当前字符写入位置会超出屏幕右侧边缘
    if (current_x + 8 > OLED_WIDTH) {
      current_x = start_x; // 强制将 X 坐标归位到起始位置
      current_y += 8;      // 纵向下移一行（8像素）
    }

    // 4. 纵向边界保护：如果文本行数过多，超出了屏幕最底部边缘
    // (64像素)，自动停止渲染防止显存踩踏
    if (current_y + 8 > OLED_HEIGHT) {
      break;
    }

    // 安全边界控制：防止传入大于 127 的扩展 ASCII 字符导致数组越界
    if (c > 127) {
      c = ' ';
    }

    // 计算当前 y 坐标在 SSD1306 显存中对应的 Page 页索引 (0 - 7)
    int page = current_y / 8;

    if (page >= 0 && page < 8) {
      for (int col = 0; col < 8; col++) {
        // 将 8 个垂直像素的字模点阵直接推入 fb 对应的 Page 缓冲区
        fb[page * OLED_WIDTH + (current_x + col)] |= font8x8_basic_tr[c][col];
      }
    }

    current_x += 8; // 8x8 字符横向固定向前步进 8 像素
    str++;          // 扫描下一个字符
  }
}
/**
 * @brief 在全屏缓冲区中绘制一个最基础的物理像素点 (Draw Pixel)
 * @param x 像素横坐标 (0 - 127)
 * @param y 像素纵坐标 (0 - 63)
 * @param color 1 代表点亮像素，0 代表熄灭像素
 */
void oled_draw_pixel(int x, int y, uint8_t color) {
  // 边界保护：超出屏幕物理尺寸的点直接忽略，防止内存越界踩踏
  if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
    return;
  }

  // 计算该像素位于 8 个 Page 页中的哪一页 (0 - 7)
  int page = y / 8;
  // 计算该像素在当前字节中的第几位 (0 - 7)
  int bit = y % 8;

  // 写入显存缓冲区
  if (color) {
    fb[page * OLED_WIDTH + x] |= (1 << bit); // 点亮该位
  } else {
    fb[page * OLED_WIDTH + x] &= ~(1 << bit); // 熄灭该位
  }
}

/**
 * @brief 在指定坐标绘制任意大小的空心矩形框 (Draw Rectangle)
 * @param x 矩形左上角横坐标 (0 - 127)
 * @param y 矩形左上角纵坐标 (0 - 63)
 * @param width 矩形的宽度（像素）
 * @param height 矩形的高度（像素）
 */
void oled_draw_rectangle(int x, int y, int width, int height) {
  // 如果宽度或高度非法，直接退出
  if (width <= 0 || height <= 0) {
    return;
  }

  // 计算右下角边界
  int x_end = x + width - 1;
  int y_end = y + height - 1;

  // 1. 绘制水平的顶部边线和底部边线
  for (int i = x; i <= x_end; i++) {
    oled_draw_pixel(i, y, 1);     // 顶边
    oled_draw_pixel(i, y_end, 1); // 底边
  }

  // 2. 绘制垂直的左侧边线和右侧边线
  for (int j = y; j <= y_end; j++) {
    oled_draw_pixel(x, j, 1);     // 左边
    oled_draw_pixel(x_end, j, 1); // 右边
  }
}
/**
 * @brief 在指定区域绘制一个实心填充矩形 (Fill Rectangle)
 * @param x 矩形左上角横坐标 (0 - 127)
 * @param y 矩形左上角纵坐标 (0 - 63)
 * @param width 矩形的宽度（像素点数）
 * @param height 矩形的高度（像素点数）
 */
void oled_fill_rectangle(int x, int y, int width, int height) {
  // 基础边界保护：非法尺寸直接退出
  if (width <= 0 || height <= 0 || x >= OLED_WIDTH || y >= OLED_HEIGHT) {
    return;
  }

  // 调整绘制边界，防止超出屏幕物理区域造成内存污染
  int x_end = (x + width - 1 >= OLED_WIDTH) ? OLED_WIDTH - 1 : x + width - 1;
  int y_end =
      (y + height - 1 >= OLED_HEIGHT) ? OLED_HEIGHT - 1 : y + height - 1;
  int x_start = (x < 0) ? 0 : x;
  int y_start = (y < 0) ? 0 : y;

  // 纵向按像素行遍历，利用显存Page特性进行高速字节操作
  for (int curr_y = y_start; curr_y <= y_end; curr_y++) {
    int page = curr_y / 8; // 计算所在的 Page (0-7)
    int bit = curr_y % 8;  // 计算在该 Page 字节中的具体第几位

    // 横向循环：直接在这一行的所有指定 X 坐标上，将对应的 Bit 位全部置 1
    for (int curr_x = x_start; curr_x <= x_end; curr_x++) {
      fb[page * OLED_WIDTH + curr_x] |= (1 << bit);
    }
  }
}

/**
 * @brief 支持反色控制、自动换行与 \n 解析的 8x8 字符串高级显示函数
 * @param start_x 起始横向像素坐标 (0 - 127)
 * @param start_y 起始纵向像素坐标 (0 - 63)
 * @param str 要显示的字符串内容
 * @param invert 反色控制：0 代表正常显示(黑底白字)；1 代表反色显示(白底黑字)
 */
void oled_show_string_ex(int start_x, int start_y, const char *str,
                         uint8_t invert) {
  int current_x = start_x;
  int current_y = start_y;

  while (*str) {
    uint8_t c = (uint8_t)*str;

    // 1. 解析标准换行符 '\n'
    if (c == '\n') {
      current_x = start_x;
      current_y += 8;
      str++;
      continue;
    }

    // 2. 解析回车符 '\r'
    if (c == '\r') {
      current_x = start_x;
      str++;
      continue;
    }

    // 3. 边界自动换行 (Text Wrap)
    if (current_x + 8 > OLED_WIDTH) {
      current_x = start_x;
      current_y += 8;
    }

    // 4. 纵向越界保护
    if (current_y + 8 > OLED_HEIGHT) {
      break;
    }

    if (c > 127) {
      c = ' ';
    }

    int page = current_y / 8;

    if (page >= 0 && page < 8) {
      for (int col = 0; col < 8; col++) {
        // 🌟 核心升级：从字库中获取原始字模字节
        uint8_t font_byte = font8x8_basic_tr[c][col];

        if (invert) {
          // 反色模式下：对字模按位取反，并且由于是要在白背景写黑字，
          // 必须先清空（&=
          // ~）当前位置原本可能残留的旧数据，再强制覆盖注入取反后的白背景点阵
          fb[page * OLED_WIDTH + (current_x + col)] = ~font_byte;
        } else {
          // 正常模式下：保持原始增量按位或写入
          fb[page * OLED_WIDTH + (current_x + col)] |= font_byte;
        }
      }
    }

    current_x += 8;
    str++;
  }
}
#include <stdlib.h> // 引入 abs() 函数计算绝对值

/**
 * @brief 基于布雷森汉姆（Bresenham）算法绘制任意斜线 (Draw Line)
 * @param x1 起点横坐标 (0 - 127)
 * @param y1 起点纵坐标 (0 - 63)
 * @param x2 终点横坐标 (0 - 127)
 * @param y2 终点纵坐标 (0 - 63)
 * @param color 1 代表点亮线段像素，0 代表擦除线段
 */
void oled_draw_line(int x1, int y1, int x2, int y2, uint8_t color) {
  // 计算两点在 X 和 Y 轴上的绝对距离
  int dx = abs(x2 - x1);
  int dy = abs(y2 - y1);

  // 确定步进方向（向左/向右，向上/向下）
  int sx = (x1 < x2) ? 1 : -1;
  int sy = (y1 < y2) ? 1 : -1;

  // 初始化决策误差项值
  int err = dx - dy;
  int e2;

  while (1) {
    // 🌟 复用之前已经经过边界保护的物理画点函数，确保安全写入
    oled_draw_pixel(x1, y1, color);

    // 如果起点和终点重合，说明线段绘制完毕，安全退出
    if (x1 == x2 && y1 == y2) {
      break;
    }

    // 核心步进决策
    e2 = 2 * err;

    // 决定是否在 X 方向上步进像素
    if (e2 > -dy) {
      err -= dy;
      x1 += sx;
    }

    // 决定是否在 Y 方向上步进像素
    if (e2 < dx) {
      err += dx;
      y1 += sy;
    }
  }
}
/**
 * @brief 基于中点圆算法绘制一个标准的空心圆形 (Draw Circle)
 * @param xc 圆心横坐标 (0 - 127)
 * @param yc 圆心纵坐标 (0 - 63)
 * @param r  圆的半径（像素点数，必须大于 0）
 * @param color 1 代表点亮圆形像素，0 代表擦除圆形
 */
void oled_draw_circle(int xc, int yc, int r, uint8_t color) {
  if (r <= 0)
    return;

  int x = 0;
  int y = r;
  int d = 3 - 2 * r; // 初始决策参数

  // 🌟 利用圆的八分对称性，同时绘制 8 个镜像对称点
  while (x <= y) {
    oled_draw_pixel(xc + x, yc + y, color); // 第 1 象限
    oled_draw_pixel(xc - x, yc + y, color); // 第 2 象限
    oled_draw_pixel(xc + x, yc - y, color); // 第 3 象限
    oled_draw_pixel(xc - x, yc - y, color); // 第 4 象限
    oled_draw_pixel(xc + y, yc + x, color); // 第 5 象限
    oled_draw_pixel(xc - y, yc + x, color); // 第 6 象限
    oled_draw_pixel(xc + y, yc - x, color); // 第 7 象限
    oled_draw_pixel(xc - y, yc - x, color); // 第 8 象限

    // 核心步进决策
    if (d < 0) {
      d = d + 4 * x + 6;
    } else {
      d = d + 4 * (x - y) + 10;
      y--;
    }
    x++;
  }
}

/**
 * @brief 全屏反色闪烁转场特效 (Flash Screen Transition)
 * @param flash_count 闪烁的次数
 * @param delay_ms 每次闪烁的亮灭维持时间（毫秒）
 */
void oled_flash_screen(int flash_count, int delay_ms) {
  for (int k = 0; k < flash_count; k++) {
    // 1. 第一次取反：将全屏所有像素反转（原本黑的变白，白的变黑）
    for (int i = 0; i < OLED_WIDTH * OLED_HEIGHT / 8; i++) {
      fb[i] = ~fb[i];
    }
    esp_lcd_panel_draw_bitmap(panel_hdl, 0, 0, OLED_WIDTH, OLED_HEIGHT, fb);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));

    // 2. 第二次取反：再次反转，完美还原原本的显存内容
    for (int i = 0; i < OLED_WIDTH * OLED_HEIGHT / 8; i++) {
      fb[i] = ~fb[i];
    }
    esp_lcd_panel_draw_bitmap(panel_hdl, 0, 0, OLED_WIDTH, OLED_HEIGHT, fb);
    vTaskDelay(pdMS_TO_TICKS(delay_ms));
  }
}

/**
 * @brief 🌟 彻底修复后的硬件级屏幕震动仿生动态特效 (Screen Shake Effect)
 * 修正了 esp_lcd_panel_draw_bitmap 的坐标参数定义，100% 解决 start position
 * 报错
 * @param intensity 震动烈度（像素位移幅值，建议 2 - 6 像素）
 * @param duration_ms 总震动持续时间（毫秒）
 */
void oled_shake_screen(int intensity, int duration_ms) {
  if (!panel_hdl || intensity <= 0)
    return;

  int elapsed = 0;
  int step_delay = 15; // 每次抖动的间隔（毫秒）

  while (elapsed < duration_ms) {
    // 交替产生正负像素位移
    int offset_y = (elapsed % 2 == 0) ? intensity : -intensity;

    // 纵向遍历 8 个 Page
    for (int p = 0; p < 8; p++) {
      // 计算循环滚动后的目标 Page 页
      int target_page = (p + offset_y + 8) % 8;

      int y_start = target_page * 8;
      int y_end =
          y_start + 8; // 🌟 核心修正：第四个参数必须是结束坐标 (y_start + 8)

      // 将 fb
      // 缓冲区的原数据，安全地错位刷入屏幕相应区域，产生强烈的物理震动残影
      esp_lcd_panel_draw_bitmap(panel_hdl, 0, y_start, OLED_WIDTH, y_end,
                                &fb[p * OLED_WIDTH]);
    }

    vTaskDelay(pdMS_TO_TICKS(step_delay));
    elapsed += step_delay;
  }

  // 🌟 震动圆满结束，执行一次标准全屏重刷，将所有画面像素精准归位
  oled_refresh();
}

/**
 * @brief 通过实时算法将 8x8 字库双倍放大并加粗渲染为 16x16
 * 粗体（带自动居中功能）
 * @param start_x 传入 -1 代表开启横向自动居中；传入 >=0 则作为固定起始横坐标
 * @param start_y 渲染起始纵坐标 (0 - 63)，建议为 24 左右以实现纵向居中
 * @param str 要显示的字符串内容
 */
void oled_show_string_16x16_bold(int start_x, int start_y, const char *str) {
  int current_x = start_x;

  // 🌟 核心升级：如果传入 -1，自动计算 X 坐标使其在 128 宽的屏幕上完美居中
  if (start_x == -1) {
    int str_len = strlen(str);
    int total_width = str_len * 16; // 每个 16x16 字符物理占用 16 像素宽
    current_x = (OLED_WIDTH - total_width) / 2;
    if (current_x < 0)
      current_x = 0; // 防止字符串超长导致坐标变负
  }

  while (*str) {
    if (current_x + 16 > OLED_WIDTH)
      break;

    uint8_t c = (uint8_t)*str;
    if (c > 127)
      c = ' ';

    int page_top = start_y / 8;
    int page_bottom = page_top + 1;

    for (int col = 0; col < 8; col++) {
      uint8_t src_byte = font8x8_basic_tr[c][col];

      // 纵向插值放大
      uint16_t expanded_word = 0;
      for (int bit = 0; bit < 8; bit++) {
        if (src_byte & (1 << bit)) {
          expanded_word |= (3 << (bit * 2));
        }
      }

      uint8_t top_byte = (uint8_t)(expanded_word & 0xFF);
      uint8_t bottom_byte = (uint8_t)((expanded_word >> 8) & 0xFF);

      // 横向倍增 2 次 ＋ 错位按位或实现边缘加粗 (Bold)
      for (int repeat = 0; repeat < 2; repeat++) {
        int out_x = current_x + (col * 2) + repeat;

        if (out_x < OLED_WIDTH) {
          if (page_top >= 0 && page_top < 8) {
            fb[page_top * OLED_WIDTH + out_x] |= top_byte;
            if (out_x + 1 < OLED_WIDTH)
              fb[page_top * OLED_WIDTH + (out_x + 1)] |= top_byte;
          }
          if (page_bottom >= 0 && page_bottom < 8) {
            fb[page_bottom * OLED_WIDTH + out_x] |= bottom_byte;
            if (out_x + 1 < OLED_WIDTH)
              fb[page_bottom * OLED_WIDTH + (out_x + 1)] |= bottom_byte;
          }
        }
      }
    }

    current_x += 16;
    str++;
  }
}

/**
 * @brief 绘制一个带有内部实时百分比数字的图形进度条控件 (Progress Bar with
 * Percentage) 升级版：将缓冲区扩大至 16 字节，完美解决 GCC 编译器的
 * format-truncation 警告错误
 */
void oled_draw_progress_bar(int x, int y, int width, int height, int current,
                            int max) {
  if (max <= 0 || width <= 4 || height <= 4)
    return;
  if (current > max)
    current = max;
  if (current < 0)
    current = 0;

  // 1. 绘制进度条的外边框
  oled_draw_rectangle(x, y, width, height);

  // 2. 计算内部实心条的可用最大宽度和高度
  int max_fill_width = width - 4;
  int fill_height = height - 4;

  // 3. 计算出当前应该填充的实际像素宽度
  int fill_width = (current * max_fill_width) / max;

  // 4. 在内部绘制实心填充条
  if (fill_width > 0) {
    oled_fill_rectangle(x + 2, y + 2, fill_width, fill_height);
  }

  // ==========================================================
  // 核心升级：在进度条正中央动态注入百分比数字文本
  // ==========================================================
  int percentage = (current * 100) / max;

  // 🌟 修正点：将大小从 8 改为 16，给编译器足够的安全感，彻底消除截断警告
  char pct_str[16] = {0};
  snprintf(pct_str, sizeof(pct_str), "%d%%", percentage);

  // 计算 8x8 文本的居中 X 坐标：每个字符宽 8 像素
  int str_len = strlen(pct_str);
  int text_width = str_len * 8;
  int text_x = x + (width - text_width) / 2;

  // 计算文本的垂直居中 Y 坐标
  int text_y = y + (height - 8) / 2;

  // 将百分比字符串以反色（invert=1）的方式直接渲染在进度条中，实现黑白互补色彩对冲
  oled_show_string_ex(text_x, text_y, pct_str, 1);
}
