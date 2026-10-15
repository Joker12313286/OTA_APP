#ifndef __OTA_CLIENT_H__
#define __OTA_CLIENT_H__

#include "main.h"
#include <stdint.h>

/* ================= 配置 ================= */
#define OTA_CHUNK_SIZE           256
#define OTA_FW_MAX_SIZE          (2 * 1024 * 1024)   /* 下载区上限 2MB，按 W25Q64 规划调 */

/* MQTT 主题 */
#define OTA_TOPIC_NOTIFY         "OTA/notify"
#define OTA_TOPIC_DATA           "OTA/data"
#define OTA_TOPIC_STATUS         "OTA/status"
#define OTA_TOPIC_LOG            "OTA/log"

/* 当前固件版本（主<<8 | 次）。发布新版时这里和 Python 都要同步改 */
#define OTA_CURRENT_VERSION      0x0100    /* 1.0 */

/* 调试开关：第一次测试把 REBOOT 设为 0，只打印结果不重启 */
#define OTA_ENABLE_REBOOT        0

/* ================= 状态机 ================= */
typedef enum {
    OTA_ST_IDLE = 0,
    OTA_ST_READY,          /* 收到 notify，已擦除，等数据 */
    OTA_ST_RECEIVING,      /* 正在收数据块 */
    OTA_ST_VERIFYING,      /* 收完，正在校验 */
    OTA_ST_DONE,           /* 校验通过，已写标志 */
    OTA_ST_ERROR
} OTA_State_t;

/* ================= 错误码 ================= */
typedef enum {
    OTA_ERR_NONE            = 0,
    OTA_ERR_VER_SAME        = 1,
    OTA_ERR_SIZE_TOO_BIG    = 2,
    OTA_ERR_FLASH_WRITE     = 3,
    OTA_ERR_CRC             = 4,
    OTA_ERR_CHUNK_LOST      = 5,
    OTA_ERR_NOTIFY_PARSE    = 6,
} OTA_Err_t;

/* ================= 会话信息 ================= */
typedef struct {
    uint16_t version;
    uint32_t fw_size;
    uint32_t fw_crc32;
    uint16_t chunk_size;
    uint16_t total_chunks;
    uint16_t received_chunks;
    uint32_t write_offset;
} OTA_Session_t;

extern OTA_Session_t g_ota_session;
extern OTA_State_t   g_ota_state;
extern OTA_Err_t     g_ota_err;

/* ================= 接口 ================= */
void OTA_Client_Init(void);
void OTA_Client_ParseMessage(const char *topic, uint8_t *data, int len);
void OTA_Client_Tick(void);      /* 主循环每帧调用一次，处理超时和收完后的校验 */

#endif
