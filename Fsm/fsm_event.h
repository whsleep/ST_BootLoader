#ifndef __FSM_EVENT_H
#define __FSM_EVENT_H

#include "fsm_bootloader.h"
#include "usart.h"
#include <stdio.h>

#define DEBUG 1 // 调试输出开关


void ModbusRecv_Entry(void);
BootEvent ModbusRecv_Do(void);
void ModbusRecv_Exit(void);


void ProgUpgrade_Entry(void);
BootEvent ProgUpgrade_Do(void);
void ProgUpgrade_Exit(void);


void JumpApp_Entry(void);
BootEvent JumpApp_Do(void);
void JumpApp_Exit(void);



#endif

