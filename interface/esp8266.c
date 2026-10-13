#include "esp8266.h"

uint8_t ESP_BUFF[BUFF_SIZE] = {0};
volatile uint16_t ESP_RX_LEN = 0;
volatile uint8_t ESP_RX_COMPLETE = 0;

extern UART_HandleTypeDef huart1;

/* void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart == &huart1)
    {
        ESP_RX_LEN = Size;
        ESP_RX_COMPLETE = 1; // 标记有新数据
    }
} */

void ESP_Start_Receive()
{
    memset(ESP_BUFF, 0, 1024);
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, ESP_BUFF, BUFF_SIZE);
}

int ESP_SendCommand(const char *cmd, const char *resp, uint32_t timeout)
{
    // 1. 清空接收状态，确保不会读到旧数据
    ESP_RX_COMPLETE = 0;
    ESP_RX_LEN = 0;
    memset(ESP_BUFF, 0, BUFF_SIZE);
    // 2. 发送命令
    HAL_UART_Transmit_DMA(&huart1, (uint8_t *)cmd, strlen(cmd));
    // 3. 等待响应
    uint32_t start_tick = HAL_GetTick();
    while (HAL_GetTick() - start_tick < timeout)
    {
        if (strstr((char *)ESP_BUFF, resp) != NULL)
        {
            return 0;
        }
        HAL_Delay(10);
    }
    return -1;
}

int ESP_Connect_WIFI(void)
{
    // 设置esp8266模式
    ESP_SendCommand("AT+CWMODE=1", "OK", 1000);

    // 连接wifi，SSID为wifi名，PASS为密码
    char cmd[100] = {0};
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", SSID, PASS);
    ESP_SendCommand(cmd, "OK", 10000);
    return 0;
}

int ESP_DisConnect_WIFI(void)
{
    // 断开连接 AT+CWQAP
    return 0;
}

int ESP_Connect_TCPServer(const char *ServerIP, int ServerPort, uint32_t timeout)
{
    // 5. 设置单路连接
    //ESP_SendCommand("AT+CIPMUX=0\r\n", "OK", 1000);
    char cmd[100] = {0};
    sprintf(cmd, "AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", ServerIP, ServerPort);

    if (0 == ESP_SendCommand(cmd, "OK", timeout))
    {
        return 0;
    }
    return -1;
}

int ESP_SendToTCPServer(const char* txData, uint32_t timeout)
{
    char cmd[100]={0};
    sprintf(cmd,"AT+CIPSEND=%d\r\n",strlen(txData));
    HAL_UART_Transmit_DMA(&huart1,(uint8_t*)cmd,strlen(cmd));
    HAL_Delay(100);
    if(0==ESP_SendCommand(txData,"SEND OK",timeout))
    {
        return 0;
    }
    return -1;
}


/**
 * @brief 解析 ESP8266 收到的 +IPD 数据
 * @param recv_data 原始接收缓冲区（含 +IPD 头）
 * @param data_len  输出：实际数据长度
 * @param data      输出：提取的数据（需提前分配空间）
 * @param data_size data 缓冲区大小
 * @return 0:成功, 负数:错误码
 */
static int parse_IPD(char *recv_data, int *data_len, char *data, int data_size)
{
    const char *prefix = "+IPD";
    char *start = strstr(recv_data, prefix);
    if (!start)
        return -1; // 未找到头部

    start += strlen(prefix); // 跳过 "+IPD"

    // 尝试解析格式1: +IPD,<len>:data   （单连接）
    int len1 = 0;
    char *colon = strchr(start, ':');
    if (colon)
    {
        // 检查逗号后是否直接跟数字（即无 link_id）
        char *comma = strchr(start, ',');
        if (comma && comma < colon)
        {
            // 有逗号，可能有多连接，先尝试解析多连接格式
            int link_id = 0;
            int scanned = sscanf(start, ",%d,%d:", &link_id, &len1);
            if (scanned == 2)
            {
                // 多连接模式成功
                *data_len = len1;
                if (*data_len >= data_size)
                    return -4; // 缓冲区不足
                strncpy(data, colon + 1, *data_len);
                data[*data_len] = '\0';
                return 0;
            }
        }
        // 否则按单连接解析: 直接取逗号后的数字作为长度
        // 格式: +IPD,<len>:data
        comma = strchr(start, ',');
        if (comma)
        {
            len1 = atoi(comma + 1);
            *data_len = len1;
            if (*data_len >= data_size)
                return -4;
            strncpy(data, colon + 1, *data_len);
            data[*data_len] = '\0';
            return 0;
        }
    }
    return -2; // 格式错误
}

uint8_t ESP_Process_Receive(void)
{
    OLED_Printf(0, 0, OLED_8X16, "%d", ESP_RX_COMPLETE);
    OLED_Update();
    if (!ESP_RX_COMPLETE)
    {
        return 0; // 无新数据
    }

    // 1. 复制数据到临时缓冲区（防止被下一包覆盖）
    uint8_t temp_buf[BUFF_SIZE];
    uint16_t len = ESP_RX_LEN;
    if(len>BUFF_SIZE)len=BUFF_SIZE;
    memcpy(temp_buf, ESP_BUFF, len);
    temp_buf[len]='\0'; // 确保字符串结束

    // 2. 清空接收缓冲区，重启 DMA
    ESP_RX_COMPLETE = 0;
    ESP_RX_LEN = 0;
    memset(ESP_BUFF, 0, BUFF_SIZE);
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, ESP_BUFF, BUFF_SIZE);

    // 3. 解析 IPD 数据
    int data_len = 0;
    uint8_t data_buf[1024] = {0};
    int16_t parse_result = parse_IPD((char *)temp_buf, &data_len, (char *)data_buf, sizeof(data_buf));
    OLED_Clear();
    OLED_Printf(64, 16, OLED_8X16, "len:%+0d", parse_result);
    OLED_Update();
    if (parse_result == 0)
    {
        // 4. 解析成功！处理数据
        // 🔥 这里可以调用你的 OTA 写入函数
        // 例如：W25Q64_PageProgram(OTA_FW_DOWNLOAD_ADDR + current_offset, data_buf, data_len);

        // 临时显示到 OLED（调试用）
        OLED_ShowString(0, 0, "RECV:", OLED_8X16);
        OLED_ShowString(0, 16, (char *)data_buf, OLED_8X16);

        char len_str[20];
        sprintf(len_str, "Len:%d", data_len);
        OLED_ShowString(0, 32, len_str, OLED_8X16);
        OLED_Update();

        return 1;
    }
    else
    {
        // 解析失败，显示错误码
        OLED_Clear();
        OLED_Printf(0, 0, OLED_8X16, "Parse Err:%d", parse_result);
        OLED_Update();
        return 0;
    }
    return 0;
}
