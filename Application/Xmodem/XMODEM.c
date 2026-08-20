#include "XMODEM.h"

/* ---------- �궨�� ---------- */
#define RX_FIFO_SIZE 4096   
#define DMA_RX_BUF_SIZE 2058 
#define WORK_BUF_SIZE 2058   

/* ---------- ��̬ȫ�ֱ��� ---------- */
static uint8_t rx_fifo[RX_FIFO_SIZE];
static volatile uint16_t fifo_head = 0;
static volatile uint16_t fifo_tail = 0;

static uint8_t dma_rx_buf[DMA_RX_BUF_SIZE];     // DMA ���ջ������

static uint8_t work_buf[WORK_BUF_SIZE];
static uint16_t work_len = 0;
static uint32_t work_last_tick = 0;

/* ---------- �ڲ��������� ---------- */
static uint16_t XMODEM_CRC16(uint8_t *data, uint16_t len);
static bool XMODEM_Send_Byte(XMODEM_Device *dev, uint8_t ch);
static bool XMODEM_FIFO_Get(uint8_t *byte);
static void XMODEM_Send_ACK(XMODEM_Device *dev);
static void XMODEM_Send_NAK(XMODEM_Device *dev);
static void XMODEM_Send_CAN(XMODEM_Device *dev);
static void XMODEM_UART_DMA_Handler(uint8_t *data, uint16_t len);

/*
 *@brief ���� CRC16 У����
 *@param data: ָ�����ݵ�ָ��
 *@param len: ���ݳ���
 *@return CRC16 У����
 */
static uint16_t XMODEM_CRC16(uint8_t *data, uint16_t len) {
  uint16_t crc = 0x0000;
  while (len--) {
    crc ^= (*data++) << 8;
    for (uint8_t i = 0; i < 8; i++) {
      if (crc & 0x8000)
        crc = (crc << 1) ^ 0x1021;
      else
        crc <<= 1;
    }
  }
  return crc;
}

/*
 *@brief ���͵��ֽ�����
 *@param dev: XMODEM �豸�ṹ��ָ��
 *@param ch: Ҫ���͵��ֽ�
 *@return true ��ʾ���ͳɹ���false ��ʾ����ʧ��
 */
static bool XMODEM_Send_Byte(XMODEM_Device *dev, uint8_t ch) {
  return (HAL_UART_Transmit(dev->huart, &ch, 1, dev->timeout_ms) == HAL_OK);
}

/*
 *@brief ����������д������ FIFO���� DMA �ص����ã�
 *@param data: ����ָ��
 *@param len: ���ݳ���
 */
static void XMODEM_UART_DMA_Handler(uint8_t *data, uint16_t len) {
  for (uint16_t i = 0; i < len; i++) {
    uint16_t next = (fifo_head + 1) % RX_FIFO_SIZE;
    if (next != fifo_tail) // FIFO δ��
    {
      rx_fifo[fifo_head] = data[i];
      fifo_head = next;
    } else {
      break;
    }
  }
}

/*
 *@brief ���ݾɽӿڣ����ֽ�д�� FIFO���Ѳ��Ƽ�ʹ�ã�
 *@param data: �����ֽ�
 */
void XMODEM_UART_IRQ_Handler(uint8_t data) {
  XMODEM_UART_DMA_Handler(&data, 1);
}

/*
 *@brief �� FIFO ��ȡ��һ���ֽ�
 *@param byte: ָ����ȡ���ֽڵ�ָ��
 *@return true ��ʾȡ���ɹ���false ��ʾȡ��ʧ��
 */
static bool XMODEM_FIFO_Get(uint8_t *byte) {
  if (fifo_head == fifo_tail) // FIFO ��
    return false;

  *byte = rx_fifo[fifo_tail];
  fifo_tail = (fifo_tail + 1) % RX_FIFO_SIZE;
  return true;
}

/* ---------- �����ַ����� ---------- */
static void XMODEM_Send_ACK(XMODEM_Device *dev) { XMODEM_Send_Byte(dev, 0x06); }
static void XMODEM_Send_NAK(XMODEM_Device *dev) { XMODEM_Send_Byte(dev, 0x15); }
static void XMODEM_Send_CAN(XMODEM_Device *dev) {
  XMODEM_Send_Byte(dev, 0x18);
  XMODEM_Send_Byte(dev, 0x18);
}

/*
 *@brief ��ʼ�� XMODEM �豸�ṹ��
 *@param dev: ָ�� XMODEM �豸�ṹ���ָ��
 *@param huart: ���ھ��
 *@param write_cb: ����д��ص�����
 *@param timeout_ms: ��ʱʱ�䣨���룩
 *@param max_retry: ������Դ���
 */
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

  HAL_UART_AbortReceive(dev->huart);

  // ��չ������� FIFO
  work_len = 0;
  work_last_tick = 0;
  fifo_head = 0;
  fifo_tail = 0;
}

/*
 *@brief �������գ����� 'C' ���֣���ʼ�����ļ���
 *@param dev: ָ�� XMODEM �豸�ṹ���ָ��
 */
void XMODEM_StartReceive(XMODEM_Device *dev) {
  dev->receiving = true;
  dev->packet_num = 1;
  dev->total_bytes = 0;
  dev->eot_received = false;
  dev->can_received = false;
  work_len = 0;

  // ��� FIFO����ֹ�������ݸ���
  fifo_head = 0;
  fifo_tail = 0;

  // ֹͣ�������ڽ��еĽ��գ�Ȼ���������� DMA ����
  if (HAL_UARTEx_ReceiveToIdle_DMA(dev->huart, dma_rx_buf, DMA_RX_BUF_SIZE) !=
      HAL_OK) {
    dev->receiving = false;
    return;
  }

  // ���� 'C' ���� CRC ģʽ
  XMODEM_Send_Byte(dev, 0x43);
}

