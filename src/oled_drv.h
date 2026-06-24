#ifndef OLED_DRV_H
#define OLED_DRV_H

#include <stdint.h>
#include <stdbool.h>

// 硬件引脚解耦配置
#define I2C_HOST_NUM        I2C_NUM_0
#define OLED_I2C_SDA_PIN    GPIO_NUM_21  // 请根据实际物理板连线调整
#define OLED_I2C_SCL_PIN    GPIO_NUM_22  // 请根据实际物理板连线调整
#define OLED_PIXEL_CLOCK    400000       // 400kHz 标准 I2C
#define OLED_I2C_ADDRESS    0x3C         // SSD1306 常见 I2C 地址

// 物理屏幕规格
#define OLED_H_RES          128
#define OLED_V_RES          64

// 导出系统调用的硬件管理接口
void oled_drv_init(void);
void oled_drv_deinit(void);
void oled_drv_clear(void);
bool oled_drv_is_ready(void);

// 渲染输出测试或绘图接口（后续连接字库点阵使用）
void oled_drv_draw_line_debug(const char *text, uint8_t line_num);

#endif // OLED_DRV_H
