#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/delay/delay.h"
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/console/console.h"
#include "./SYSTEM/iic/mi2c.h"
#include "eeprom.h"
#include "data_task.h"
#include "FreeRTOS.h"
#include "task.h"


#define CONSOLE_TASK_STACK_WORDS    ( 512u )
#define CONSOLE_TASK_PRIORITY       ( 3u )
#define EEPROM_TASK_STACK_WORDS     ( 256u )
#define EEPROM_TASK_PRIORITY        ( 2u )
#define DATA_TASK_STACK_WORDS       ( 256u )
#define DATA_TASK_PRIORITY          ( 1u )


int main(void)
{
    HAL_Init();
    HAL_NVIC_SetPriorityGrouping( NVIC_PRIORITYGROUP_4 );

    sys_stm32_clock_init( 336, 8, 2, 7 );
    delay_init( 168 );

    /* 先建信号量，再开 UART IDLE，避免 GiveFromISR 空句柄 */
    HX_Console_Init();
    HX_UART_Init( 115200 );
    MIIC_Init();
    if ( EEPROM_Init() != HAL_OK )
        while ( 1 );

    if ( xTaskCreate( EEPROM_Task, "eeprom",
                      EEPROM_TASK_STACK_WORDS, NULL,
                      EEPROM_TASK_PRIORITY, NULL ) != pdPASS ) {
        HX_UART_Printf( "[ERROR] eeprom task create failed.\n" );
        while ( 1 );
    }

    if ( xTaskCreate( HX_DebugConsole_Task, "console",
                      CONSOLE_TASK_STACK_WORDS, NULL,
                      CONSOLE_TASK_PRIORITY, NULL ) != pdPASS ) {
        HX_UART_Printf( "[ERROR] console task create failed.\n" );
        while ( 1 );
    }

    if ( xTaskCreate( Data_Task, "data",
                      DATA_TASK_STACK_WORDS, NULL, 
                      DATA_TASK_PRIORITY, NULL ) != pdPASS ) {
        HX_UART_Printf( "[ERROR] data task create failed.\n" );
        while( 1 );
    }

    vTaskStartScheduler();

    while ( 1 ) {
        HX_UART_Printf( "[ERROR] The proj could not reach here! Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        for( ; ; );
    }
}
