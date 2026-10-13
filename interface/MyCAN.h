#ifndef __MYCAN__H__
#define  __MYCAN__H__

#include "main.h"
#include "can.h"
#include "string.h"

typedef struct {
    CAN_RxHeaderTypeDef pHeader;
    uint8_t data[8];
} MYCAN_rec;

/**
 * @brief 初始化CAN 配置白名单过滤器 开启CAN
 */
void MYCAN_Init(void);

/**
 * @brief 发送CAN数据函数
 * @param id 发送的CAN ID(默认数据格式 标准ID)
 * @param data 发送的数据(一次性最多发8位)
 * @param len 发送的数据长度
 */
void MYCAN_send(uint16_t id, uint8_t *data, uint8_t len);

/**
 * @brief 接收CAN数据函数
 * @param mycan_rec 接收的CAN数据结构体
 * @param rec_conut 接收数据
 * @return 接收的数据长度
 */
void MYCAN_receive(MYCAN_rec *mycan_rec,uint8_t *rec_count);

#endif
