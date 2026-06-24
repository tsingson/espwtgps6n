#ifndef GPS_DRV_H
#define GPS_DRV_H

#include "driver/uart.h"

#define GPS_UART_NUM   UART_NUM_2
#define GPS_BAUD_RATE  115200
#define BUF_SIZE       1024
#define GPS_TX_PIN     GPIO_NUM_33
#define GPS_RX_PIN     GPIO_NUM_32

// 🟢 统一采用组件命名规范，作为外部唯一调用接口
void gps_drv_init(void);
void gps_drv_deinit(void);

// 创建底层流接收驱动任务
void gps_drv_create_rx_task(void);
void gps_drv_resume_rx_task(void);

#endif // GPS_DRV_H
