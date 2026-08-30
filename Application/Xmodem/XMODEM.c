#include "XMODEM.h"

/* ---------- 变量定义 ---------- */
uint8_t dma_rx_buf[DMA_RX_BUF_SIZE];
volatile uint16_t dma_read_idx = 0;  // 主循环已处理位置
volatile uint16_t dma_write_idx = 0; // DMA 当前写入位置

static uint16_t XMODEM_CRC16_Ring(uint16_t offset, uint16_t len) {
  uint16_t crc = 0x0000;        // XMODEM 标准初值
  const uint16_t poly = 0x1021; // CCITT 多项式

  for (uint16_t i = 0; i < len; i++) {
    uint8_t byte = dma_rx_buf[(dma_read_idx + offset + i) % DMA_RX_BUF_SIZE];
    crc ^= (uint16_t)byte << 8; // 字节异或到高字节
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ poly;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

static bool XMODEM_Send_Byte(XMODEM_Device *dev, uint8_t ch) {
  HAL_UART_Transmit_IT(dev->huart, &ch, 1);
  return true;
}

static void XMODEM_Send_ACK(XMODEM_Device *dev) { XMODEM_Send_Byte(dev, 0x06); }
static void XMODEM_Send_NAK(XMODEM_Device *dev) { XMODEM_Send_Byte(dev, 0x15); }
static void XMODEM_Send_CAN(XMODEM_Device *dev) {
  XMODEM_Send_Byte(dev, 0x18);
  XMODEM_Send_Byte(dev, 0x18);
}

static void XMODEM_DMA_AddIDX(uint16_t inc) {
  dma_read_idx = (dma_read_idx + inc) % DMA_RX_BUF_SIZE;
}

static uint16_t XMODEM_Return_ReadIDX(uint16_t inc) {
  return (dma_read_idx + inc) % DMA_RX_BUF_SIZE;
}

static uint16_t XMODEM_Get_Valid_Length() {
  uint16_t size =
      (dma_write_idx - dma_read_idx + DMA_RX_BUF_SIZE) % DMA_RX_BUF_SIZE;
  return size;
}

void XMODEM_Init(XMODEM_Device *dev, UART_HandleTypeDef *huart,
                 XMODEM_WriteCallback write_cb, uint16_t timeout_ms,
                 uint8_t max_retry) {
  dev->huart = huart;
  dev->write_cb = write_cb;
  dev->timeout_ms = timeout_ms;
  dev->max_retry = max_retry;
  dev->packet_num = 1;
  dev->total_bytes = 0;
  dev->eot_received = false;
  dev->can_received = false;
  dev->receiving = false;
  dev->rx_len = 0;
  dev->last_byte_tick = 0;
}

void XMODEM_StartReceive(XMODEM_Device *dev) {
  dev->receiving = true;
  dev->packet_num = 1;
  dev->total_bytes = 0;
  dev->eot_received = false;
  dev->can_received = false;
  HAL_UART_Abort(dev->huart);

  __HAL_UART_CLEAR_IDLEFLAG(dev->huart);
  __HAL_UART_CLEAR_OREFLAG(dev->huart); // 清除溢出标志
  while (HAL_UARTEx_ReceiveToIdle_DMA(dev->huart, dma_rx_buf,
                                      DMA_RX_BUF_SIZE - 1) != HAL_OK) {
  }

  XMODEM_Send_Byte(dev, 0x43);
}

void XMODEM_Poll(XMODEM_Device *dev) {
  if (!dev->receiving)
    return;

  while (XMODEM_Get_Valid_Length() > 0) {
    uint8_t head = dma_rx_buf[dma_read_idx]; // 读取头字符
    // 有限处理特殊字符
    if (head == 0x04) { // EOT
      XMODEM_DMA_AddIDX(1);
      XMODEM_Send_ACK(dev);
      dev->eot_received = true;
      dev->receiving = false;
      return;
    }
    if (head == 0x18) { // CAN
      XMODEM_DMA_AddIDX(1);
      // 若连续两个 CAN，可视为取消，这里简单处理
      dev->can_received = true;
      dev->receiving = false;
      return;
    }
    uint16_t data_len;
    uint16_t frame_len;
    // 处理头字符
    if (head == 0x01) {
      data_len = 128;
      frame_len = 128 + 5;
    } else if (head == 0x02) { // STX
      data_len = DATA_LEN;
      frame_len = FRAME_LEN;
    } else {
      XMODEM_DMA_AddIDX(1);
      continue;
    }
    // 数据帧不完整，等待下一轮
    if (XMODEM_Get_Valid_Length() < frame_len) {
      // 检查超时
      if (HAL_GetTick() - dev->last_byte_tick >= dev->timeout_ms) {
        if (dev->retry_count < dev->max_retry) {
          XMODEM_Send_NAK(dev);
          dev->retry_count++;
          dev->last_byte_tick = HAL_GetTick();
          dma_read_idx = dma_write_idx;
        } else {
        }
      }
      return;
    }
    // 读取包序号及反码
    uint8_t block_num = dma_rx_buf[XMODEM_Return_ReadIDX(1)];
    uint8_t inverse_num = dma_rx_buf[XMODEM_Return_ReadIDX(2)];
    // 序号反码校验
    if ((uint8_t)(block_num + inverse_num) != 0xFF) {
      // 帧头可能误判，滑动一个字节继续找下一个 SOH/STX
      XMODEM_DMA_AddIDX(1);
      continue;
    }
    // 序号处理
    if (block_num != dev->packet_num) {
      if (block_num == (uint8_t)(dev->packet_num - 1)) {
        XMODEM_Send_ACK(dev);
      } else {
        if (dev->retry_count < dev->max_retry) {
          XMODEM_Send_NAK(dev);
          dev->retry_count++;
        } else {
          return;
        }
      }
      // 丢弃整个帧（无论重复还是错误）
      XMODEM_DMA_AddIDX(frame_len);
      continue;
    }
    // CRC 校验
    uint16_t cal_crc = XMODEM_CRC16_Ring(3, data_len);
    uint8_t crc_hi = dma_rx_buf[XMODEM_Return_ReadIDX(3 + data_len)];
    uint8_t crc_lo = dma_rx_buf[XMODEM_Return_ReadIDX(3 + data_len + 1)];
    uint16_t recv_crc = ((uint16_t)crc_hi << 8) | crc_lo;

    if (cal_crc != recv_crc) {
      // CRC 错误，丢弃整个帧，请求重发
      XMODEM_DMA_AddIDX(frame_len);
      if (dev->retry_count < dev->max_retry) {
        XMODEM_Send_NAK(dev);
        dev->retry_count++;
      } else {
        return;
      }
      continue;
    }
    // 将数据拷贝到连续缓冲区 dev->rx_buf（避免环形越界）
    for (uint16_t i = 0; i < data_len; i++) {
      dev->rx_buf[i] = dma_rx_buf[XMODEM_Return_ReadIDX(3 + i)];
    }
    // 帧正确：写入数据
    if (dev->write_cb != NULL) {
      dev->write_cb(dev->rx_buf, data_len);
    }
    dev->total_bytes += data_len;
    dev->packet_num++;
    // 发送 ACK
    XMODEM_Send_ACK(dev);
    // 成功接收一帧，清零重试计数
    dev->retry_count = 0;
    // 丢弃已处理的帧
    XMODEM_DMA_AddIDX(frame_len);
  }
  // 无数据：检查超时
  if (XMODEM_Get_Valid_Length() == 0) {
    if (HAL_GetTick() - dev->last_byte_tick >= dev->timeout_ms) {
      if (dev->retry_count < dev->max_retry) {
        XMODEM_Send_NAK(dev);
        dev->retry_count++;
        dev->last_byte_tick = HAL_GetTick();
      } else {
      }
    }
    return;
  }
}

void XMODEM_UART_RxEventCallback(XMODEM_Device *dev, uint16_t size) {
  dma_write_idx = size % DMA_RX_BUF_SIZE;
  dev->last_byte_tick = HAL_GetTick();
}