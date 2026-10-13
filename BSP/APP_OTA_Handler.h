#ifndef __APP_OTA_HANDLER__H__
#define  __APP_OTA_HANDLER__H__

#include "main.h"
#include "comm_interface.h"
#include "W25Q64.h"
#include "OTA_Manager.h"

// 外部Flash的固件存放基址  W25Q64
#define OTA_FW_BASE_ADDR      0x00000000


/**
  * @brief App端的OTA接收任务（在App主循环或中断回调中调用）
  */
void APP_OTA_Task(void);

#endif
