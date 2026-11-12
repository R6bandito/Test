#ifndef  __DHT_DRIVER_H__
#define __DHT_DRIVER_H__


#include "stm32f4xx_hal.h"


#define  DHT_USART_INSTANCE         (USART6)
#define  DHT_USART_DQ_PIN           (GPIO_PIN_9)
#define  DHT_USART_DQ_PORT          (GPIOG)
#define  DHT_USART_BAUDRATE         (9600u)
#define  DHT_USART_DQ_PORT_EN()     do { __HAL_RCC_GPIOG_CLK_ENABLE(); } while(0)
#define  DHT_USART_CLK_EN()         do { __HAL_RCC_USART6_CLK_ENABLE(); } while(0)


#define  DHT_GPIO_SWITCH_OUT_MODE(PORT,PIN)    do {  \
    GPIO_InitTypeDef GPIO_InitStruct = {0}; \
    GPIO_InitStruct.Pin = PIN; \
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;  \
    GPIO_InitStruct.Pull = GPIO_PULLUP; \
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH; \
    HAL_GPIO_Init(PORT, &GPIO_InitStruct); \
 } while(0)

#define  DHT_GPIO_SWITCH_AF_MODE(PORT,PIN)  do { \
    GPIO_InitTypeDef GPIO_InitStruct = {0}; \
    GPIO_InitStruct.Pin = PIN; \
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;  \
    GPIO_InitStruct.Pull = GPIO_PULLUP; \
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH; \
    GPIO_InitStruct.Alternate = GPIO_AF8_USART6; \
    HAL_GPIO_Init(PORT, &GPIO_InitStruct); \
} while(0)


HAL_StatusTypeDef DHT_Init( void );

HAL_StatusTypeDef DHT_Poll( float *humidity, float *temp );

#endif /* __DHT_DRIVER_H__ */
