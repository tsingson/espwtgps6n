#ifndef GPS_PROC_H
#define GPS_PROC_H

#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define LINE_BUF_SIZE  128

typedef enum { GPS_DATA_STATUS, GPS_DATA_LOCATION } gps_msg_type_t;

typedef struct {
  gps_msg_type_t type;
  char line1[LINE_BUF_SIZE];
  char line2[LINE_BUF_SIZE];
  char line3[LINE_BUF_SIZE];
} gps_msg_t;

// 全局显示消费队列句柄（如 SSD1306 外部驱动可以直接读取此句柄）
extern QueueHandle_t g_ssd1306_queue;

// 创建业务与显示处理任务
void gps_proc_create_tasks(void);
void gps_proc_resume_tasks(void);

// 线程安全的休眠时间动态获取/设置接口
void set_gps_sleep_interval(uint32_t seconds);
uint32_t get_gps_sleep_interval(void);

// 向中台推送数据的统一内部接口
void gps_proc_push_data(gps_msg_t *msg);
void gps_proc_push_timeout_msg(uint32_t interval);

// 强制刷新排空标准控制台缓冲区
void gps_proc_flush_console_buffer(void);

#endif // GPS_PROC_H
