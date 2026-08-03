#ifndef __XMODEM_H__
#define __XMODEM_H__

#include "main.h"

/* 返回状态枚举 */
typedef enum
{
    XMODEM_OK = 0x00,     // 接收成功
    XMODEM_ERROR_TIMEOUT, // 超时（无响应或重传超时）
    XMODEM_ERROR_CRC,     // CRC 校验错误
    XMODEM_ERROR_SEQ,     // 包序号错误
    XMODEM_ERROR_CANCEL,  // 收到 CAN（对方取消）
    XMODEM_ERROR_UNKNOWN  // 其他错误
} XMODEM_Status;

/* 数据回调函数类型：当接收到一个有效数据包时调用 */
typedef void (*XMODEM_WriteCallback)(uint8_t *data, uint16_t len, void *user_arg);

/* XMODEM 设备结构体（包含配置、状态和缓冲区） */
typedef struct
{
    UART_HandleTypeDef *huart;     // 绑定的串口句柄
    XMODEM_WriteCallback write_cb; // 数据写入回调
    void *user_arg;                // 回调用户参数

    uint8_t rx_buf[1029]; // 接收缓冲区（最大包长 1029）
    uint8_t packet_num;   // 当前期望的包序号（从1开始）
    uint32_t total_bytes; // 已接收的有效数据总字节数
    uint16_t timeout_ms;  // 接收超时时间（毫秒）
    uint8_t retry_cnt;    // 当前包重试次数
    uint8_t max_retry;    // 最大重试次数
} XMODEM_Device;

/* 外部接口函数 */
void XMODEM_Init(XMODEM_Device *dev, UART_HandleTypeDef *huart,
                 XMODEM_WriteCallback write_cb, void *user_arg,
                 uint16_t timeout_ms, uint8_t max_retry);
XMODEM_Status XMODEM_Receive_File(XMODEM_Device *dev);

#endif
