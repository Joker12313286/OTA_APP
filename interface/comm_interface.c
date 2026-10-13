#include "comm_interface.h"

// 接收缓冲区（外部定义，在APP_OTA_Handler中声明）
extern uint8_t rx_buffer[];
extern uint16_t rx_len;

// 全局标志：表示是否收到完整包
volatile uint8_t comm_rx_complete = 0;

uint8_t Comm_ReceivePacket(uint8_t *buf, uint16_t *len)
{
    // 这里根据你选的通信方式调用不同的接收函数
    // 返回 0 表示无数据，1 表示收到完整包
#ifdef USE_UART
    return UART_Get_Data(buf, len);
#elif defined(USE_CAN)
    return CAN_Get_Data(buf, len);
#endif
}