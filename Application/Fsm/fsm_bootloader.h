#ifndef __FSM_BOOTLOADER_H
#define __FSM_BOOTLOADER_H

#include <stdint.h>
#include <stdbool.h>
#include "usart.h"

extern uint32_t s_tick;

typedef enum
{
    STATE_MODBUS_RECV = 0,
    STATE_PROG_UPGRADE,
    STATE_JUMP_APP
} BootState;

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

typedef void (*StateFunc_Entry)(void);
typedef BootEvent (*StateFunc_Do)(void);
typedef void (*StateFunc_Exit)(void);

typedef struct
{
    BootState state;
    StateFunc_Entry entry;
    StateFunc_Do do_action;
    StateFunc_Exit exit;
} Fsm_Struct;

void Fsm_Init(void);
void Fsm_Run(void);
void Fsm_Process(void);
BootState Fsm_IsInState(void);

void StartTimeout(void);
void StopTimeout(void);
bool IsTimeout(void);
#endif
