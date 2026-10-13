#ifndef __COMM_INTERFACE__H__
#define  __COMM_INTERFACE__H__

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/*--------------------- 通信方式选择（在编译器中定义） ---------------------*/
// 在 Keil 的 C/C++ 编译器中添加宏定义：
// -DUSE_UART  使用串口
// -DUSE_CAN   使用CAN
// -DUSE_WIFI  使用WiFi


/*--------------------- 返回值定义 ---------------------*/
#define COMM_OK         0   /**< 收到完整有效包 */
#define COMM_NO_DATA    1   /**< 无数据 */
#define COMM_ERROR      2   /**< 通信错误 */

/*--------------------- 函数声明 ---------------------*/

/**
  * @brief 从通信接口接收一个数据包
  * @param buf  存储接收到的数据
  * @param len  返回实际接收的数据长度
  * @return COMM_OK: 收到完整包, COMM_NO_DATA: 无数据, COMM_ERROR: 错误
  */
 uint8_t Comm_ReceivePacket(uint8_t *buf, uint16_t *len);

/**
  * @brief 初始化通信接口（根据所选通信方式调用对应的初始化）
  */
void Comm_Init(void);

#endif
