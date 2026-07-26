#include "fsm_bootloader.h"

/* ========== 空壳业务函数（每个状态的具体动作） ========== */
static void ModbusRecv_Entry(void)  { /* 初始化串口、启动定时器等 */ }
static int  ModbusRecv_Do(void)     { return EVENT_NONE; /* 返回实际事件 */ }
static void ModbusRecv_Exit(void)   { /* 停止定时器等清理 */ }

static void ProgUpgrade_Entry(void) { /* 解锁 Flash */ }
static int  ProgUpgrade_Do(void)    { return EVENT_BURN_NOT_COMPLETE; }
static void ProgUpgrade_Exit(void)  { /* 锁定 Flash */ }

static void JumpApp_Entry(void)     { /* 关中断、设 VTOR、加载 MSP 并跳转（不返回） */ }
static int  JumpApp_Do(void)        { return EVENT_NONE; }
static void JumpApp_Exit(void)      { }

/* ========== 状态描述符表（每个状态对应一个 Fsm_Struct） ========== */
static const Fsm_Struct state_descriptor[] = {
    [STATE_MODBUS_RECV] = {
        .state     = STATE_MODBUS_RECV,
        .event     = EVENT_NONE,
        .entry     = ModbusRecv_Entry,
        .do_action = ModbusRecv_Do,
        .exit      = ModbusRecv_Exit
    },
    [STATE_PROG_UPGRADE] = {
        .state     = STATE_PROG_UPGRADE,
        .event     = EVENT_NONE,
        .entry     = ProgUpgrade_Entry,
        .do_action = ProgUpgrade_Do,
        .exit      = ProgUpgrade_Exit
    },
    [STATE_JUMP_APP] = {
        .state     = STATE_JUMP_APP,
        .event     = EVENT_NONE,
        .entry     = JumpApp_Entry,
        .do_action = JumpApp_Do,
        .exit      = JumpApp_Exit
    }
};

/* 当前状态指针，指向状态描述符表中的某一项 */
static const Fsm_Struct *p_current = NULL;

/* ========== 状态切换：先退出当前状态，再进入新状态 ========== */
static void ChangeState(BootState new_state)
{
    if (p_current && p_current->exit) {
        p_current->exit();
    }
    p_current = &state_descriptor[new_state];
    if (p_current->entry) {
        p_current->entry();
    }
}

/* ========== 初始化：设置初始状态并调用入口函数 ========== */
void Fsm_Init(void)
{
    ChangeState(STATE_MODBUS_RECV);
}

/* ========== 运行状态机（单次轮询，由外部主循环调用） ========== */
void Fsm_Run(void)
{
    if (!p_current) return;

    /* 执行当前状态的 do 动作，获取事件 */
    BootEvent event = (BootEvent)(p_current->do_action ? p_current->do_action() : EVENT_NONE);

    /* 根据当前状态和事件进行跳转 */
    switch (p_current->state) {
        case STATE_MODBUS_RECV:
            if (event == EVENT_RECV_LEGAL_06H) {
                ChangeState(STATE_PROG_UPGRADE);
            } else if (event == EVENT_TIMEOUT_APP_VALID) {
                ChangeState(STATE_JUMP_APP);
            } else if (event == EVENT_TIMEOUT_APP_INVALID) {
                /* 仅重置定时器，不切换状态（此处可重新调用 entry 实现） */
            }
            break;

        case STATE_PROG_UPGRADE:
            if (event == EVENT_BURN_COMPLETE_APP_VALID) {
                ChangeState(STATE_JUMP_APP);
            } else if (event == EVENT_BURN_COMPLETE_APP_INVALID) {
                ChangeState(STATE_MODBUS_RECV);
            }
            /* EVENT_BURN_NOT_COMPLETE 保持当前状态 */
            break;

        case STATE_JUMP_APP:
            /* 瞬时态，不处理循环事件 */
            break;

        default:
            break;
    }
}
