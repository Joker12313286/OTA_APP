#include "APP_OTA_Handler.h"


/**
  * @brief App端的OTA接收任务（在App主循环或中断回调中调用）
  */
void APP_OTA_Task(void)
{
    // 1. 从通信接口获取数据包（由 comm_interface.c 提供）
    if(Comm_ReceviePacket(rx_buffer,&len) == COMM_OK)
    {
        // 2. 如果是固件数据，写入W25Q64（地址递增）
        if(is_firmware_packet)
        {
            W25Q64_PageProgram(OTA_FW_BASE_ADDR+current_offer,rx_buffer,len);
            current_offer += len;
        }
        // 3. 如果是固件结束包，执行校验
        if(is_finish_packet)
        {
            if(CRC_Check() == PASS)
            {
                // 4. 校验通过！写OTA标志，触发升级
                OTA_Flag_Write(OTA_FLAG_UPDATE_PENDING);
                HAL_NVIC_SystemReset();// 复位，让Bootloader干活
            }
            else
            {
                // 校验失败，丢弃，清空外部Flash
                OTA_Flag_Clear();
            }
        }
    }

}