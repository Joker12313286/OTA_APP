#include "esp8266_MQTT.h"
#include "usart.h"
#include "OLED.h"

extern UART_HandleTypeDef huart1;
volatile uint32_t g_rx_cb_cnt = 0;

/* ==================== 多缓冲环形队列 ==================== */
#define DMA_BUF_NUM  4

static uint8_t  s_dma_buf[DMA_BUF_NUM][MQTT_BUFF_SIZE] = {0};
static volatile uint8_t  s_dma_widx  = 0;       /* DMA 当前写哪块 */
static volatile uint8_t  s_dma_ridx  = 0;       /* 主循环读哪块 */
static volatile uint16_t s_dma_len[DMA_BUF_NUM] = {0};
static volatile uint8_t  s_ready_cnt = 0;       /* 队列中待处理块数 */

/* 累积缓冲 */
static uint8_t  s_accum[MQTT_BUFF_SIZE * 2] = {0};
static uint16_t s_accum_len = 0;

/* 阻塞 AT 命令用的临时缓冲（独立） */
static uint8_t           s_rx_buff[MQTT_BUFF_SIZE] = {0};
static volatile uint16_t s_rx_len   = 0;
static volatile uint8_t  s_rx_ready = 0;

/* 对外暴露 */
char ESP_MQTT_LastData[MQTT_DATA_SIZE]   = {0};
char ESP_MQTT_LastTopic[MQTT_TOPIC_SIZE] = {0};
int  ESP_MQTT_LastDataLen = 0;

/* ====================== UART 回调 ======================
利用多缓冲 DMA + 空闲中断，接收数据到环形队列中，主循环再处理
 * ====================================================== */
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart != &huart1) return;
    g_rx_cb_cnt++;

    /* 当前块写完，入队 */
    s_dma_len[s_dma_widx] = Size;
    s_ready_cnt++;

    /* 队列满：丢最老的一块（保护当前已经写入的数据） */
    if (s_ready_cnt > DMA_BUF_NUM)
    {
        s_ready_cnt = DMA_BUF_NUM;
        s_dma_ridx  = (s_dma_ridx + 1) % DMA_BUF_NUM;
    }

    /* 切到下一块，重启 DMA */
    s_dma_widx = (s_dma_widx + 1) % DMA_BUF_NUM;
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, s_dma_buf[s_dma_widx], MQTT_BUFF_SIZE);
}

/* ====================== 启动 DMA 接收（异步阶段用） ====================== */
void ESP_MQTT_StartReceive(void)
{
    HAL_UART_AbortReceive(&huart1);
    HAL_Delay(10);

    memset(s_dma_buf, 0, sizeof(s_dma_buf));
    s_dma_widx  = 0;
    s_dma_ridx  = 0;
    s_ready_cnt = 0;
    s_accum_len = 0;

    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, s_dma_buf[s_dma_widx], MQTT_BUFF_SIZE);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart != &huart1) return;

    /* 清除所有错误标志 */
    __HAL_UART_CLEAR_OREFLAG(huart);
    __HAL_UART_CLEAR_PEFLAG(huart);
    __HAL_UART_CLEAR_FEFLAG(huart);
    __HAL_UART_CLEAR_NEFLAG(huart);

    /* 重启 DMA，否则 UART 会永久停收 */
    HAL_UARTEx_ReceiveToIdle_DMA(&huart1, s_dma_buf[s_dma_widx], MQTT_BUFF_SIZE);

    /* 上报错误 */
    char log[48];
    sprintf(log, "{\"ev\":\"uart_err\",\"code\":%lu}", huart->ErrorCode);
    ESP_MQTT_Publish("OTA/log", log, strlen(log));
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
    if (ESP_MQTT_Subscribe(MQTT_SUB_TOPIC)      != 0) return -5;
    if (ESP_MQTT_Subscribe(OTA_TOPIC_NOTIFY)    != 0) return -6;   /* "OTA/notify" */
    if (ESP_MQTT_Subscribe(OTA_TOPIC_DATA)      != 0) return -7;   /* "OTA/data" */

    /* 所有 AT 交互完成，启动 DMA 进入异步消息接收阶段 */
    ESP_MQTT_StartReceive();
    return 0;
}

/* ====================== 解析单条 +MQTTSUBRECV ======================
 * 输入:  buf[0..buf_len-1]  原始缓冲
 * 输出:  *consumed  本条消息占用的字节数（用于跳过继续找下一条）
 *        topic / data / data_len
 * 返回:  0=成功, -1=没有头部, -2=格式错或数据不全
 *
 * 注意：数据段是二进制，不能当字符串处理，全程用长度 + memcpy
 * ==================================================================== */
