#ifndef __MY_USART_H__
#define __MY_USART_H__


#include "stm32f4xx_hal.h"
#include <stdio.h>


#define UART_RCV_BUF_LEN        (256u)
/* 不小于协议最大响应线帧，BuildResponse 结果可经 TxFrame 发出 */
#define UART_TX_BUF_LEN         (280u)
#define UART_TX_WAIT_FOREVER    ( 0u )

#define USE_UART_INSTANCE       (USART1)
#define USE_UART_TX_PIN         (GPIO_PIN_9)
#define USE_UART_RX_PIN         (GPIO_PIN_10)
#define USE_UART_CLK_EN()       do { __HAL_RCC_USART1_CLK_ENABLE(); } while(0)

#define USE_UART_GPIO_PORT      (GPIOA)
#define USE_UART_GPIO_CLK_EN()  do { __HAL_RCC_GPIOA_CLK_ENABLE(); } while(0)



void HX_UART_Init( uint32_t baudrate );

/* IDLE 中断入口；段长度在模块内维护，不暴露给 it.c */
void HX_UART_OnIdle( void );
uint8_t HX_UART_RxPending( void );
/* 无待读段：HAL_BUSY；成功取出且 *seg_len>0：HAL_OK；参数/容量错误：HAL_ERROR */
HAL_StatusTypeDef HX_UART_ReadSegment( uint8_t *read_buf, uint16_t read_cap, uint16_t *seg_len );
uint16_t HX_UART_RxOverflowCount( void );

/*
 * 阻塞发送整帧（DMA + 等 TC）。timeout_ms=0 表示一直等。
 * HAL_OK 成功；HAL_BUSY 通道占用且在 timeout 内未等到；HAL_TIMEOUT 发送未完成超时；HAL_ERROR 参数错误
 */
HAL_StatusTypeDef HX_UART_TxFrame( const uint8_t *p, uint16_t len, uint32_t timeout_ms );
/*
 * 日志的一笔事务：格式化后整段发出再放锁。
 * RTOS 下不要用 libc printf（会拆成多次 fputc）。
 */
int HX_UART_Printf( const char *fmt, ... );
uint8_t HX_UART_TxBusy( void );
uint8_t *HX_UART_TxBuf( void );

UART_HandleTypeDef *GetConsoleUartHandle( void );
DMA_HandleTypeDef *GetConsoleDMARxHandle( void );
DMA_HandleTypeDef *GetConsoleDMATxHandle( void );


#endif /* __MY_USART_H__ */
