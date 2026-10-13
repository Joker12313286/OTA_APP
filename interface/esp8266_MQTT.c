#include "esp8266_MQTT.h"
#include "usart.h"
#include "OLED.h"

extern UART_HandleTypeDef huart1;


/* DMA 接收缓冲，仅用于异步 MQTT 消息 */
static uint8_t           s_rx_buff[MQTT_BUFF_SIZE] = {0};
static volatile uint16_t s_rx_len   = 0;
static volatile uint8_t  s_rx_ready = 0;

/* 对外暴露：最近一次收到的 MQTT 数据 */
char ESP_MQTT_LastData[MQTT_DATA_SIZE]   = {0};
char ESP_MQTT_LastTopic[MQTT_TOPIC_SIZE] = {0};
int  ESP_MQTT_LastDataLen = 0;

/* ====================== UART 回调 ======================
 * 只标记，不重启 DMA（重启由上层统一管理，避免状态打架）
 * ====================================================== */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart == &huart1)
    {
        s_rx_len   = Size;
        s_rx_ready = 1;
    }
}

/* ====================== 启动 DMA 接收（异步阶段用） ====================== */
void ESP_MQTT_StartReceive(void)
{
    HAL_UART_AbortReceive(&huart1);
    HAL_Delay(10);          /* 给状态机恢复时间 */
    memset(s_rx_buff, 0, MQTT_BUFF_SIZE);
    s_rx_len   = 0;
    s_rx_ready = 0;
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, s_rx_buff, MQTT_BUFF_SIZE);
}

/* ====================== 阻塞式 AT 命令交互 ====================== */
static int mqtt_send_cmd(const char *cmd, const char *resp, uint32_t timeout)
{
    uint16_t idx = 0;

    memset(s_rx_buff, 0, MQTT_BUFF_SIZE);
    s_rx_len   = 0;
    s_rx_ready = 0;

    HAL_UART_AbortReceive(&huart1);
    HAL_Delay(5);

    HAL_UART_Transmit(&huart1, (uint8_t *)cmd, strlen(cmd), 1000);

    uint32_t start = HAL_GetTick();
    while (HAL_GetTick() - start < timeout)
    {
        uint8_t ch;
        if (HAL_UART_Receive(&huart1, &ch, 1, 20) == HAL_OK)
        {
            if (idx < MQTT_BUFF_SIZE - 1)
                s_rx_buff[idx++] = ch;
            s_rx_buff[idx] = '\0';
            s_rx_len = idx;

            if (strstr((char *)s_rx_buff, resp) != NULL)
                return 0;
            if (strstr((char *)s_rx_buff, "ERROR") != NULL ||
                strstr((char *)s_rx_buff, "FAIL")  != NULL)
                return -2;
        }
    }

    /* 超时：OLED 显示收到的前 16 个字符，便于调试 */
    char disp[17] = {0};
    for (int j = 0; j < 16 && j < idx; j++)
        disp[j] = (s_rx_buff[j] >= 32 && s_rx_buff[j] < 127) ? s_rx_buff[j] : '.';
    OLED_Printf(0, 48, OLED_8X16, "to:%s", disp);
    OLED_Update();
    return -1;
}

/* ====================== 上电握手 ====================== */
int ESP_MQTT_WaitReady(void)
{
    HAL_Delay(2000);    /* 等 ESP8266 上电日志吐完 */

    for (int i = 0; i < 5; i++)
    {
        if (mqtt_send_cmd("AT\r\n", "OK", 1000) == 0)
        {
            mqtt_send_cmd("ATE0\r\n", "OK", 1000);   /* 关回显 */
            return 0;
        }
        HAL_Delay(500);
    }
    return -1;
}

/* ====================== WiFi 连接 ====================== */
int ESP_MQTT_ConnectWIFI(void)
{
    int ret;

    /* 1) 设 Station 模式 */
    ret = mqtt_send_cmd("AT+CWMODE=1\r\n", "OK", 2000);
    OLED_Printf(0,  0, OLED_8X16, "CWMODE:%d", ret);
    OLED_Update();
    HAL_Delay(500);
    if (ret != 0) return -1;

    /* 2) 查询是否已连上目标 WiFi */
    ret = mqtt_send_cmd("AT+CWJAP?\r\n", "OK", 2000);
    if (ret == 0 && strstr((char *)s_rx_buff, WIFI_SSID) != NULL)
    {
        OLED_Printf(0, 16, OLED_8X16, "already conn");
        OLED_Update();
        HAL_Delay(500);
        return 0;
    }

    /* 3) 未连上，尝试连接 */
    char cmd[128] = {0};
    sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASS);
    ret = mqtt_send_cmd(cmd, "OK", 30000);
    OLED_Printf(0, 32, OLED_8X16, "CWJAP:%d", ret);
    OLED_Update();
    HAL_Delay(800);
    if (ret != 0) return -2;

    return 0;
}

/* ====================== MQTT 用户配置 ====================== */
int ESP_MQTT_UserConfig(void)
{
    char cmd[256] = {0};
    sprintf(cmd,
            "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
            MQTT_CLIENT_ID, MQTT_USERNAME, MQTT_PASSWORD);
    if (mqtt_send_cmd(cmd, "OK", 2000) != 0) return -1;
    return 0;
}

