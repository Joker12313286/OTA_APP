#ifndef __MYWIFI__H__
#define  __MYWIFI__H__

#include "main.h"
#include <string.h>
#include <stdio.h>
#include "usart.h"

#define IP      "192.168.227.139"
#define PORT    777
#define txData  "Hello World\r\n"

//数据最大长度
#define BUFF_SIZE 4096

#define SSID "REDMI K80"
#define PASS "5201314."

// 启动 DMA 接收
void ESP_Start_Receive(void);

// 检查是否收到数据（非阻塞）
uint8_t ESP_Check_Receive(uint8_t *data, uint16_t *len);

// 发送数据（透传模式直接发）
void ESP_Send_Data(uint8_t *data, uint16_t len);

// 初始化 WiFi 连接（透传模式）
uint8_t ESP_Init_Transparent(void);

#endif
