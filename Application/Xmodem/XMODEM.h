#ifndef __XMODEM_H__
#define __XMODEM_H__

#include "main.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define DATA_LEN 1024
#define FRAME_LEN 1029
// 这里使用环形BUFF，多流出一个字节，用于判断是否缓冲区已满
#define DMA_RX_BUF_SIZE (FRAME_LEN * 2 + 1) // 1029 * 2 + 1 = 2059

extern uint8_t dma_rx_buf[DMA_RX_BUF_SIZE];
/*
 * 例如：使用长度8的缓冲区，但只使用前7个字节，
 *  ┌───┬───┬───┬───┬───┬───┬───┬───┐
 *  │ 0 │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │
 *  └───┴───┴───┴───┴───┴───┴───┴───┘
 *    ↑
 *   R/W  (读=写，没有数据可读)
 * R和W均代表即将读/写的位置，当R和W重合时，表示缓冲区为空
 * 当R和W相差1时，表示缓冲区已满
 *  ┌───┬───┬───┬───┬───┬───┬───┬───┐
 *  │ 0 │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │
 *  └───┴───┴───┴───┴───┴───┴───┴───┘
 *    ↑                           ↑
 *    R                           W
 *
 */
extern volatile uint16_t dma_read_idx;
extern volatile uint16_t dma_write_idx;

/* 返回状态枚举（保持不变） */
typedef enum {
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
typedef struct {
  UART_HandleTypeDef *huart;     // 串口句柄
  XMODEM_WriteCallback write_cb; // 数据写入回调

  uint8_t rx_buf[DATA_LEN]; // 工作缓冲区（最大包长 1029）
  uint16_t rx_len;          // 当前工作区有效字节数
  uint32_t last_byte_tick;  // 最后收到字节的时间（ms）
  uint8_t packet_num;       // 期望的下一个包序号（1~255）
  uint32_t total_bytes;     // 累计接收数据字节数
  uint16_t timeout_ms;      // 包间超时时间（ms）
  uint8_t max_retry;        // 最大重试次数
  uint8_t retry_count;

  bool eot_received; // 收到 EOT 标志
  bool can_received; // 收到 CAN 标志
  bool receiving;    // 是否正在接收中
} XMODEM_Device;

/* 外部接口 */
void XMODEM_Init(XMODEM_Device *dev, UART_HandleTypeDef *huart,
                 XMODEM_WriteCallback write_cb, uint16_t timeout_ms,
                 uint8_t max_retry);

/* 供用户在 HAL_UARTEx_RxEventCallback 中调用的处理函数 */
void XMODEM_UART_RxEventCallback(XMODEM_Device *dev, uint16_t size);

/* 主循环轮询函数（核心接收处理） */
void XMODEM_Poll(XMODEM_Device *dev);

/* 启动接收（发送 'C' 握手，开始接收文件） */
void XMODEM_StartReceive(XMODEM_Device *dev);

#endif