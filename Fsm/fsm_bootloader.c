#include "fsm_bootloader.h"
#include <stddef.h>

extern volatile uint16_t Modbus_read_regs[];
extern volatile uint16_t Modbus_write_regs[];
extern uint16_t Modbus_holding_regs[];

#define TICK_PERIOD_MS 10                             // Fsm_Run 调用间隔（ms）
#define TIMEOUT_MS 5000                               // 超时总时长（ms）
#define TIMEOUT_PERIODS (TIMEOUT_MS / TICK_PERIOD_MS) // 超时周期数

/* 从 Modbus 寄存器提取命令数据 */
#define MODBUS_06_REC Modbus_write_regs[0]
#define MODBUS_03_REC Modbus_read_regs[0]
#define MODBUS_REC Modbus_holding_regs[0]

#define DEBUG 1 // 调试输出开关

/**
 * @brief 检查当前 Flash 中的 APP 是否有效
 * @return true 有效，false 无效
 * @note 用户需根据实际硬件实现（CRC、签名等）
 */
bool CheckAppValid(void)
{
    return true;
}

static uint32_t s_tick = 0;           // 系统 tick 计数器（每次 Fsm_Run 递增）
static uint32_t timeout_deadline = 0; // 超时截止 tick 值，0 表示未启动定时器

/**
 * @brief 启动超时定时器（设置截止 tick）
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
}

/**
 * @brief 检查是否发生超时
 * @return true 超时，false 未超时或定时器未启动
 */
static bool IsTimeout(void)
{
    return (timeout_deadline != 0 && s_tick >= timeout_deadline);
}

/**
 * @brief 转换表条目结构
 */
typedef struct
{
    BootState src; // 源状态
    BootEvent evt; // 触发事件
    BootState dst; // 目标状态
} Transition;

/**
 * @brief 所有有效状态转换规则
 */
static const Transition g_trans_table[] = {
    // 等待命令状态下，收到合法 0x06 写寄存器命令 → 进入升级状态
    {STATE_MODBUS_RECV, EVENT_RECV_LEGAL_06H, STATE_PROG_UPGRADE},
    // 等待命令状态下，超时且 APP 有效 → 跳转 APP
    {STATE_MODBUS_RECV, EVENT_TIMEOUT_APP_VALID, STATE_JUMP_APP},
    // 等待命令状态下，超时但 APP 无效 → 重新进入等待（重置超时）
    {STATE_MODBUS_RECV, EVENT_TIMEOUT_APP_INVALID, STATE_MODBUS_RECV},
    // 升级状态下，烧写完成且新 APP 有效 → 跳转 APP
    {STATE_PROG_UPGRADE, EVENT_BURN_COMPLETE_APP_VALID, STATE_JUMP_APP},
    // 升级状态下，烧写完成但新 APP 无效 → 回到等待命令
    {STATE_PROG_UPGRADE, EVENT_BURN_COMPLETE_APP_INVALID, STATE_MODBUS_RECV},
    // 升级状态下，烧写未完成（或出错）→ 回到等待命令
    {STATE_PROG_UPGRADE, EVENT_BURN_NOT_COMPLETE, STATE_MODBUS_RECV},
};
#define TRANS_COUNT (sizeof(g_trans_table) / sizeof(g_trans_table[0]))

/* ---------------- 状态 STATE_MODBUS_RECV ---------------- */

/**
 * @brief 进入等待命令状态：启动超时定时器
 */
static void ModbusRecv_Entry(void)
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
static BootEvent ModbusRecv_Do(void)
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

static void ModbusRecv_Exit(void)
{
    // 退出等待状态时的清理（本例无特殊需求）
}

/* ---------------- 状态 STATE_PROG_UPGRADE ---------------- */

/**
 * @brief 进入升级状态：启动超时定时器（烧写超时保护）
 */
static void ProgUpgrade_Entry(void)
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
static BootEvent ProgUpgrade_Do(void)
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

static void ProgUpgrade_Exit(void)
{
    // 退出升级状态时的清理
}

/* ---------------- 状态 STATE_JUMP_APP ---------------- */

/**
 * @brief 进入跳转状态：执行跳转（通常不返回）
 */
static void JumpApp_Entry(void)
{
#if DEBUG
    printf("[FSM] Enter STATE_JUMP_APP, tick=%lu\n", (unsigned long)s_tick);
#endif
    // 实际应调用 JumpToApp();  // 该函数不会返回
}

/**
 * @brief 跳转状态的核心执行（此状态为终态，一般无操作）
 */
static BootEvent JumpApp_Do(void)
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

static void JumpApp_Exit(void)
{
    // 无操作
}

/**
 * @brief 状态描述符数组，按状态索引
 */
static const Fsm_Struct state_descriptor[] = {
    [STATE_MODBUS_RECV] = {
        .state = STATE_MODBUS_RECV,
        .entry = ModbusRecv_Entry,
        .do_action = ModbusRecv_Do,
        .exit = ModbusRecv_Exit},
    [STATE_PROG_UPGRADE] = {.state = STATE_PROG_UPGRADE, .entry = ProgUpgrade_Entry, .do_action = ProgUpgrade_Do, .exit = ProgUpgrade_Exit},
    [STATE_JUMP_APP] = {.state = STATE_JUMP_APP, .entry = JumpApp_Entry, .do_action = JumpApp_Do, .exit = JumpApp_Exit}};

static const Fsm_Struct *p_current = NULL; // 当前状态指针

/**
 * @brief 根据当前状态和事件查找下一个状态
 * @param cur 当前状态
 * @param evt 触发事件
 * @return 下一个状态，若未找到规则则返回原状态
 */
static BootState FindNextState(BootState cur, BootEvent evt)
{
    for (size_t i = 0; i < TRANS_COUNT; i++)
    {
        if (g_trans_table[i].src == cur && g_trans_table[i].evt == evt)
            return g_trans_table[i].dst;
    }
    return cur; // 无匹配规则，保持状态
}

/**
 * @brief 执行状态切换（调用旧状态退出函数，新状态进入函数）
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
 * @brief 状态机初始化：重置 tick，进入等待命令状态
 */
void Fsm_Init(void)
{
    s_tick = 0;
    ChangeState(STATE_MODBUS_RECV);
}

/**
 * @brief 状态机时间驱动函数，需以固定周期（TICK_PERIOD_MS）调用
 * @note 每次调用递增内部 tick，超时判断在 do_action 中完成
 */
void Fsm_Run(void)
{
    s_tick++;
}

/**
 * @brief 状态机处理函数，由主循环反复调用
 * @note 执行当前状态的 do_action，根据返回事件驱动状态转移
 */
void Fsm_Process(void)
{
    if (!p_current)
        return;

    // 执行当前状态的 do_action，获取事件
    BootEvent evt = EVENT_NONE;
    if (p_current->do_action)
        evt = p_current->do_action();

    // 无事件则直接返回
    if (evt == EVENT_NONE)
        return;

    // 根据事件查找目标状态
    BootState next = FindNextState(p_current->state, evt);

    if (next != p_current->state)
    {
        // 正常状态转移
        ChangeState(next);
    }
    else if (evt == EVENT_TIMEOUT_APP_INVALID)
    {
        // 超时且 APP 无效：重新进入当前状态以重置超时定时器
        ChangeState(p_current->state);
    }
    // 其他情况（事件未导致转移且不是特定重入事件）忽略
}

/**
 * @brief 查询当前状态是否等于指定状态
 * @param state 要查询的状态
 * @return true 相等，false 不等或未初始化
 */
bool Fsm_IsInState(BootState state)
{
    return (p_current != NULL && p_current->state == state);
}