/* ====================== MQTT 连接 ====================== */
int ESP_MQTT_Connect(void)
{
    char cmd[128] = {0};
    sprintf(cmd, "AT+MQTTCONN=0,\"%s\",%d,%d\r\n",
            MQTT_BROKER_IP, MQTT_BROKER_PORT, 1);
    if (mqtt_send_cmd(cmd, "OK", 10000) != 0) return -1;
    return 0;
}

/* ====================== 订阅话题 ====================== */
int ESP_MQTT_Subscribe(const char *topic)
{
    char cmd[128] = {0};
    sprintf(cmd, "AT+MQTTSUB=0,\"%s\",%d\r\n", topic, MQTT_QOS);
    if (mqtt_send_cmd(cmd, "OK", 5000) != 0) return -1;
    return 0;
}

/* ====================== 发布消息 ====================== */
int ESP_MQTT_Publish(const char *topic, const char *data, int data_len)
{
    char cmd[256 + MQTT_DATA_SIZE] = {0};
    int n = snprintf(cmd, sizeof(cmd),
                     "AT+MQTTPUB=0,\"%s\",\"", topic);

    for (int i = 0; i < data_len && n < (int)sizeof(cmd) - 8; i++)
    {
        if (data[i] == '"' || data[i] == '\\')
            cmd[n++] = '\\';
        cmd[n++] = data[i];
    }
    n += snprintf(cmd + n, sizeof(cmd) - n, "\",%d,0\r\n", MQTT_QOS);

    if (mqtt_send_cmd(cmd, "OK", 5000) != 0) return -1;

    /* 发布完后重新启动 DMA 接收，继续监听异步消息 */
    ESP_MQTT_StartReceive();
    return 0;
}

/* ====================== 断开 MQTT ====================== */
int ESP_MQTT_Disconnect(void)
{
    if (mqtt_send_cmd("AT+MQTTCLEAN=0\r\n", "OK", 2000) != 0) return -1;
    return 0;
}

/* ====================== 一键初始化 ====================== */
int ESP_MQTT_Init(void)
{
    if (ESP_MQTT_WaitReady()   != 0) return -1;
    if (ESP_MQTT_ConnectWIFI() != 0) return -2;
    if (ESP_MQTT_UserConfig()  != 0) return -3;
    if (ESP_MQTT_Connect()     != 0) return -4;
    if (ESP_MQTT_Subscribe(MQTT_SUB_TOPIC) != 0) return -5;

    /* 所有 AT 交互完成，启动 DMA 进入异步消息接收阶段 */
    ESP_MQTT_StartReceive();
    return 0;
}

/* ====================== 解析 MQTT 订阅消息 ======================
 * 格式：+MQTTSUBRECV:<link_id>,"<topic>",<data_len>,<data>
 * ============================================================== */
static int parse_MQTTSUBRECV(char *recv,
                             char *topic, int  topic_size,
                             char *data,  int  data_size,
                             int  *data_len)
{
    const char *prefix = "+MQTTSUBRECV:";
    char *p = strstr(recv, prefix);
    if (!p) return -1;

    p += strlen(prefix);
    p = strchr(p, ',');
    if (!p) return -2;
    p++;

    if (*p != '"') return -3;
    p++;
    char *topic_end = strchr(p, '"');
    if (!topic_end) return -4;

    int tlen = topic_end - p;
    if (tlen >= topic_size) tlen = topic_size - 1;
    memcpy(topic, p, tlen);
    topic[tlen] = '\0';

    p = topic_end + 1;
    if (*p != ',') return -5;
    p++;

    int len = atoi(p);
    p = strchr(p, ',');
    if (!p) return -6;
    p++;

    if (len < 0 || len >= data_size) return -7;

    memcpy(data, p, len);
    data[len] = '\0';
    *data_len = len;

    return 0;
}

/* ====================== 轮询处理异步接收 ====================== */
uint8_t ESP_MQTT_ProcessReceive(void)
{
    if (!s_rx_ready) return 0;

    /* 拷贝一份，防止被下一包覆盖 */
    uint8_t  temp[MQTT_BUFF_SIZE];
    uint16_t len = s_rx_len;
    if (len > MQTT_BUFF_SIZE) len = MQTT_BUFF_SIZE;
    memcpy(temp, s_rx_buff, len);
    temp[len] = '\0';

    /* 清状态并重启 DMA */
    s_rx_ready = 0;
    s_rx_len   = 0;
    memset(s_rx_buff, 0, MQTT_BUFF_SIZE);
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, s_rx_buff, MQTT_BUFF_SIZE);

    /* 解析是否含订阅消息 */
    int data_len = 0;
    int ret = parse_MQTTSUBRECV((char *)temp,
                                ESP_MQTT_LastTopic, MQTT_TOPIC_SIZE,
                                ESP_MQTT_LastData,  MQTT_DATA_SIZE,
                                &data_len);
    if (ret != 0)
    {
        /* 不是订阅消息，是 AT 异步响应，忽略 */
        return 0;
    }

    ESP_MQTT_LastDataLen = data_len;

    /* 调试显示 */
    OLED_Clear();
    OLED_ShowString(0,  0, "MQTT RECV:", OLED_8X16);
    OLED_ShowString(0, 16, ESP_MQTT_LastTopic, OLED_8X16);
    OLED_ShowString(0, 32, ESP_MQTT_LastData,  OLED_8X16);
    char tmp[24];
    sprintf(tmp, "Len:%d", data_len);
    OLED_ShowString(0, 48, tmp, OLED_8X16);
    OLED_Update();

    /* TODO: 接 OTA 时在这里调 W25Q64_PageProgram 写 flash */
    return 1;
}
