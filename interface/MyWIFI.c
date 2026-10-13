#include "MyWIFI.h"


uint8_t ESP_BUFF[BUFF_SIZE] = {0};
volatile uint16_t ESP_RX_LEN = 0;        // 本次接收到的数据长度
volatile uint8_t ESP_RX_COMPLETE = 0;    // 1=收到一帧完整数据

extern UART_HandleTypeDef huart1;

/**
  * @brief UART 空闲中断回调（DMA 接收完成）
  * @note  HAL 库自动调用，不能加 static
  */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART1)
    {
        ESP_RX_LEN = Size;
        ESP_RX_COMPLETE = 1;   // 标记收到数据
    }
}

int ESP_SendCommand(const char* cmd,const char* resp,uint32_t timeout)
{
    HAL_UART_Transmit_DMA(&huart1,(uint8_t*)cmd,strlen(cmd));
    uint32_t start_tick = HAL_GetTick();
    while (HAL_GetTick()-start_tick<timeout)
    {
        if(strstr((char*)ESP_BUFF,resp)!=NULL)
        {
            return 0;
        }
    }
    return -1;
}

/**
  * @brief 启动 DMA 接收（在 main 中调用一次）
  */
void ESP_Start_Receive(void)
{
    memset(ESP_BUFF, 0, BUFF_SIZE);
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, ESP_BUFF, BUFF_SIZE);
}

/**
  * @brief 检查是否收到新数据（非阻塞）
  * @param data  输出缓冲区
  * @param len   输出数据长度
  * @return 1:有新数据, 0:无数据
  */
uint8_t ESP_Check_Receive(uint8_t *data, uint16_t *len)
{
    if (ESP_RX_COMPLETE)
    {
        ESP_RX_COMPLETE = 0;
        *len = ESP_RX_LEN;
        // 拷贝数据到用户缓冲区（避免被下一次 DMA 覆盖）
        memcpy(data, ESP_BUFF, ESP_RX_LEN);
        // 重启 DMA 接收
        memset(ESP_BUFF, 0, BUFF_SIZE);
        HAL_UARTEx_ReceiveToIdle_DMA(&huart1, ESP_BUFF, BUFF_SIZE);
        return 1;
    }
    return 0;
}

/**
  * @brief 初始化 ESP8266 进入透传模式
  * @return 0:成功, 1:失败
  */
uint8_t ESP_Init_Transparent(void)
{
    uint8_t retry = 3;

    // 1. 恢复出厂设置
    while (retry--) {
        ESP_SendCommand("AT+RST\r\n", "ready", 2000);
        HAL_Delay(1000);
        if (ESP_SendCommand("AT\r\n", "OK", 1000) == 0) break;
    }
    if (retry == 0) return 1;

    // 2. 设置 WiFi 模式
    ESP_SendCommand("AT+CWMODE=1\r\n", "OK", 1000);

    // 3. 连接 WiFi
    char cmd[100];
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", SSID, PASS);
    ESP_SendCommand(cmd, "OK", 10000);

    // 4. 设置单路连接
    ESP_SendCommand("AT+CIPMUX=0\r\n", "OK", 1000);

    // 5. 连接 TCP 服务器
    sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", IP, PORT);
    ESP_SendCommand(cmd, "CONNECT", 5000);

    // 6. 进入透传模式
    ESP_SendCommand("AT+CIPMODE=1\r\n", "OK", 1000);
    ESP_SendCommand("AT+CIPSEND\r\n", ">", 1000);

    return 0;
}

/**
  * @brief 发送数据（透传模式）
  */
void ESP_Send_Data(uint8_t *data, uint16_t len)
{
    HAL_UART_Transmit_DMA(&huart1, data, len);
}
