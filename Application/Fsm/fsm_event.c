#include "fsm_event.h"

extern volatile uint16_t Modbus_read_regs[];
extern volatile uint16_t Modbus_write_regs[];
extern uint16_t Modbus_holding_regs[];
extern Modbus_CommContext mb_comm;
extern MODBUS_Device Modbus_dev0;
extern uint8_t rxdata;
extern XMODEM_Device xmodem;

/* 从 Modbus 寄存器提取命令数据 */
#define MODBUS_06_REC Modbus_write_regs[0]
#define MODBUS_03_REC Modbus_read_regs[0]
#define MODBUS_REC Modbus_holding_regs[0]

/* ---------------- 状态 STATE_MODBUS_RECV ---------------- */

/**
 * @brief 进入等待命令状态：启动超时定时器
 */
void ModbusRecv_Entry(void)
{
    ULOG_INFO("Entering STATE_MODBUS_RECV");
    StartTimeout();
}

/**
 * @brief 等待命令状态的核心执行：检查命令或超时
 * @return 产生的事件，若无事件则返回 EVENT_NONE
 */
BootEvent ModbusRecv_Do(void)
{
    // 检测是否收到合法的升级触发命令
    if (MODBUS_REC == 0x5B5B && MODBUS_06_REC > 0 && MODBUS_03_REC > 0)
    {
        ULOG_INFO("Received legal 0x06 command, triggering upgrade");
        StopTimeout(); // 收到命令，停止超时
        return EVENT_RECV_LEGAL_06H;
    }
    if (mb_comm.rx_len > 0)
    {
        uint16_t len = mb_comm.rx_len;
        mb_comm.rx_len = 0; // 立即释放标志位，允许中断记录新帧
        MODBUS_Status sta = MODBUS_Process_Frame(&Modbus_dev0, mb_comm.rx_buffer, &len, mb_comm.tx_buffer, &mb_comm.tx_len);
        if (sta == MB_OK)
        {
            HAL_UART_Transmit(&huart1, mb_comm.tx_buffer, mb_comm.tx_len, 0xffff);
        }
        HAL_UARTEx_ReceiveToIdle_IT(&huart1, mb_comm.rx_buffer, MODBUS_RX_BUFFER_SIZE);
    }
    // 超时检测
    if (IsTimeout())
    {
        ULOG_INFO("Timeout in STATE_MODBUS_RECV");
        StopTimeout();
        if (CheckAppValid())
            return EVENT_TIMEOUT_APP_VALID;
        else
            return EVENT_TIMEOUT_APP_INVALID;
    }

    return EVENT_NONE; // 无事件，保持状态
}

void ModbusRecv_Exit(void)
{
    ULOG_INFO("Exiting STATE_MODBUS_RECV");
    HAL_UART_AbortReceive(&huart1);           // 终止当前接收（包括禁用IDLE中断）
    HAL_UART_Receive_IT(&huart1, &rxdata, 1); // 开启单字节接收
}

/* ---------------- 状态 STATE_PROG_UPGRADE ---------------- */

/**
 * @brief 进入升级状态：启动超时定时器（烧写超时保护）
 */
void ProgUpgrade_Entry(void)
{
    ULOG_INFO("Entering STATE_PROG_UPGRADE");
    // 擦除 APP 区（扇区 2~5）
    if (Bootloader_Erase() != BL_OK)
    {
        // 擦除失败，上报错误，可能死循环或等待复位
        Error_Handler();
    }
    Bootloader_FlashBegin(); // 开始 Flash 写入
    XMODEM_StartReceive(&xmodem);
    StartTimeout();
}

/**
 * @brief 升级状态的核心执行：模拟烧写进度或检测超时
 * @return 产生的事件
 * @note 实际项目中应检查 Flash_GetStatus() 等硬件状态
 */
BootEvent ProgUpgrade_Do(void)
{

    XMODEM_Poll(&xmodem);
    // 判断是否结束接收
    if (xmodem.eot_received == true)
    {
        Bootloader_FlashEnd(); // 结束 Flash 写入，处理剩余缓存
        if (CheckAppValid())   // 检查 APP 魔数是否有效
        {
            ULOG_INFO("APP is valid");
            return EVENT_BURN_COMPLETE_APP_VALID;
        }
        else
        {
            ULOG_INFO("APP is invalid");
            return EVENT_BURN_COMPLETE_APP_INVALID;
        }
    }
    return EVENT_NONE;
}

void ProgUpgrade_Exit(void)
{
    ULOG_INFO("Exiting STATE_PROG_UPGRADE");
    // 退出升级状态时的清理
}

/* ---------------- 状态 STATE_JUMP_APP ---------------- */

/**
 * @brief 进入跳转状态：执行跳转（通常不返回）
 */
void JumpApp_Entry(void)
{
    ULOG_INFO("Entering STATE_JUMP_APP");
    // 实际应调用 JumpToApp();  // 该函数不会返回
}

/**
 * @brief 跳转状态的核心执行（此状态为终态，一般无操作）
 */
BootEvent JumpApp_Do(void)
{
    BootJumpAPP();
    if (IsTimeout())
    {
        ULOG_INFO("Timeout in STATE_JUMP_APP");
        return EVENT_NONE;
    }
    return EVENT_NONE;
}

void JumpApp_Exit(void)
{
    // 无操作
}
