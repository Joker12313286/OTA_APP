#include "OTA_Client.h"
#include "esp8266_MQTT.h"
#include "W25Q64.h"
#include "OTA_Manager.h"
#include "OLED.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ================= 下载区参数 =================
 * 起始地址 0x100，保证 256B 对齐，chunk 与 Flash 页对齐
 * ============================================== */
#define OTA_DOWNLOAD_BASE        0x00000100UL
#define OTA_DOWNLOAD_MAX         (2 * 1024 * 1024UL)

#define OTA_RECV_TIMEOUT_MS      10000

/* ================= 全局状态 ================= */
OTA_Session_t g_ota_session;
OTA_State_t   g_ota_state = OTA_ST_IDLE;
OTA_Err_t     g_ota_err   = OTA_ERR_NONE;

static uint32_t s_last_chunk_tick = 0;

/* ================= CRC32 / BZIP2 =================
 * 与 Python 端一致：
 *   crcmod.mkCrcFun(0x104C11DB7, initCrc=0xFFFFFFFF, xorOut=0xFFFFFFFF)
 *   poly=0x04C11DB7, init=0xFFFFFFFF, refIn=false, refOut=false, xorOut=0xFFFFFFFF
 * ================================================= */
static uint32_t crc32_init(void)
{
    return 0xFFFFFFFFUL;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= ((uint32_t)data[i] << 24);
        for (int j = 0; j < 8; j++)
        {
            if (crc & 0x80000000UL)
                crc = (crc << 1) ^ 0x04C11DB7UL;
            else
                crc = (crc << 1);
        }
    }
    return crc;
}

static uint32_t crc32_final(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFUL;
}

/* ================= 简易 JSON 数值读取 =================
 * 找到 "key" 后取 : 后面的十进制整数
 * =================================================== */
static int json_get_int(const char *json, const char *key, int32_t *val)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    char *p = strstr(json, pattern);
    if (!p) return -1;
    p = strchr(p, ':');
    if (!p) return -1;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '"') p++;
    *val = atoi(p);
    return 0;
}

/* ================= 非对齐 Flash 写（自动拆页） ================= */
static int flash_write(uint32_t addr, const uint8_t *data, uint16_t len)
{
    while (len > 0)
    {
        uint16_t page_remain = 256 - (addr & 0xFF);
        uint16_t wlen = (len < page_remain) ? len : page_remain;

        W25Q64_PageProgram(addr, (uint8_t *)data, wlen);
        if (W25Q64_WaitBusy() != 0) return -1;

        addr += wlen;
        data += wlen;
        len  -= wlen;
    }
    return 0;
}

/* ================= 范围擦除（按 4KB 扇区对齐） ================= */
static int flash_erase_range(uint32_t addr, uint32_t size)
{
    uint32_t start = addr & ~0xFFFUL;
    uint32_t end   = (addr + size + 0xFFFUL) & ~0xFFFUL;

    for (uint32_t a = start; a < end; a += 0x1000UL)
    {
        W25Q64_SectorErase(a);
        if (W25Q64_WaitBusy() != 0) return -1;
    }
    return 0;
}

/* ================= OLED 显示辅助 ================= */
/* ================= OLED 显示辅助 ================= */
static void ota_show(const char *line1, const char *line2)
{
    OLED_Clear();
    OLED_ShowString(0,  0, (char *)(line1 ? line1 : ""), OLED_8X16);
    if (line2)
        OLED_ShowString(0, 16, (char *)line2, OLED_8X16);
    OLED_Update();
}

static void ota_show_progress(void)
{
    char tmp[24];
    sprintf(tmp, "%d/%d",
            g_ota_session.received_chunks,
            g_ota_session.total_chunks);
    OLED_ShowString(0, 32, tmp, OLED_8X16);
    OLED_Update();
}

/* ================= Init ================= */
void OTA_Client_Init(void)
{
    memset(&g_ota_session, 0, sizeof(g_ota_session));
    g_ota_state = OTA_ST_IDLE;
    g_ota_err   = OTA_ERR_NONE;
    s_last_chunk_tick = HAL_GetTick();
}

/* ================= 处理 OTA/notify ================= */
static void handle_notify(const char *json)
{
    int32_t v_version = 0, v_size = 0, v_crc = 0, v_chunk = 0, v_total = 0;

    if (json_get_int(json, "version",      &v_version) != 0 ||
        json_get_int(json, "size",         &v_size)    != 0 ||
        json_get_int(json, "crc32",        &v_crc)     != 0 ||
        json_get_int(json, "chunk_size",   &v_chunk)   != 0 ||
        json_get_int(json, "total_chunks", &v_total)   != 0)
    {
        g_ota_err   = OTA_ERR_NOTIFY_PARSE;
        g_ota_state = OTA_ST_ERROR;
        ota_show("notify parse err", NULL);
        return;
    }

    if ((uint16_t)v_version == OTA_CURRENT_VERSION)
    {
        g_ota_err   = OTA_ERR_VER_SAME;
        g_ota_state = OTA_ST_ERROR;
        ota_show("same version", NULL);
        return;
    }

    if (v_size <= 0 || (uint32_t)v_size > OTA_DOWNLOAD_MAX)
    {
        g_ota_err   = OTA_ERR_SIZE_TOO_BIG;
        g_ota_state = OTA_ST_ERROR;
        ota_show("size too big", NULL);
        return;
    }

    g_ota_session.version         = (uint16_t)v_version;
    g_ota_session.fw_size         = (uint32_t)v_size;
    g_ota_session.fw_crc32        = (uint32_t)v_crc;
    g_ota_session.chunk_size      = (uint16_t)v_chunk;
    g_ota_session.total_chunks    = (uint16_t)v_total;
    g_ota_session.received_chunks = 0;
    g_ota_session.write_offset    = 0;

    ota_show("OTA:", "erasing...");

    if (flash_erase_range(OTA_DOWNLOAD_BASE, g_ota_session.fw_size) != 0)
    {
        g_ota_err   = OTA_ERR_FLASH_WRITE;
        g_ota_state = OTA_ST_ERROR;
        ota_show("erase fail", NULL);
        return;
    }

    g_ota_state       = OTA_ST_READY;
    s_last_chunk_tick = HAL_GetTick();
    ota_show("OTA:", "ready");
}

