#include "MyCAN.h"


/**
 * @brief 初始化CAN 配置白名单过滤器 开启CAN
 */
void MYCAN_Init(void)
{
    CAN_FilterTypeDef filterConfig = {0};
    filterConfig.FilterBank = 0;
    filterConfig.FilterMode = CAN_FILTERMODE_IDMASK;
    filterConfig.FilterScale = CAN_FILTERSCALE_32BIT;
    filterConfig.FilterIdHigh = 0x0000;
    filterConfig.FilterIdLow = 0x0000;
    filterConfig.FilterMaskIdHigh = 0x0000;
    filterConfig.FilterMaskIdLow = 0x0000;
    filterConfig.FilterFIFOAssignment = CAN_RX_FIFO0;
    filterConfig.FilterActivation = ENABLE;
    filterConfig.SlaveStartFilterBank = 14; 
    HAL_CAN_ConfigFilter(&hcan, &filterConfig);
    HAL_CAN_Start(&hcan);
}

/**
 * @brief 发送CAN数据函数
 * @param id 发送的CAN ID(默认数据格式 标准ID)
 * @param data 发送的数据(一次性最多发8位)
 * @param len 发送的数据长度
 */
void MYCAN_send(uint16_t id, uint8_t *data, uint8_t len)
{
    //等待空闲
    while(HAL_CAN_GetTxMailboxesFreeLevel(&hcan) == 0);
    //配置过滤器
    CAN_TxHeaderTypeDef pHeader = {0};
    pHeader.StdId = id;
    pHeader.IDE = CAN_ID_STD;
    pHeader.RTR = CAN_RTR_DATA;
    pHeader.DLC = len;
    pHeader.TransmitGlobalTime = DISABLE;
    uint32_t pTxMailbox;
    //发送邮箱
    HAL_CAN_AddTxMessage(&hcan,&pHeader,data,&pTxMailbox);
}

/**
 * @brief 接收CAN数据函数
 * @param mycan_rec 接收的CAN数据结构体
 * @param rec_conut 接收数据个数
 * @return 接收的数据长度
 */
void MYCAN_receive(MYCAN_rec *mycan_rec,uint8_t *rec_count)
{
    *rec_count = HAL_CAN_GetRxFifoFillLevel(&hcan,CAN_RX_FIFO0);
    for(uint8_t i=0;i<*rec_count;i++)
    {
        MYCAN_rec *p_rec = &mycan_rec[i];
        //清空对应消息缓存
        memset(p_rec,0,sizeof(MYCAN_rec));
        //读取一条消息
        HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &(p_rec->pHeader), (p_rec->data));
    }
}
