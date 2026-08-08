#include "XMODEM.h"
#include <stdbool.h>

/* ---------- 宏定义 ---------- */
#define RX_FIFO_SIZE 2048  // 环形 FIFO 大小（至少 2*1029）
#define WORK_BUF_SIZE 2048 // 工作缓冲区大小（略大于 2*最大包）

/* ---------- 静态全局变量 ---------- */
static uint8_t rx_fifo[RX_FIFO_SIZE];
static volatile uint16_t fifo_head = 0;
static volatile uint16_t fifo_tail = 0;

static uint8_t work_buf[WORK_BUF_SIZE];
static uint16_t work_len = 0;
static uint32_t work_last_tick = 0;

/* ---------- 内部函数声明 ---------- */
static uint16_t XMODEM_CRC16(uint8_t *data, uint16_t len);
static bool XMODEM_Send_Byte(XMODEM_Device *dev, uint8_t ch);
static bool XMODEM_FIFO_Get(uint8_t *byte);
static void XMODEM_Send_ACK(XMODEM_Device *dev);
static void XMODEM_Send_NAK(XMODEM_Device *dev);
static void XMODEM_Send_CAN(XMODEM_Device *dev);

/*
 *@brief 计算 CRC16 校验码
 *@param data: 指向数据的指针
 *@param len: 数据长度
 *@return CRC16 校验码
 */
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

/*
 *@brief 发送单字节数据
 *@param dev: XMODEM 设备结构体指针
 *@param ch: 要发送的字节
 *@return true 表示发送成功，false 表示发送失败
 */
static bool XMODEM_Send_Byte(XMODEM_Device *dev, uint8_t ch)
{
    return (HAL_UART_Transmit(dev->huart, &ch, 1, dev->timeout_ms) == HAL_OK);
}

/*
 *@brief 串口中断处理函数
 *@param data: 接收到的字节
 */
void XMODEM_UART_IRQ_Handler(uint8_t data)
{
    // 环形缓冲区计算下一字节位置
    uint16_t next = (fifo_head + 1) % RX_FIFO_SIZE;
    // head=tail 时表示 FIFO 空，head+1=tail 时表示 FIFO 满
    if (next != fifo_tail)
    {
        rx_fifo[fifo_head] = data;
        fifo_head = next; // 更新 head
    }
}

/*
 *@brief 从 FIFO 中取出一个字节
 *@param byte: 指向存放取出字节的指针
 *@return true 表示取出成功，false 表示取出失败
 */
static bool XMODEM_FIFO_Get(uint8_t *byte)
{
    // 如果 FIFO 为空，返回 false
    if (fifo_head == fifo_tail)
        return false;
    *byte = rx_fifo[fifo_tail];                 // 取出 FIFO 中的字节
    fifo_tail = (fifo_tail + 1) % RX_FIFO_SIZE; // 更新 tail
    return true;
}

/* ---------- 控制字符发送 ---------- */
static void XMODEM_Send_ACK(XMODEM_Device *dev) { XMODEM_Send_Byte(dev, 0x06); }
static void XMODEM_Send_NAK(XMODEM_Device *dev) { XMODEM_Send_Byte(dev, 0x15); }
static void XMODEM_Send_CAN(XMODEM_Device *dev)
{
    XMODEM_Send_Byte(dev, 0x18);
    XMODEM_Send_Byte(dev, 0x18);
}

/*
 *@brief 初始化 XMODEM 设备结构体
 *@param dev: 指向 XMODEM 设备结构体的指针
 *@param huart: 串口句柄
 *@param write_cb: 数据写入回调函数
 *@param timeout_ms: 超时时间（毫秒）
 *@param max_retry: 最大重试次数
 */
void XMODEM_Init(XMODEM_Device *dev, UART_HandleTypeDef *huart,
                 XMODEM_WriteCallback write_cb,
                 uint16_t timeout_ms, uint8_t max_retry)
{
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

    // 清空工作区
    work_len = 0;
    work_last_tick = 0;
    // 清空 FIFO（可选）
    fifo_head = 0;
    fifo_tail = 0;
}

/*
 *@brief 启动接收（发送 'C' 握手，开始接收文件）
 *@param dev: 指向 XMODEM 设备结构体的指针
 */
void XMODEM_StartReceive(XMODEM_Device *dev)
{
    dev->receiving = true;     // 标记为正在接收
    dev->packet_num = 1;       // 期望的下一个包序号
    dev->total_bytes = 0;      // 累计接收字节数清零
    dev->eot_received = false; // 未接收 EOT
    dev->can_received = false; // 未接收 CAN
    work_len = 0;
    // 发送 'C' 请求 CRC 模式
    XMODEM_Send_Byte(dev, 0x43);
}

/*
 *
 *@brief 核心轮询函数（主循环调用）
 *@param dev: 指向 XMODEM 设备结构体的指针
 */