/*
 *@brief ������ѯ��������ѭ�����ã�
 *@param dev: ָ�� XMODEM �豸�ṹ���ָ��
 */
void XMODEM_Poll(XMODEM_Device *dev) {
  if (!dev->receiving)
    return;

  uint8_t byte;

  // 1. �� FIFO �������ֽ�ȡ����׷�ӵ�����������
	while (XMODEM_FIFO_Get(&byte)) {
			if (work_len < WORK_BUF_SIZE) {
					work_buf[work_len++] = byte;
					work_last_tick = HAL_GetTick();
			}else{
				  // ����������ǿ����ղ� NAK����������
					work_len = 0;
					XMODEM_Send_NAK(dev);
					break;
			}
	}

  // 2. ��ʱ��飺�����������ݵ���ʱ��δ�յ����ֽڣ�����ղ����� NAK
  if ((HAL_GetTick() - work_last_tick) > dev->timeout_ms) {
    work_len = 0;
    XMODEM_Send_NAK(dev);
		work_last_tick = HAL_GetTick();
    return;
  }

  // 3. ɨ�蹤������Ѱ�����������ݰ�
  uint16_t offset = 0;
  while (offset < work_len) {
    uint8_t type = work_buf[offset];
    uint16_t data_len = 0;
    uint16_t pkg_len = 0;

    // 3.1 ʶ��֡ͷ
    if (type == 0x01) // SOH
    {
      data_len = 128;
      pkg_len = 1 + 2 + 128 + 2; // 133
    } else if (type == 0x02)     // STX
    {
      data_len = 1024;
      pkg_len = 1 + 2 + 1024 + 2; // 1029
    } else if (type == 0x04)      // EOT
    {
      XMODEM_Send_ACK(dev);
      dev->eot_received = true;
      dev->receiving = false;
      __disable_irq();
      HAL_UART_AbortReceive(dev->huart); // ֹͣ DMA ����
      __enable_irq();
      memmove(work_buf, &work_buf[offset + 1], work_len - offset - 1);
      work_len -= (offset + 1);
      return;
    } else if (type == 0x18) // CAN
    {
      dev->can_received = true;
      dev->receiving = false;
      __disable_irq();
      HAL_UART_AbortReceive(dev->huart); // ֹͣ DMA ����
      __enable_irq();
      memmove(work_buf, &work_buf[offset + 1], work_len - offset - 1);
      work_len -= (offset + 1);
      return;
    } else {
      // ����Ч֡ͷ��������ǰ�ֽ�
      offset++;
      if (offset > 512) {
        memmove(work_buf, &work_buf[offset], work_len - offset);
        work_len -= offset;
        offset = 0;
      }
      continue;
    }

    // 3.2 ���ݲ��㣬�˳��ȴ�
    if (work_len < offset + pkg_len) {
      if (offset > 512) {
        memmove(work_buf, &work_buf[offset], work_len - offset);
        work_len -= offset;
        offset = 0;
      }
      return;
    }

    // 3.3 ������У��
    uint8_t *pkg = &work_buf[offset];

    // У����źͷ���
    if ((pkg[1] + pkg[2]) != 0xFF) {
      offset++;
      continue;
    }

    // �������Ƿ�������ƥ��
    if (pkg[1] != dev->packet_num) {
      if (pkg[1] == dev->packet_num - 1) {
        XMODEM_Send_ACK(dev); // �ظ��ɰ����� ACK
      } else {
        XMODEM_Send_NAK(dev); // ��Ŵ��󣬷� NAK
      }
      offset += pkg_len;   // ����������
      continue;
    }

    // У�� CRC
    uint16_t recv_crc = (pkg[pkg_len - 2] << 8) | pkg[pkg_len - 1];
    uint16_t calc_crc = XMODEM_CRC16(&pkg[3], data_len);
    if (calc_crc != recv_crc) {
      XMODEM_Send_NAK(dev);
      offset += pkg_len;   // ����������
      continue;
    }

    // ---------- У��ͨ�� ----------
    if (dev->write_cb != NULL) {
      dev->write_cb(&pkg[3], data_len);
    }
    dev->total_bytes += data_len;
    dev->packet_num++;

    XMODEM_Send_ACK(dev);

    // �Ƴ��Ѵ��������ݣ�������ǰ����Ч�ֽڣ�
    uint16_t consumed = offset + pkg_len;
    memmove(work_buf, &work_buf[consumed], work_len - consumed);
    work_len -= consumed;
    offset = 0; // ����ɨ��λ��
  }

  // 4. ���������ۻ�������Ч���ݣ�ǿ�����
  if (work_len >= WORK_BUF_SIZE) {
    work_len = 0;
    XMODEM_Send_NAK(dev);
  }
}

/*
 *@brief ���û��� HAL_UARTEx_RxEventCallback �е��õĴ�������
 *@param huart: �����ص��Ĵ��ھ��
 *@param size: ���ν��յ����ֽ���
 */
void XMODEM_UART_RxEventCallback(XMODEM_Device *dev, uint16_t size) {

  // �������յ�����������д������ FIFO
  XMODEM_UART_DMA_Handler(dma_rx_buf, size);

  // �������� DMA ���գ�׼����һ������
  if (dev->receiving) {
    HAL_UARTEx_ReceiveToIdle_DMA(dev->huart, dma_rx_buf, DMA_RX_BUF_SIZE);
  }
}
