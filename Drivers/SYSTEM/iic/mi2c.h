#ifndef __MI2C_H__
#define __MI2C_H__

#include "stm32f4xx_hal.h"


#define MIIC_INSTANCE           ( I2C1 )
#define MIIC_GPIO_PORT          ( GPIOB )
#define MIIC_SCL_PIN            ( GPIO_PIN_8 )
#define MIIC_SDA_PIN            ( GPIO_PIN_9 )
#define MIIC_GPIO_AF            ( GPIO_AF4_I2C1 )
#define MIIC_CLOCK_HZ           ( 100000u )
#define MIIC_WAIT_FOREVER       ( 0u )
#define MIIC_ACK                ( 1u )
#define MIIC_NACK               ( 0u )

#define MIIC_GPIO_CLK_EN()      do { __HAL_RCC_GPIOB_CLK_ENABLE(); } while ( 0 )
#define MIIC_PERIPH_CLK_EN()    do { __HAL_RCC_I2C1_CLK_ENABLE(); } while ( 0 )
#define MIIC_PERIPH_RESET()     do { \
    __HAL_RCC_I2C1_FORCE_RESET(); \
    __HAL_RCC_I2C1_RELEASE_RESET(); \
} while ( 0 )


void MIIC_Init( void );
I2C_HandleTypeDef *GetMIICHandle( void );

HAL_StatusTypeDef MIIC_Start( uint32_t timeout_ms );
HAL_StatusTypeDef MIIC_Stop( uint32_t timeout_ms );
HAL_StatusTypeDef MIIC_SendAddr( uint32_t addr_7b, uint32_t rw, uint32_t timeout_ms );
HAL_StatusTypeDef MIIC_SendByte( uint8_t byte, uint32_t timeout_ms );
/* 工具函数，一次 1 字节。ack=MIIC_ACK 继续，MIIC_NACK 表示最后一字节。缓冲读取用 MIIC_Read。 */
HAL_StatusTypeDef MIIC_ReadByte( uint32_t ack, uint8_t *out, uint32_t timeout_ms );
/*
 * 读地址已发出且 ADDR 尚未清除（SendAddr 读方向不会清 ADDR）。
 * 内部按 1 / 2 / ≥3 字节在清 ADDR 前设好 ACK，并在末尾发 STOP、等 BUSY 掉。
 */
HAL_StatusTypeDef MIIC_Read( uint8_t *buf, uint16_t len, uint32_t timeout_ms );

/* 写地址已发出且 ADDR 已清。末字节等一次 BTF 再 STOP。 */
HAL_StatusTypeDef MIIC_Write( const uint8_t *buf, uint16_t len, uint32_t timeout_ms );


#endif /* __MI2C_H__ */
