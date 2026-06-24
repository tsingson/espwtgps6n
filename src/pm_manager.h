#ifndef PM_MANAGER_H
#define PM_MANAGER_H

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// 供全局或其它业务模块查询的任务运行标志
extern volatile bool g_tasks_should_run;

// 初始化系统同步 IPC
void pm_system_ipc_init(void);

// 申请/释放休眠通行证的接口
BaseType_t pm_wait_for_business_done(TickType_t xTicksToWait);
void pm_give_sleep_permit(void);

// 休眠前与唤醒后的核心切时序动作
void pm_prepare_system_to_sleep(void);
void pm_restore_system_after_wakeup(void);

#endif // PM_MANAGER_H
