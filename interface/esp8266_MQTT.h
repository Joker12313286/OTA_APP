#ifndef __ESP8266_MQTT_H__
#define __ESP8266_MQTT_H__

#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "OLED.h"
#include "OTA_Client.h"

/* ======================== 用户配置 ======================== */
#define WIFI_SSID          "REDMI K80"
#define WIFI_PASS          "5201314."

// MQTT 服务器配置
#define MQTT_BROKER_IP     "192.168.227.139"
#define MQTT_BROKER_PORT   1883
#define MQTT_CLIENT_ID     "ESP8266Client"
#define MQTT_USERNAME      "LIN"
#define MQTT_PASSWORD      "123"
#define MQTT_KEEPALIVE     60

// MQTT 主题配置
#define MQTT_SUB_TOPIC     "a"
#define MQTT_PUB_TOPIC     "a"
#define MQTT_QOS           0

/* 缓冲区大小，别超过 F103C8 RAM（20KB） */
#define MQTT_BUFF_SIZE     2048
#define MQTT_DATA_SIZE     1024
#define MQTT_TOPIC_SIZE    64

#define OTA_CHUNK_SIZE     256

extern volatile uint32_t g_rx_cb_cnt;
/* ======================== API 接口 ======================== */

/* 启动 UART 空闲+DMA 接收（必须先调用） */
void ESP_MQTT_StartReceive(void);

/* 连接 WiFi (AT+CWMODE=1 -> AT+CWJAP) */
int  ESP_MQTT_ConnectWIFI(void);

/* 配置 MQTT 用户信息 (AT+MQTTUSERCFG) */
int  ESP_MQTT_UserConfig(void);

/* 连接 MQTT Broker (AT+MQTTCONN) */
int  ESP_MQTT_Connect(void);

/* 订阅主题 (AT+MQTTSUB) */
int  ESP_MQTT_Subscribe(const char *topic);

/* 发布消息 (AT+MQTTPUB) */
int  ESP_MQTT_Publish(const char *topic, const char *data, int data_len);

/* 断开 MQTT (AT+MQTTCLEAN) */
int  ESP_MQTT_Disconnect(void);

/* 一键初始化：StartReceive + WIFI + UserConfig + Connect + Subscribe */
int  ESP_MQTT_Init(void);

/* 轮询处理收到的 MQTT 数据；返回 1=解析到一条有效数据，0=无 */
uint8_t ESP_MQTT_ProcessReceive(void);

/* ESP8266上电后的等待函数，避免其反馈出来的垃圾日志 */
int ESP_MQTT_WaitReady(void);

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);

/* 最近一次解析出来的数据（供外部直接读） */
extern char ESP_MQTT_LastData[MQTT_DATA_SIZE];
extern char ESP_MQTT_LastTopic[MQTT_TOPIC_SIZE];
extern int  ESP_MQTT_LastDataLen;

#endif
