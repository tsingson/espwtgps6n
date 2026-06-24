#include "oled_drv.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"

static const char *TAG = "OLED_DRV";

static i2c_master_bus_handle_t i2c_bus = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static volatile bool is_hardware_initialized = false;

void oled_drv_init(void) {
    if (is_hardware_initialized) return;
    ESP_LOGI(TAG, "Initializing SSD1306 Hardware via esp_lcd...");

    // 1. 初始化 I2C 主机总线
    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_HOST_NUM,
        .scl_io_num = OLED_I2C_SCL_PIN,
        .sda_io_num = OLED_I2C_SDA_PIN,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_master_new_bus(&i2c_bus_config, &i2c_bus));

    // 2. 将 LCD IO 挂载到 I2C 总线上
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = OLED_I2C_ADDRESS,
        .scl_speed_hz = OLED_PIXEL_CLOCK,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &io_config, &io_handle));

    // 3. 实例化 SSD1306 驱动句柄
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .bits_per_pixel = 1,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel_handle));

    // 4. 打开屏幕电源并充能
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));

    is_hardware_initialized = true;
}

void oled_drv_deinit(void) {
    if (!is_hardware_initialized) return;
    ESP_LOGI(TAG, "Deinitializing SSD1306 Core to prevent power leak...");

    if (panel_handle) {
        esp_lcd_panel_disp_on_off(panel_handle, false);
        esp_lcd_panel_del(panel_handle);
        panel_handle = NULL;
    }
    if (io_handle) {
        esp_lcd_panel_io_del(io_handle);
        io_handle = NULL;
    }
    if (i2c_bus) {
        i2c_master_bus_delete(i2c_bus);
        i2c_bus = NULL;
    }

    is_hardware_initialized = false;
}

void oled_drv_clear(void) {
    if (!panel_handle || !is_hardware_initialized) return;
    uint8_t clear_buf = {0};
    for (int i = 0; i < 8; i++) {
        esp_lcd_panel_draw_bitmap(panel_handle, 0, i, 128, i + 1, clear_buf);
    }
}

bool oled_drv_is_ready(void) {
    return is_hardware_initialized;
}

void oled_drv_draw_line_debug(const char *text, uint8_t line_num) {
    if (!panel_handle || !is_hardware_initialized) return;
    // 留供后续挂载点阵字库（如 U8g2 / Font library）物理画字使用
    ESP_LOGI(TAG, "[OLED Screen Phys Frame] Line %d -> %s", line_num, text);
}
