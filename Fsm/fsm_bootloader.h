#ifndef __FSM_BOOTLOADER_H
#define __FSM_BOOTLOADER_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Bootloader 状态枚举
 *
 * STATE_MODBUS_RECV  : 等待 Modbus 命令（空闲态）
 * STATE_PROG_UPGRADE : 正在接收固件并烧写（升级态）
 * STATE_JUMP_APP     : 跳转到应用程序（终态，不再返回）
 */
typedef enum
{
    STATE_MODBUS_RECV = 0,
    STATE_PROG_UPGRADE,
    STATE_JUMP_APP
} BootState;

/**
 * @brief 状态机事件枚举
 *
 * EVENT_NONE                    : 无事件（保持当前状态）
 * EVENT_RECV_LEGAL_06H          : 收到合法的 Modbus 0x06 写寄存器命令（触发升级）
 * EVENT_TIMEOUT_APP_VALID       : 超时且 APP 有效（跳转 APP）
 * EVENT_TIMEOUT_APP_INVALID     : 超时且 APP 无效（重新进入等待状态，复位超时）
 * EVENT_BURN_COMPLETE_APP_VALID : 烧写完成且新 APP 有效（跳转 APP）
 * EVENT_BURN_COMPLETE_APP_INVALID: 烧写完成但新 APP 无效（回到等待状态）
 * EVENT_BURN_NOT_COMPLETE       : 烧写未完成（回到等待状态，等待下次命令）
 */
typedef enum
{
    EVENT_NONE = 0,
    EVENT_RECV_LEGAL_06H,
    EVENT_TIMEOUT_APP_VALID,
    EVENT_TIMEOUT_APP_INVALID,
    EVENT_BURN_COMPLETE_APP_VALID,
    EVENT_BURN_COMPLETE_APP_INVALID,
    EVENT_BURN_NOT_COMPLETE
} BootEvent;

/* 状态函数指针类型定义 */
typedef void (*StateFunc_Entry)(void);   // 进入状态时的回调
typedef BootEvent (*StateFunc_Do)(void); // 状态运行时的回调（返回事件）
typedef void (*StateFunc_Exit)(void);    // 退出状态时的回调

/**
 * @brief 状态描述结构体
 *
 * 每个状态包含其 ID、进入函数、执行函数和退出函数。
 */
typedef struct
{
    BootState state;        // 状态
    StateFunc_Entry entry;  // 状态入口函数(定时器内执行)
    StateFunc_Do do_action; // 状态核心执行函数(循环执行)
    StateFunc_Exit exit;    // 退出函数(定时器内执行)
} Fsm_Struct;

/* 公共 API 声明 */
void Fsm_Init(void);             // 初始化状态机，进入 STATE_MODBUS_RECV
void Fsm_Run(void);              // 定时调用（例如每 10ms），驱动状态机运行
bool Fsm_IsInState(BootState s); // 查询当前是否处于指定状态
void Fsm_Process(void);          // 由主循环反复调用，执行动作和状态切换
#endif
