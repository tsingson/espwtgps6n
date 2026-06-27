#ifndef OLED_SSD1306_H
#define OLED_SSD1306_H

#include "esp_err.h"

#ifndef OLED_SDA_PIN
#define OLED_SDA_PIN 21 // 只有当没有定义过该宏时，默认值才为 33
#endif

#ifndef OLED_SCL_PIN
#define OLED_SCL_PIN 22 // 只有当没有定义过该宏时，默认值才为 33
#endif

#define OLED_I2C_ADDR 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64

esp_err_t oled_init(int scl_pin, int sda_pin);
esp_err_t oled_init_default(void);
void oled_clear(void);
void oled_show_string(int x, int y, const char *str);
void oled_refresh(void);
void oled_sleep_enter(void);
void oled_sleep_exit(void);

// 🌟 追加这一行高级换行接口声明
void oled_show_string_wrap(int start_x, int start_y, const char *str);
// 追加图形接口声明
void oled_draw_pixel(int x, int y, uint8_t color);
void oled_draw_rectangle(int x, int y, int width, int height);

// 追加实心矩形填充接口声明
void oled_fill_rectangle(int x, int y, int width, int height);

// 升级后的字符串显示接口声明，带反色控制
void oled_show_string_ex(int start_x, int start_y, const char *str,
                         uint8_t invert);
// 追加斜线绘制接口声明
void oled_draw_line(int x1, int y1, int x2, int y2, uint8_t color);
// 追加圆形绘制接口声明
void oled_draw_circle(int xc, int yc, int r, uint8_t color);
// 追加进度条接口声明
void oled_draw_progress_bar(int x, int y, int width, int height, int current,
                            int max);
// 追加全屏转场闪烁接口声明
void oled_flash_screen(int flash_count, int delay_ms);
// 追加屏幕震动特效接口声明
void oled_shake_screen(int intensity, int duration_ms);
// 追加 16x16 动态算法粗体接口声明
void oled_show_string_16x16_bold(int start_x, int start_y, const char *str);

#endif
