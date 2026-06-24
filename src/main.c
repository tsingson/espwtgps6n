// 替换成修正后的 src/main.c
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdio.h>

#include "gps_drv.h"
#include "gps_proc.h"
#include "pm_manager.h"

void app_main(void) {
  // 关键修复：加入延时以等待系统完全稳定，抑制上电乱码
  vTaskDelay(pdMS_TO_TICKS(500));

  esp_log_level_set("*", ESP_LOG_INFO);
  printf("\n--- 🟢 工业级模块化 GPS 监控系统固件已就绪 ---\n");

  // 1. 初始化核心级 IPC
  pm_system_ipc_init();

  // 2. 明确工作状态：上电默认开启运行标志
  g_tasks_should_run = true;

  // 3. 依次拉起业务解耦组件的任务
  gps_drv_init(); // 🟢 关键修正：修改为正确的组件初始化函数名
  gps_proc_create_tasks();
  gps_drv_create_rx_task();

  while (1) {
    uint32_t current_interval = get_gps_sleep_interval();
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)current_interval *
                                                  1000 * 1000));

    ESP_LOGI("MAIN", "=== 监测轮询开始，等待有效定位信号... ===");
    gps_proc_flush_console_buffer();

    // 给子任务 5 秒时间去读串口、解析拼包
    if (pm_wait_for_business_done(pdMS_TO_TICKS(5000)) == pdFALSE) {
      ESP_LOGW("MAIN", "本轮搜星超时，正在下发超时状态通知...");
      gps_proc_push_timeout_msg(current_interval);
    }

    vTaskDelay(pdMS_TO_TICKS(80));
    gps_proc_flush_console_buffer();

    ESP_LOGI("MAIN", "各外设配置正常，进入轻度休眠周期: %" PRIu32 " 秒",
             current_interval);
    gps_proc_flush_console_buffer();

    // 4. 业务做完了，调用 PM 管理中台隔离硬件并去睡觉
    pm_prepare_system_to_sleep();

    // 5. 触发底层的硬件级低功耗休眠
    esp_light_sleep_start();

    // ================= 【30秒后自动苏醒点】 =================

    // 6. 一键重建硬件依赖并无缝复活所有挂起业务
    pm_restore_system_after_wakeup();
  }
}
