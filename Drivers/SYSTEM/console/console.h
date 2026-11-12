#ifndef __HX_CONSOLE_H__
#define __HX_CONSOLE_H__

#include "stm32f4xx_hal.h"
#include "./SYSTEM/proto/proto.h"


#define HX_CONSOLE_DISPATCH_MAX     ( 16u )

typedef HX_ProtoStat ( *HX_ConsoleCmdHandler )( const HX_ProtoRequest *req, HX_ProtoResponse *rsp );

typedef struct {
    uint8_t                 class_id;
    uint8_t                 cmd;
    HX_ConsoleCmdHandler    handler;
} HX_ConsoleDispatchEntry;


/*
 * HX_UART_Init 之前调 HX_Console_Init()（先建信号量，避免 IDLE 里 Give 空句柄）。
 * 任务：HX_DebugConsole_Task；解析仍走 HX_Console_Poll。
 */
HAL_StatusTypeDef HX_Console_RegisterHandler( uint8_t class_id, uint8_t cmd,
                                              HX_ConsoleCmdHandler handler );
void HX_Console_Init( void );
void HX_Console_Poll( void );
void HX_DebugConsole_Task( void *arg );


#endif /* __HX_CONSOLE_H__ */
