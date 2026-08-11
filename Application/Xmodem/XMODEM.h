#ifndef __XMODEM_H__
#define __XMODEM_H__

#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* 返回状态枚举（保持不变） */
typedef enum
{
    XMODEM_OK = 0x00,
    XMODEM_ERROR_TIMEOUT,
    XMODEM_ERROR_CRC,
    XMODEM_ERROR_SEQ,
    XMODEM_ERROR_CANCEL,
    XMODEM_ERROR_UNKNOWN
} XMODEM_Status;

/* 数据写入回调类型 */
typedef void (*XMODEM_WriteCallback)(uint8_t *data, uint16_t len);

/* XMODEM 设备结构体（扩展） */
typedef struct
{
    UART_HandleTypeDef *huart;     // 串口句柄
    XMODEM_WriteCallback write_cb; // 数据写入回调

    uint8_t rx_buf[1029];    // 工作缓冲区（最大包长 1029）
    uint16_t rx_len;         // 当前工作区有效字节数
    uint32_t last_byte_tick; // 最后收到字节的时间（ms）
    uint8_t packet_num;      // 期望的下一个包序号（1~255）
    uint32_t total_bytes;    // 累计接收数据字节数
    uint16_t timeout_ms;     // 包间超时时间（ms）
    uint8_t max_retry;       // 最大重试次数

    bool eot_received; // 收到 EOT 标志
    bool can_received; // 收到 CAN 标志
    bool receiving;    // 是否正在接收中
} XMODEM_Device;

/* 外部接口 */
void XMODEM_Init(XMODEM_Device *dev, UART_HandleTypeDef *huart,
                 XMODEM_WriteCallback write_cb,
                 uint16_t timeout_ms, uint8_t max_retry);

/* 中断服务调用的入队函数（在串口中断中调用） */
void XMODEM_UART_IRQ_Handler(uint8_t data);

/* 主循环轮询函数（核心接收处理） */
void XMODEM_Poll(XMODEM_Device *dev);

/* 启动接收（发送 'C' 握手，开始接收文件） */
void XMODEM_StartReceive(XMODEM_Device *dev);

#endif