/* ================= 处理 OTA/data ================= */
static void handle_data(const uint8_t *buf, int len)
{
    if (g_ota_state != OTA_ST_READY && g_ota_state != OTA_ST_RECEIVING)
        return;

    if (len < 2) return;

    uint16_t chunk_idx = ((uint16_t)buf[0] << 8) | buf[1];

    /* ===== 临时调试 ===== */
    char dbg[24];
    sprintf(dbg, "%d/%d", chunk_idx, g_ota_session.received_chunks);
    OLED_ShowString(52, 0, dbg, OLED_6X8);
    OLED_Update();
    /* =================== */

    const uint8_t *payload = buf + 2;
    int payload_len = len - 2;

    /* 重复块：忽略 */
    if (chunk_idx < g_ota_session.received_chunks)
        return;

    /* 乱序/跳号：错误 */
    if (chunk_idx > g_ota_session.received_chunks)
    {
        g_ota_err   = OTA_ERR_CHUNK_LOST;
        g_ota_state = OTA_ST_ERROR;
        ota_show("chunk lost", NULL);
        return;
    }

    /* 用 chunk_size 计算偏移，末块长度差异不影响地址 */
    uint32_t addr = OTA_DOWNLOAD_BASE +
                    (uint32_t)chunk_idx * g_ota_session.chunk_size;

    if (flash_write(addr, payload, (uint16_t)payload_len) != 0)
    {
        g_ota_err   = OTA_ERR_FLASH_WRITE;
        g_ota_state = OTA_ST_ERROR;
        ota_show("flash fail", NULL);
        return;
    }

    g_ota_session.received_chunks++;
    g_ota_session.write_offset += payload_len;
    g_ota_state = OTA_ST_RECEIVING;
    s_last_chunk_tick = HAL_GetTick();

    /* 每 32 块或最后一块刷新 OLED */
    if ((g_ota_session.received_chunks & 0x1F) == 0 ||
         g_ota_session.received_chunks == g_ota_session.total_chunks)
    {
        ota_show("OTA:", "recving...");
        ota_show_progress();
    }

    /* 全部收完 → 交给 Tick 做校验 */
    if (g_ota_session.received_chunks >= g_ota_session.total_chunks)
    {
        g_ota_state = OTA_ST_VERIFYING;
    }
}

/* ================= 校验 + 写标志 + 复位 ================= */
static void do_verify_and_trigger(void)
{
    ota_show("OTA:", "verifying...");

    /* 分块读回固件、累积计算 CRC32 */
    uint32_t crc = crc32_init();
    uint32_t offset = 0;
    uint8_t  buf[256];

    while (offset < g_ota_session.fw_size)
    {
        uint32_t remain = g_ota_session.fw_size - offset;
        uint16_t chunk  = (remain > 256) ? 256 : (uint16_t)remain;
        W25Q64_ReadData(OTA_DOWNLOAD_BASE + offset, buf, chunk);
        crc = crc32_update(crc, buf, chunk);
        offset += chunk;
    }
    crc = crc32_final(crc);

    if (crc != g_ota_session.fw_crc32)
    {
        g_ota_err   = OTA_ERR_CRC;
        g_ota_state = OTA_ST_ERROR;
        ota_show("CRC fail", NULL);
        return;
    }

    /* 写升级标志 */
    ota_show("OTA:", "write flag");
    if (OTA_Flag_Write(OTA_FLAG_UPDATE_PENDING) != 0)
    {
        g_ota_err   = OTA_ERR_FLASH_WRITE;
        g_ota_state = OTA_ST_ERROR;
        ota_show("flag fail", NULL);
        return;
    }

    g_ota_state = OTA_ST_DONE;

#if OTA_ENABLE_REBOOT
    ota_show("OTA:", "rebooting...");
    HAL_Delay(500);
    NVIC_SystemReset();
#else
    ota_show("OTA:", "CRC OK!");
#endif
}

/* ================= 总入口 ================= */
void OTA_Client_ParseMessage(const char *topic, uint8_t *data, int len)
{
    if (strcmp(topic, OTA_TOPIC_NOTIFY) == 0)
    {
        char json[256];
        int n = (len < 255) ? len : 255;
        memcpy(json, data, n);
        json[n] = '\0';
        handle_notify(json);
    }
    else if (strcmp(topic, OTA_TOPIC_DATA) == 0)
    {
        handle_data(data, len);
    }
    /* 其他 topic 忽略 */
}

/* ================= 主循环 Tick ================= */
void OTA_Client_Tick(void)
{
    /* 接收超时保护 */
    if (g_ota_state == OTA_ST_RECEIVING)
    {
        if (HAL_GetTick() - s_last_chunk_tick > OTA_RECV_TIMEOUT_MS)
        {
            g_ota_err   = OTA_ERR_CHUNK_LOST;
            g_ota_state = OTA_ST_ERROR;
            ota_show("OTA timeout", NULL);
        }
    }

    /* 收完 → 校验并触发 */
    if (g_ota_state == OTA_ST_VERIFYING)
    {
        do_verify_and_trigger();
    }
}
