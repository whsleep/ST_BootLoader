#include "fsm_event.h"

extern volatile uint16_t Modbus_read_regs[];
extern volatile uint16_t Modbus_write_regs[];
extern uint16_t Modbus_holding_regs[];

/* 从 Modbus 寄存器提取命令数据 */
#define MODBUS_06_REC Modbus_write_regs[0]
#define MODBUS_03_REC Modbus_read_regs[0]
#define MODBUS_REC Modbus_holding_regs[0]

/**
 * @brief 检查当前 Flash 中的 APP 是否有效
 * @return true 有效，false 无效
 * @note 用户需根据实际硬件实现（CRC、签名等）
 */
bool CheckAppValid(void)
{
    return true;
}

/* ---------------- 状态 STATE_MODBUS_RECV ---------------- */

/**
 * @brief 进入等待命令状态：启动超时定时器
 */
void ModbusRecv_Entry(void)
{
#if DEBUG
    printf("[FSM] Enter STATE_MODBUS_RECV, tick=%lu\n", (unsigned long)s_tick);
#endif
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
#if DEBUG
        printf("[FSM] Received legal 0x06 command, trigger upgrade\n");
#endif
        StopTimeout(); // 收到命令，停止超时
        return EVENT_RECV_LEGAL_06H;
    }

    // 超时检测
    if (IsTimeout())
    {
#if DEBUG
        printf("[FSM] Timeout in STATE_MODBUS_RECV\n");
#endif
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
    // 退出等待状态时的清理（本例无特殊需求）
}

/* ---------------- 状态 STATE_PROG_UPGRADE ---------------- */

/**
 * @brief 进入升级状态：启动超时定时器（烧写超时保护）
 */
void ProgUpgrade_Entry(void)
{
#if DEBUG
    printf("[FSM] Enter STATE_PROG_UPGRADE, tick=%lu\n", (unsigned long)s_tick);
#endif
    StartTimeout();
}

/**
 * @brief 升级状态的核心执行：模拟烧写进度或检测超时
 * @return 产生的事件
 * @note 实际项目中应检查 Flash_GetStatus() 等硬件状态
 */
BootEvent ProgUpgrade_Do(void)
{
    // 本示例仅模拟超时作为烧写完成条件
    if (IsTimeout())
    {
#if DEBUG
        printf("[FSM] Timeout in STATE_PROG_UPGRADE (simulate burn complete)\n");
#endif
        StopTimeout();
        // 假设烧写完成，根据新 APP 有效性决定事件
        // 实际应使用 CheckNewAppValid()，此处暂用 CheckAppValid() 模拟
        if (CheckAppValid())
            return EVENT_BURN_COMPLETE_APP_VALID;
        else
            return EVENT_BURN_COMPLETE_APP_INVALID;
    }

    return EVENT_NONE;
}

void ProgUpgrade_Exit(void)
{
    // 退出升级状态时的清理
}

/* ---------------- 状态 STATE_JUMP_APP ---------------- */

/**
 * @brief 进入跳转状态：执行跳转（通常不返回）
 */
void JumpApp_Entry(void)
{
#if DEBUG
    printf("[FSM] Enter STATE_JUMP_APP, tick=%lu\n", (unsigned long)s_tick);
#endif
    // 实际应调用 JumpToApp();  // 该函数不会返回
}

/**
 * @brief 跳转状态的核心执行（此状态为终态，一般无操作）
 */
BootEvent JumpApp_Do(void)
{
    // 本示例仅模拟超时作为烧写完成条件
    if (IsTimeout())
    {
#if DEBUG
        printf("[FSM] Timeout in STATE_JUMP_APP (simulate jump complete)\n");
#endif
        return EVENT_NONE;
    }
    // 无事件，保持状态（若 JumpToApp 已执行则不会运行到这里）
    return EVENT_NONE;
}

void JumpApp_Exit(void)
{
    // 无操作
}
