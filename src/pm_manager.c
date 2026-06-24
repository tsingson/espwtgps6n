// 修改 src/pm_manager.c 的开头部分，删除 static const char *TAG 声明：
#include "pm_manager.h"
#include "esp_log.h"
#include "gps_drv.h"
#include "gps_proc.h"
#include <stdio.h>

// 🟢 已移除 static const char *TAG = "PM_MGR"; 消除编译警告

volatile bool g_tasks_should_run = true;
static SemaphoreHandle_t sleep_sem = NULL;
// ... 后面保持完全不变 ...

void pm_system_ipc_init(void) { sleep_sem = xSemaphoreCreateBinary(); }

BaseType_t pm_wait_for_business_done(TickType_t xTicksToWait) {
  if (sleep_sem == NULL)
    return pdFALSE;
  return xSemaphoreTake(sleep_sem, xTicksToWait);
}

void pm_give_sleep_permit(void) {
  if (sleep_sem != NULL) {
    xSemaphoreGive(sleep_sem);
  }
}

void pm_prepare_system_to_sleep(void) {
  // 1. 通知子任务安全就地卧倒
  g_tasks_should_run = false;
  vTaskDelay(pdMS_TO_TICKS(50)); // 给 50ms 让子任务跳出当前阻塞

  // 2. 彻底销毁 GPS 的 UART 驱动并物理隔离引脚，杜绝电平噪声干扰
  gps_drv_deinit();
}

void pm_restore_system_after_wakeup(void) {
  // 1. 重新拉起并初始化 UART 硬件与引脚绑定
  gps_drv_init();

  // 2. 恢复工作标志，并通过 Task Notification 瞬间“激活”所有业务任务
  g_tasks_should_run = true;
  gps_drv_resume_rx_task();
  gps_proc_resume_tasks();
}
