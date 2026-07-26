#include "fsm_bootloader.h"
#include <stddef.h>

extern volatile uint16_t Modbus_read_regs;
extern volatile uint16_t Modbus_write_regs;

#define TICK_PERIOD_MS 10                             // 每次Fsm_Run调用的间隔（毫秒）
#define TIMEOUT_MS 5000                               // 超时时间（毫秒）
#define TIMEOUT_PERIODS (TIMEOUT_MS / TICK_PERIOD_MS) // 500个周期

#define MODBUS_06_REC Modbus_write_regs[0]
#define MODBUS_03_REC Modbus_read_regs[0]

// /* ============================================================
//    外部硬件接口（用户需实现）
//    ============================================================ */
bool CheckAppValid(void)
{
}
// extern bool CheckNewAppValid(void);
// extern void JumpToApp(void);
// extern int Flash_GetStatus(void); // 0-烧写中，1-完成且有效，2-完成但无效

static uint32_t s_tick = 0;           // 内部计数器，每次Fsm_Run递增
static uint32_t timeout_deadline = 0; // 超时截止周期数（0表示未启动）

typedef struct
{
    BootState src;
    BootEvent evt;
    BootState dst;
} Transition;

static const Transition trans_table[] = {
    {STATE_MODBUS_RECV, EVENT_RECV_LEGAL_06H, STATE_PROG_UPGRADE},
    {STATE_MODBUS_RECV, EVENT_TIMEOUT_APP_VALID, STATE_JUMP_APP},
    {STATE_MODBUS_RECV, EVENT_TIMEOUT_APP_INVALID, STATE_MODBUS_RECV},
    {STATE_PROG_UPGRADE, EVENT_BURN_COMPLETE_APP_VALID, STATE_JUMP_APP},
    {STATE_PROG_UPGRADE, EVENT_BURN_COMPLETE_APP_INVALID, STATE_MODBUS_RECV},
    {STATE_PROG_UPGRADE, EVENT_BURN_NOT_COMPLETE, STATE_MODBUS_RECV},
};
#define TRANS_COUNT (sizeof(trans_table) / sizeof(trans_table[0]))

/* ============================================================
   超时管理（内部函数）
   ============================================================ */
static void StartTimeout(void)
{
    timeout_deadline = s_tick + TIMEOUT_PERIODS;
}

static void StopTimeout(void)
{
    timeout_deadline = 0;
}

static bool IsTimeout(void)
{
    return (timeout_deadline != 0 && s_tick >= timeout_deadline);
}

/* ---------- Modbus接收 ---------- */
static void ModbusRecv_Entry(void)
{
}
static BootEvent ModbusRecv_Do(void)
{
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
}
static void JumpApp_Exit(void)
{
}

static const Fsm_Struct state_descriptor[] = {
    [STATE_MODBUS_RECV] = {
        .state = STATE_MODBUS_RECV,
        .entry = ModbusRecv_Entry,
        .do_action = ModbusRecv_Do,
        .exit = ModbusRecv_Exit},
    [STATE_PROG_UPGRADE] = {.state = STATE_PROG_UPGRADE, .entry = ProgUpgrade_Entry, .do_action = ProgUpgrade_Do, .exit = ProgUpgrade_Exit},
    [STATE_JUMP_APP] = {.state = STATE_JUMP_APP, .entry = JumpApp_Entry, .do_action = JumpApp_Do, .exit = JumpApp_Exit}};

/* ============================================================
   当前状态指针
   ============================================================ */
static const Fsm_Struct *p_current = NULL;

/* ============================================================
   辅助函数
   ============================================================ */
static BootState FindNextState(BootState cur, BootEvent evt)
{
    for (size_t i = 0; i < TRANS_COUNT; i++)
    {
        if (trans_table[i].src == cur && trans_table[i].evt == evt)
            return trans_table[i].dst;
    }
    return cur;
}

static void ChangeState(BootState new_state)
{
    if (p_current && p_current->exit)
        p_current->exit();
    p_current = &state_descriptor[new_state];
    if (p_current && p_current->entry)
        p_current->entry();
}

/* ============================================================
   公共API
   ============================================================ */
void Fsm_Init(void)
{
    s_tick = 0; // 重置计数器
    ChangeState(STATE_MODBUS_RECV);
}

void Fsm_Run(void)
{
    if (!p_current)
        return;

    // 递增内部计数器（每次调用代表一个周期）
    s_tick++;

    // 执行当前状态的do，获取事件
    BootEvent evt = p_current->do_action ? p_current->do_action() : EVENT_NONE;
    BootState next = FindNextState(p_current->state, evt);
    if (next != p_current->state)
    {
        ChangeState(next);
    }
    else
    {
        // 如果超时无效，需要重新进入当前状态以重置超时
        if (evt == EVENT_TIMEOUT_APP_INVALID)
        {
            ChangeState(p_current->state);
        }
    }
}

bool Fsm_IsInState(BootState state)
{
    return (p_current != NULL && p_current->state == state);
}
