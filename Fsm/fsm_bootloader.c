#include "fsm_bootloader.h"
#include <stddef.h>

extern Fsm_Struct state_descriptor[];

#define TICK_PERIOD_MS 10                             // Fsm_Run 调用间隔（ms）
#define TIMEOUT_MS 5000                               // 超时总时长（ms）
#define TIMEOUT_PERIODS (TIMEOUT_MS / TICK_PERIOD_MS) // 超时周期数

uint32_t s_tick = 0;                  // 系统 tick 计数器（每次 Fsm_Run 递增）
static uint32_t timeout_deadline = 0; // 超时截止 tick 值，0 表示未启动定时器

/**
 * @brief 启动超时定时器（设置截止 tick）
 */
void StartTimeout(void)
{
    timeout_deadline = s_tick + TIMEOUT_PERIODS;
}

/**
 * @brief 停止超时定时器（清零截止值）
 */
void StopTimeout(void)
{
    timeout_deadline = 0;
}

/**
 * @brief 检查是否发生超时
 * @return true 超时，false 未超时或定时器未启动
 */
bool IsTimeout(void)
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
