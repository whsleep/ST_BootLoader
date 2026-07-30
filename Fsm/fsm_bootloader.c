#include "fsm_bootloader.h"
#include <stddef.h>

/* 外部 Modbus 寄存器*/
extern volatile uint16_t Modbus_read_regs[];
extern volatile uint16_t Modbus_write_regs[];
extern uint16_t Modbus_holding_regs[];

#define TICK_PERIOD_MS 10                             // 每次Fsm_Run调用的间隔（毫秒）
#define TIMEOUT_MS 5000                               // 超时时间（毫秒）
#define TIMEOUT_PERIODS (TIMEOUT_MS / TICK_PERIOD_MS) // 500个周期超时

/* 从 Modbus 寄存器中提取特定寄存器（此处将写寄存器[0]作为 0x06 命令接收区） */
#define MODBUS_06_REC Modbus_write_regs[0]
#define MODBUS_03_REC Modbus_read_regs[0]
#define MODBUS_REC Modbus_holding_regs[0]

static uint32_t s_tick = 0;           // 内部计数器，每次Fsm_Run递增
static uint32_t timeout_deadline = 0; // 超时截止周期数（0表示未启动）
static volatile bool s_timeout_flag = false;

/**
 * @brief 检查当前 Flash 中的 APP 是否有效
 * @return true 有效，false 无效
 * @note 用户需要根据实际硬件实现校验（如 CRC、签名等）
 */
bool CheckAppValid(void)
{
    return false;
}
// extern bool CheckNewAppValid(void);
// extern void JumpToApp(void);
// extern int Flash_GetStatus(void); // 0-烧写中，1-完成且有效，2-完成但无效

typedef struct
{
    BootState src; // 源状态
    BootEvent evt; // 触发事件
    BootState dst; // 目标状态
} Transition;

/* 定义所有有效状态转换 */
static const Transition trans_table[] = {
    // 从等待状态收到合法 0x06 命令 → 进入升级状态
    {STATE_MODBUS_RECV, EVENT_RECV_LEGAL_06H, STATE_PROG_UPGRADE},
    // 等待状态下超时且 APP 有效 → 跳转 APP
    {STATE_MODBUS_RECV, EVENT_TIMEOUT_APP_VALID, STATE_JUMP_APP},
    // 等待状态下超时但 APP 无效 → 仍停留在等待状态（重新触发超时）
    {STATE_MODBUS_RECV, EVENT_TIMEOUT_APP_INVALID, STATE_MODBUS_RECV},
    // 升级状态下烧写完成且新 APP 有效 → 跳转 APP
    {STATE_PROG_UPGRADE, EVENT_BURN_COMPLETE_APP_VALID, STATE_JUMP_APP},
    // 升级状态下烧写完成但新 APP 无效 → 回到等待状态
    {STATE_PROG_UPGRADE, EVENT_BURN_COMPLETE_APP_INVALID, STATE_MODBUS_RECV},
    // 升级状态下烧写未完成（例如中途出错） → 回到等待状态
    {STATE_PROG_UPGRADE, EVENT_BURN_NOT_COMPLETE, STATE_MODBUS_RECV},
};
#define TRANS_COUNT (sizeof(trans_table) / sizeof(trans_table[0]))

/**
 * @brief 启动超时定时器
 * @note 设置截止 tick = 当前 tick + 超时周期数
 */
static void StartTimeout(void)
{
    timeout_deadline = s_tick + TIMEOUT_PERIODS;
}
/**
 * @brief 停止超时定时器（清零截止值）
 */
static void StopTimeout(void)
{
    timeout_deadline = 0;
    s_timeout_flag = false;
}
/**
 * @brief 判断是否发生了超时
 * @return true 超时发生，false 未超时或定时器未启动
 */
static bool IsTimeout(void)
{
    return (timeout_deadline != 0 && s_tick >= timeout_deadline);
}

/* ---------- Modbus接收 ---------- */
static void ModbusRecv_Entry(void)
{
    // 进入等待状态时启动超时定时器
    StartTimeout();
}
static BootEvent ModbusRecv_Do(void)
{
    // 接收到升级标志
    if (MODBUS_REC == 0x5B5B && MODBUS_06_REC > 0 && MODBUS_03_REC > 0)
    {
        // 停止超时定时器
        StopTimeout();
        return EVENT_RECV_LEGAL_06H;
    }

    // 检查是否超时
    if (IsTimeout())
    {
        // 停止超时定时器
        StopTimeout();
        // 根据 APP 是否有效决定事件
        if (CheckAppValid())
            return EVENT_TIMEOUT_APP_VALID;
        else
            return EVENT_TIMEOUT_APP_INVALID;
    }

    // 无事件发生，保持状态
    return EVENT_NONE;
}
static void ModbusRecv_Exit(void)
{
}

