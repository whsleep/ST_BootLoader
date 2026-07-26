// fsm_bootloader.h
#ifndef __FSM_BOOTLOADER_H
#define __FSM_BOOTLOADER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========== 状态定义 ========== */
typedef enum {
    STATE_MODBUS_RECV = 0, // modbus接收
    STATE_PROG_UPGRADE,    // 程序升级
    STATE_JUMP_APP         // APP跳转
} BootState;

/* ========== 内部事件定义 ========== */
typedef enum {
    EVENT_NONE = 0,
    EVENT_RECV_LEGAL_06H,           /* 收到合法 06H 指令 */
    EVENT_TIMEOUT_APP_VALID,        /* 超时且 APP 有效 */
    EVENT_TIMEOUT_APP_INVALID,      /* 超时且 APP 无效 */
    EVENT_BURN_COMPLETE_APP_VALID,  /* 烧写完成且新 APP 有效 */
    EVENT_BURN_COMPLETE_APP_INVALID,/* 烧写完成但新 APP 无效 */
    EVENT_BURN_NOT_COMPLETE         /* 烧写未完成 */
} BootEvent;

/* ========== 状态动作函数指针类型 ========== */
typedef void (*StateFunc_Entry)(void);
typedef int  (*StateFunc_Do)(void);    
typedef void (*StateFunc_Exit)(void);

typedef struct Fsm{
    // 状态
    BootState state;
    // 事件
    BootEvent event;
    // 动作：进入、执行、退出
    StateFunc_Entry entry;
    StateFunc_Do    do_action;
    StateFunc_Exit  exit;
} Fsm_Struct;


#ifdef __cplusplus
}
#endif

#endif
