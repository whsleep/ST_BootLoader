#include "XMODEM.h"
#include <stdbool.h>

/* ---------- 内部函数 ---------- */
static uint16_t XMODEM_CRC16(uint8_t *data, uint16_t len);
static bool XMODEM_Send_Byte(XMODEM_Device *dev, uint8_t ch);
static bool XMODEM_Receive_Byte(XMODEM_Device *dev, uint8_t *ch, uint32_t timeout);
static bool XMODEM_Receive_Packet(XMODEM_Device *dev);
static void XMODEM_Send_ACK(XMODEM_Device *dev);
static void XMODEM_Send_NAK(XMODEM_Device *dev);
static void XMODEM_Send_CAN(XMODEM_Device *dev);

/* ---------- CRC16 计算 ---------- */
static uint16_t XMODEM_CRC16(uint8_t *data, uint16_t len)
{
    uint16_t crc = 0x0000;
    while (len--)
    {
        crc ^= (*data++) << 8;
        for (uint8_t i = 0; i < 8; i++)
        {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/* ---------- 串口收发（带超时） ---------- */
static bool XMODEM_Send_Byte(XMODEM_Device *dev, uint8_t ch)
{
    return (HAL_UART_Transmit(dev->huart, &ch, 1, dev->timeout_ms) == HAL_OK);
}

static bool XMODEM_Receive_Byte(XMODEM_Device *dev, uint8_t *ch, uint32_t timeout)
{
    return (HAL_UART_Receive(dev->huart, ch, 1, timeout) == HAL_OK);
}

/* ---------- 接收一个完整数据包 ---------- */
static bool XMODEM_Receive_Packet(XMODEM_Device *dev)
{
    uint8_t *buf = dev->rx_buf;
    uint16_t data_len;

    // 1. 接收起始字节
    if (!XMODEM_Receive_Byte(dev, &buf[0], dev->timeout_ms))
        return false;

    // 判断包类型
    if (buf[0] == 0x02)
    { // STX -> 1K
        data_len = 1024;
    }
    else if (buf[0] == 0x01)
    { // SOH -> 128
        data_len = 128;
    }
    else if (buf[0] == 0x04)
    {                // EOT
        return true; // 表示传输结束
    }
    else if (buf[0] == 0x18)
    {                // CAN
        return true; // 取消
    }
    else
    {
        return false;
    }

    // 2. 接收序号和反码
    if (!XMODEM_Receive_Byte(dev, &buf[1], dev->timeout_ms))
        return false;
    if (!XMODEM_Receive_Byte(dev, &buf[2], dev->timeout_ms))
        return false;

    // 验证序号和反码
    if ((buf[1] + buf[2]) != 0xFF)
        return false;

    // 3. 接收数据区（可能分多次接收）
    uint16_t remaining = data_len;
    uint16_t offset = 3;
    while (remaining > 0)
    {
        uint8_t tmp[64];
        uint16_t chunk = (remaining > sizeof(tmp)) ? sizeof(tmp) : remaining;
        if (HAL_UART_Receive(dev->huart, tmp, chunk, dev->timeout_ms) != HAL_OK)
            return false;
        memcpy(&buf[offset], tmp, chunk);
        offset += chunk;
        remaining -= chunk;
    }

    // 4. 接收 CRC（高字节在前）
    uint8_t crc_h, crc_l;
    if (!XMODEM_Receive_Byte(dev, &crc_h, dev->timeout_ms))
        return false;
    if (!XMODEM_Receive_Byte(dev, &crc_l, dev->timeout_ms))
        return false;
    uint16_t recv_crc = (crc_h << 8) | crc_l;

    // 5. 计算并比较 CRC
    uint16_t calc_crc = XMODEM_CRC16(&buf[3], data_len);
    if (calc_crc != recv_crc)
        return false;

    // 6. 检查序号是否与期望匹配
    if (buf[1] != dev->packet_num)
        return false;

    // 将数据长度信息存到缓冲区头部，供外部使用（0 表示 128，1 表示 1024）
    buf[0] = (data_len == 1024) ? 0x02 : 0x01;
    return true;
}

/* ---------- 发送控制字符 ---------- */
static void XMODEM_Send_ACK(XMODEM_Device *dev) { XMODEM_Send_Byte(dev, 0x06); }
static void XMODEM_Send_NAK(XMODEM_Device *dev) { XMODEM_Send_Byte(dev, 0x15); }
static void XMODEM_Send_CAN(XMODEM_Device *dev)
{
    XMODEM_Send_Byte(dev, 0x18);
    XMODEM_Send_Byte(dev, 0x18);
}

/* ---------- 公共 API ---------- */
void XMODEM_Init(XMODEM_Device *dev, UART_HandleTypeDef *huart,
                 XMODEM_WriteCallback write_cb, void *user_arg,
                 uint16_t timeout_ms, uint8_t max_retry)
{
    dev->huart = huart;
    dev->write_cb = write_cb;
    dev->user_arg = user_arg;
    dev->timeout_ms = timeout_ms;
    dev->max_retry = max_retry;
    dev->packet_num = 1;
    dev->total_bytes = 0;
}

XMODEM_Status XMODEM_Receive_File(XMODEM_Device *dev)
{
    // 1. 握手：发送 'C'，等待第一个包
    bool first_packet_received = false;
    uint8_t handshake_retry = 0;
    const uint8_t HANDSHAKE_MAX = 20;

    while (!first_packet_received && handshake_retry < HANDSHAKE_MAX)
    {
        // 发送 'C' 请求 CRC 模式
        if (!XMODEM_Send_Byte(dev, 0x43))
        {
            return XMODEM_ERROR_TIMEOUT;
        }

        // 尝试接收一个完整包
        if (XMODEM_Receive_Packet(dev))
        {
            uint8_t type = dev->rx_buf[0];
            if (type == 0x02 || type == 0x01)
            {
                first_packet_received = true;
                break;
            }
            else if (type == 0x18)
            { // CAN
                return XMODEM_ERROR_CANCEL;
            }
            else
            {
                // 收到非预期字符，继续
            }
        }
        handshake_retry++;
        HAL_Delay(500);
    }

    if (!first_packet_received)
        return XMODEM_ERROR_TIMEOUT;

    // 2. 主接收循环
    while (1)
    {
        bool packet_ok = false;
        uint8_t retry_count = 0;

        while (!packet_ok && retry_count < dev->max_retry)
        {
            if (XMODEM_Receive_Packet(dev))
            {
                uint8_t type = dev->rx_buf[0];

                if (type == 0x02 || type == 0x01)
                {
                    // 有效数据包
                    uint16_t data_len = (type == 0x02) ? 1024 : 128;
                    uint8_t *data_ptr = &dev->rx_buf[3]; // 数据起始

                    // 调用回调，写入数据（如 Flash）
                    if (dev->write_cb != NULL)
                    {
                        dev->write_cb(data_ptr, data_len, dev->user_arg);
                    }
                    dev->total_bytes += data_len;
                    dev->packet_num++; // 下一个包序号
                    XMODEM_Send_ACK(dev);
                    packet_ok = true;
                }
                else if (type == 0x04)
                { // EOT
                    XMODEM_Send_ACK(dev);
                    return XMODEM_OK;
                }
                else if (type == 0x18)
                { // CAN
                    return XMODEM_ERROR_CANCEL;
                }
                else
                {
                    // 未知类型，NAK 重试
                    XMODEM_Send_NAK(dev);
                    retry_count++;
                }
            }
            else
            {
                // 接收失败（超时或格式错误），发送 NAK
                XMODEM_Send_NAK(dev);
                retry_count++;
            }
        }

        // 重试次数耗尽
        if (!packet_ok)
        {
            XMODEM_Send_CAN(dev);
            return XMODEM_ERROR_TIMEOUT;
        }
    }
}