void XMODEM_Poll(XMODEM_Device *dev)
{
    if (!dev->receiving) // 如果未处于接收状态，则直接返回
        return;

    uint8_t byte;

    // 1. 将 FIFO 中所有字节取出，追加到工作缓冲区
    while (XMODEM_FIFO_Get(&byte))
    {
        if (work_len < WORK_BUF_SIZE) // 防止溢出
        {
            work_buf[work_len++] = byte;    // 提取FIFO
            work_last_tick = HAL_GetTick(); // 更新最后接收时间
        }
    }

    // 2. 超时检查：如果工作区有数据但长时间未收到新字节，则清空并发送 NAK
    if (work_len > 0 && (HAL_GetTick() - work_last_tick) > dev->timeout_ms)
    {
        work_len = 0;
        XMODEM_Send_NAK(dev);
        return;
    }

    // 3. 扫描工作区，寻找完整的数据包
    uint16_t offset = 0;
    while (offset < work_len) // 0--work_len-1 查找
    {
        uint8_t type = work_buf[offset]; // 当前字节作为帧头
        uint16_t data_len = 0;           // 数据长度（128 或 1024）
        uint16_t pkg_len = 0;            // 包总长度（帧头+序号+反码+数据+CRC）

        // 3.1 识别帧头
        if (type == 0x01)
        { // SOH (128字节数据)
            data_len = 128;
            pkg_len = 1 + 2 + 128 + 2; // 包头+序号+反码+数据+CRC = 133
        }
        else if (type == 0x02)
        { // STX (1024字节数据)
            data_len = 1024;
            pkg_len = 1 + 2 + 1024 + 2; // 1029
        }
        else if (type == 0x04)
        { // EOT
            // 发送 ACK，置结束标志
            XMODEM_Send_ACK(dev);
            dev->eot_received = true; // 标记接收完成
            dev->receiving = false;   // 标记接收结束
            // 移除 EOT 及之前的数据
            // |0--已处理--offset--需要丢弃的数据--work_len-1|
            memmove(work_buf, &work_buf[offset + 1], work_len - offset - 1);
            work_len -= (offset + 1);
            return;
        }
        else if (type == 0x18)
        {                             // CAN
            dev->can_received = true; // 标记接收被取消
            dev->receiving = false;   // 标记接收结束
            memmove(work_buf, &work_buf[offset + 1], work_len - offset - 1);
            work_len -= (offset + 1);
            return;
        }
        else
        {
            // 非有效帧头，跳过当前字节（滑动窗口）
            offset++;
            // 如果跳过太多无效数据，剪切工作区
            if (offset > 512)
            {
                // 剩余字节移到头部
                memmove(work_buf, &work_buf[offset], work_len - offset);
                work_len -= offset;
                offset = 0;
            }
            continue;
        }

        // 3.2 检查是否攒够完整包
        if (work_len < offset + pkg_len)
        {
            // 数据不够，暂时退出，等待更多数据
            // 但若偏移量很大，可剪切前面的无效数据
            if (offset > 512)
            {
                memmove(work_buf, &work_buf[offset], work_len - offset);
                work_len -= offset;
                offset = 0;
            }
            return;
        }

        // 3.3 此时 work_buf[offset] 开始是一个完整包，进行校验
        uint8_t *pkg = &work_buf[offset];

        // 校验序号和反码
        if ((pkg[1] + pkg[2]) != 0xFF)
        {
            // 序号错误，跳过当前帧头，继续找
            offset++;
            continue;
        }

        // 检查序号是否与期望匹配
        if (pkg[1] != dev->packet_num)
        {
            // 可能是旧包重传（发送端未收到ACK）
            // 如果序号是上一个包，可以发ACK并丢弃；否则发NAK。
            if (pkg[1] == dev->packet_num - 1)
            {
                // 重复的旧包，发送ACK让发送端继续
                XMODEM_Send_ACK(dev);
            }
            else
            {
                // 序号完全不对，发NAK要求重传
                XMODEM_Send_NAK(dev);
            }
            // 跳过这个包，继续扫描
            offset++;
            continue;
        }

        // 校验 CRC
        uint16_t recv_crc = (pkg[pkg_len - 2] << 8) | pkg[pkg_len - 1];
        uint16_t calc_crc = XMODEM_CRC16(&pkg[3], data_len);
        if (calc_crc != recv_crc)
        {
            // CRC 错误，发 NAK
            XMODEM_Send_NAK(dev);
            offset++;
            continue;
        }

        // ---------- 校验通过：有效数据包 ----------
        // 调用回调写入数据
        if (dev->write_cb != NULL)
        {
            dev->write_cb(&pkg[3], data_len);
        }
        dev->total_bytes += data_len;
        dev->packet_num++; // 期望下一包序号

        // 发送 ACK
        XMODEM_Send_ACK(dev);

        // 从工作区中移除已处理的数据（包括包前的无效字节）
        uint16_t consumed = offset + pkg_len;
        memmove(work_buf, &work_buf[consumed], work_len - consumed);
        work_len -= consumed;
        offset = 0; // 重置扫描位置
    }

    // 4. 若工作区累积过多无效数据，强行清空
    if (work_len > 1029 * 2)
    {
        work_len = 0;
        XMODEM_Send_NAK(dev);
    }
}