static int parse_one_mqttrecv(const uint8_t *buf, uint16_t buf_len,
                              uint16_t *consumed,
                              char *topic, int topic_size,
                              uint8_t *data, int data_size,
                              int *data_len)
{
    const char *prefix = "+MQTTSUBRECV:";
    const int  prefix_len = 12;   /* "+MQTTSUBRECV:" 共 12 字节 */
    int pos = -1;

    /* 1) 找头部（用 memcmp，不用 strstr，避免数据里碰巧含 0x00 干扰） */
    if (buf_len < prefix_len) return -1;
    for (int i = 0; i <= (int)buf_len - prefix_len; i++)
    {
        if (memcmp(&buf[i], prefix, prefix_len) == 0)
        {
            pos = i;
            break;
        }
    }
    if (pos < 0) return -1;

    int cursor = pos + prefix_len;

    /* 2) 跳过 link_id（形如 "0,"），找第一个逗号 */
    while (cursor < buf_len && buf[cursor] != ',') cursor++;
    if (cursor >= buf_len) return -2;
    cursor++;

    /* 3) 期望 '"' 开头的 topic */
    if (cursor >= buf_len || buf[cursor] != '"') return -2;
    cursor++;
    int t_start = cursor;
    while (cursor < buf_len && buf[cursor] != '"') cursor++;
    if (cursor >= buf_len) return -2;
    int t_len = cursor - t_start;
    if (t_len >= topic_size) t_len = topic_size - 1;
    memcpy(topic, &buf[t_start], t_len);
    topic[t_len] = '\0';
    cursor++;   /* 跳过收尾的 '"' */

    /* 4) 期望 ',' */
    if (cursor >= buf_len || buf[cursor] != ',') return -2;
    cursor++;

    /* 5) 解析 data_len（十进制数字） */
    int len = 0;
    while (cursor < buf_len && buf[cursor] >= '0' && buf[cursor] <= '9')
    {
        len = len * 10 + (buf[cursor] - '0');
        cursor++;
    }
    if (cursor >= buf_len || buf[cursor] != ',') return -2;
    cursor++;

    /* 6) 数据段从 cursor 开始，长 len 字节 */
    if (cursor + len > buf_len) return -2;   /* 数据不完整 */
    if (len >= data_size)       return -2;   /* 目标缓冲太小 */

    memcpy(data, &buf[cursor], len);
    data[len] = '\0';    /* 便于调试打印；二进制处理时请用 *data_len */

    *data_len = len;
    *consumed = cursor + len;
    return 0;
}


/* ====================== 轮询处理异步接收 ====================== */
uint8_t ESP_MQTT_ProcessReceive(void)
{
    /* 把队列里所有就绪块依次搬进累积缓冲 */
    while (s_ready_cnt > 0)
    {
        uint8_t  idx;
        uint16_t len;

        __disable_irq();
        idx = s_dma_ridx;
        len = s_dma_len[idx];
        s_dma_ridx = (s_dma_ridx + 1) % DMA_BUF_NUM;
        s_ready_cnt--;
        __enable_irq();

        if (len > MQTT_BUFF_SIZE) len = MQTT_BUFF_SIZE;

        if (s_accum_len + len > sizeof(s_accum))
        {
            /* 累积缓冲溢出：清空（正常不该发生，说明主循环实在太慢） */
            s_accum_len = 0;
        }
        memcpy(&s_accum[s_accum_len], s_dma_buf[idx], len);
        s_accum_len += len;
    }

    if (s_accum_len == 0) return 0;

    /* 循环解析累积缓冲里所有完整的 +MQTTSUBRECV 帧 */
    uint16_t offset = 0;
    uint8_t  got_any = 0;

    while (offset < s_accum_len)
    {
        uint16_t consumed = 0;
        int data_len = 0;

        int ret = parse_one_mqttrecv(&s_accum[offset],
                                     s_accum_len - offset,
                                     &consumed,
                                     ESP_MQTT_LastTopic, MQTT_TOPIC_SIZE,
                                     (uint8_t *)ESP_MQTT_LastData, MQTT_DATA_SIZE,
                                     &data_len);

        if (ret == -1)
        {
            /* 完全没有头部 —— 整段都是 AT 异步消息，丢弃 */
            offset = s_accum_len;
            break;
        }
        if (ret == -2)
        {
            /* 头部在，但数据不完整 —— 保留从 offset 开始的，等下次 */
            break;
        }

        /* ---- 解析成功一条 ---- */
        ESP_MQTT_LastDataLen = data_len;
        got_any = 1;

        /* 调试显示 */
        OLED_Clear();
        OLED_ShowString(0,  0, "MQTT RECV:", OLED_8X16);
        OLED_ShowString(0, 16, ESP_MQTT_LastTopic, OLED_8X16);
        char disp[17] = {0};
        for (int j = 0; j < 16 && j < data_len; j++)
            disp[j] = (ESP_MQTT_LastData[j] >= 32 && ESP_MQTT_LastData[j] < 127)
                      ? ESP_MQTT_LastData[j] : '.';
        OLED_ShowString(0, 32, disp, OLED_8X16);
        char tmp[24];
        sprintf(tmp, "Len:%d", data_len);
        OLED_ShowString(0, 48, tmp, OLED_8X16);
        OLED_Update();

        /* TODO: 阶段2 完成后在这里调 OTA_Client_ParseMessage(...) */
        OTA_Client_ParseMessage(ESP_MQTT_LastTopic,
                        (uint8_t *)ESP_MQTT_LastData,
                        ESP_MQTT_LastDataLen);
        offset += consumed;
    }

    /* 把剩下的（未解析完的尾部）挪到缓冲开头 */
    if (offset > 0)
    {
        uint16_t remain = s_accum_len - offset;
        if (remain > 0) memmove(s_accum, &s_accum[offset], remain);
        s_accum_len = remain;
    }

    return got_any;
}
