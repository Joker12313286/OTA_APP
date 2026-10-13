#ifndef __ESP8266__H__
#define  __ESP8266__H__

#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>   // 提供 atoi
#include <stdint.h>   // 提供 uint32_t
#include "OLED.h"
#include "usart.h"

#define IP      "192.168.227.139"
#define PORT    777


#define BUFF_SIZE 4096
#define SSID "REDMI K80"
#define PASS "5201314."

// 函数声明，明确参数类型
void ESP_Start_Receive(void);   // 加 void
int ESP_SendCommand(const char* cmd, const char* resp, uint32_t timeout);
int ESP_Connect_WIFI(void);
int ESP_DisConnect_WIFI(void);
int ESP_Connect_TCPServer(const char* ServerIP, int ServerPort, uint32_t timeout);
int ESP_SendToTCPServer(const char* txData, uint32_t timeout);
uint8_t ESP_Process_Receive(void);

/**
 * @brief 解析 ESP8266 收到的 +IPD 数据
 * @param recv_data 原始接收缓冲区（含 +IPD 头）
 * @param data_len  输出：实际数据长度
 * @param data      输出：提取的数据（需提前分配空间）
 * @param data_size data 缓冲区大小
 * @return 0:成功, 负数:错误码
 */
static int parse_IPD(char* recv_data, int* data_len, char* data, int data_size);

#endif