/* ---------- 程序升级 ---------- */
static void ProgUpgrade_Entry(void)
{
}
static BootEvent ProgUpgrade_Do(void)
{
    // 无事件发生，保持状态
    return EVENT_NONE;
}
static void ProgUpgrade_Exit(void)
{
}

/* ---------- 跳转APP ---------- */
static void JumpApp_Entry(void)
{
}
static BootEvent JumpApp_Do(void)
{
    // 无事件发生，保持状态
    return EVENT_NONE;
}
static void JumpApp_Exit(void)
{
}

// 状态描述表
static const Fsm_Struct state_descriptor[] = {
    [STATE_MODBUS_RECV] = {
        .state = STATE_MODBUS_RECV,
        .entry = ModbusRecv_Entry,
        .do_action = ModbusRecv_Do,
        .exit = ModbusRecv_Exit},
    [STATE_PROG_UPGRADE] = {.state = STATE_PROG_UPGRADE, .entry = ProgUpgrade_Entry, .do_action = ProgUpgrade_Do, .exit = ProgUpgrade_Exit},
    [STATE_JUMP_APP] = {.state = STATE_JUMP_APP, .entry = JumpApp_Entry, .do_action = JumpApp_Do, .exit = JumpApp_Exit}};

// 当前状态指针
static const Fsm_Struct *p_current = NULL;

/**
 * @brief 根据当前状态和事件查找下一个状态
 * @param cur 当前状态
 * @param evt 事件
 * @return 下一个状态，若未找到则返回原状态
 */
static BootState FindNextState(BootState cur, BootEvent evt)
{
    for (size_t i = 0; i < TRANS_COUNT; i++)
    {
        if (trans_table[i].src == cur && trans_table[i].evt == evt)
            return trans_table[i].dst;
    }
    return cur;
}
/**
 * @brief 执行状态切换（调用退出和进入回调）
 * @param new_state 目标状态
 */
static void ChangeState(BootState new_state)
{
    if (p_current && p_current->exit)
        p_current->exit();
    p_current = &state_descriptor[new_state];
    if (p_current && p_current->entry)
        p_current->entry();
}

/**
 * @brief 状态机初始化
 * @note 重置 tick 计数器，并进入 STATE_MODBUS_RECV 状态
 */
void Fsm_Init(void)
{
    s_tick = 0; // 重置计数器
    ChangeState(STATE_MODBUS_RECV);
}
/**
 * @brief 状态机运行函数（需周期性调用，间隔 TICK_PERIOD_MS）
 * @note 每次调用增加 tick，执行当前状态的 do_action，并根据事件进行状态转移
 */
void Fsm_Run(void)
{
    s_tick++;
    if (IsTimeout())
    {
        s_timeout_flag = true;
    }
}

void Fsm_Process(void)
{
    if (!p_current)
        return;

    BootEvent evt = EVENT_NONE;

    // 超时事件
    if (evt == EVENT_NONE && s_timeout_flag)
    {
        s_timeout_flag = false;
        StopTimeout();
        if (p_current->state == STATE_MODBUS_RECV)
        {
            evt = CheckAppValid() ? EVENT_TIMEOUT_APP_VALID
                                  : EVENT_TIMEOUT_APP_INVALID;
        }
    }

    // 执行当前状态的 do_action
    if (evt == EVENT_NONE && p_current->do_action)
    {
        evt = p_current->do_action();
    }

    // 如果没有事件，直接返回
    if (evt == EVENT_NONE)
        return;

    // 状态转移
    BootState next = FindNextState(p_current->state, evt);
    if (next != p_current->state)
    {
        ChangeState(next);
    }
    else if (evt == EVENT_TIMEOUT_APP_INVALID)
    {
        ChangeState(p_current->state);
    }
}
/**
 * @brief 查询当前状态是否等于指定状态
 * @param state 要查询的状态
 * @return true 当前状态等于 state，false 否则
 */
bool Fsm_IsInState(BootState state)
{
    return (p_current != NULL && p_current->state == state);
}
