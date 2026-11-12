#include "mi2c.h"
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/dwt.h"


static __attribute__((section(".ccmram"))) I2C_HandleTypeDef hi2c_eeprom;


static uint32_t MIIC_TimeoutMsToCount( uint32_t timeout_ms ) {
    return timeout_ms * ( HAL_RCC_GetHCLKFreq() / 1000u );
}


/*
 * set=1：等到 reg 出现 mask；set=0：等到 mask 消失。
 * timeout_ms=0 表示一直等。I2C 标志是微秒级，这里自旋，不用 vTaskDelay。
 *
 * 进 while 前不必再延时：标志还没到位时条件为真，会继续等，不会误判成功。
 * WAIT_FOREVER 只是关掉超时，逻辑一样。要防的是「上一次残留标志还在」，
 * 靠写 DR / 读 SR1+SR2 清位，不是靠空等几微秒。
 */
static HAL_StatusTypeDef MIIC_WaitReg( volatile uint32_t *reg, uint32_t mask,
                                       uint8_t set, uint32_t timeout_ms ) {
    uint32_t start = HX_DWT_GetCount();
    uint32_t limit = 0u;

    if ( timeout_ms != MIIC_WAIT_FOREVER )
        limit = MIIC_TimeoutMsToCount( timeout_ms );

    while ( set ? ( ( *reg & mask ) == 0u ) : ( ( *reg & mask ) != 0u ) ) {
        if ( timeout_ms != MIIC_WAIT_FOREVER && HX_DWT_Elapsed( start ) >= limit )
            return HAL_TIMEOUT;
    }

    return HAL_OK;
}


static void MIIC_ClearAddrFlag( void ) {
    volatile uint32_t tmp;

    tmp = hi2c_eeprom.Instance->SR1;
    tmp = hi2c_eeprom.Instance->SR2;
    (void)tmp;
}


void MIIC_Init( void ) {
    GPIO_InitTypeDef gpio;

    MIIC_GPIO_CLK_EN();
    MIIC_PERIPH_CLK_EN();
    MIIC_PERIPH_RESET();

    gpio.Pin = MIIC_SCL_PIN | MIIC_SDA_PIN;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = MIIC_GPIO_AF;
    HAL_GPIO_Init( MIIC_GPIO_PORT, &gpio );

    hi2c_eeprom.Instance = MIIC_INSTANCE;
    hi2c_eeprom.Init.ClockSpeed = MIIC_CLOCK_HZ;
    hi2c_eeprom.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c_eeprom.Init.OwnAddress1 = 0;
    hi2c_eeprom.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c_eeprom.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c_eeprom.Init.OwnAddress2 = 0;
    hi2c_eeprom.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    /* DISABLE = 允许从机时钟拉伸（24C02 写周期会用到） */
    hi2c_eeprom.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if ( HAL_I2C_Init( &hi2c_eeprom ) != HAL_OK ) {
        HX_UART_Printf( "[ERROR] I2C1 init failed.\n" );
        return;
    }

    HX_UART_Printf( "[INFO] I2C1 Initialize OK. SCL=PB8 SDA=PB9 100kHz stretch=on\n" );
}


I2C_HandleTypeDef *GetMIICHandle( void ) {
    return &hi2c_eeprom;
}


HAL_StatusTypeDef MIIC_Start( uint32_t timeout_ms ) {
    /* 不要先等 BUSY=0：重复 START 时总线本来就是忙的 */
    hi2c_eeprom.Instance->CR1 |= I2C_CR1_START_Msk;
    return MIIC_WaitReg( &hi2c_eeprom.Instance->SR1, I2C_SR1_SB, 1u, timeout_ms );
}


HAL_StatusTypeDef MIIC_Stop( uint32_t timeout_ms ) {
    hi2c_eeprom.Instance->CR1 |= I2C_CR1_STOP_Msk;
    return MIIC_WaitReg( &hi2c_eeprom.Instance->SR2, I2C_SR2_BUSY, 0u, timeout_ms );
}


/* 出错收尾：清 NACK，发 STOP，等总线放开。调用方再决定返回 ERROR 还是 TIMEOUT。 */
static HAL_StatusTypeDef MIIC_Abort( uint32_t timeout_ms ) {
    if ( hi2c_eeprom.Instance->SR1 & I2C_SR1_AF )
        hi2c_eeprom.Instance->SR1 &= ~I2C_SR1_AF;
    return MIIC_Stop( timeout_ms );
}


HAL_StatusTypeDef MIIC_SendAddr( uint32_t addr_7b, uint32_t rw, uint32_t timeout_ms ) {
    HAL_StatusTypeDef res;

    if ( rw > 1u )
        rw = 1u;

    hi2c_eeprom.Instance->DR = (uint32_t)( ( addr_7b << 1 ) | rw );
    res = MIIC_WaitReg( &hi2c_eeprom.Instance->SR1, I2C_SR1_ADDR, 1u, timeout_ms );
    if ( res != HAL_OK ) {
        /* 从机 NACK 时是 AF，ADDR 不会置位 */
        if ( hi2c_eeprom.Instance->SR1 & I2C_SR1_AF ) {
            hi2c_eeprom.Instance->SR1 &= ~I2C_SR1_AF;
            HX_UART_Printf( "[WARN] I2C slave NACK on addr. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        }
        else {
            HX_UART_Printf( "[WARN] I2C slave no ADDR. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        }
        return res;
    }

    /*
     * 写：立刻清 ADDR，才能继续 TXE。
     * 读：ADDR 留给 MIIC_Read，必须先按长度设好 ACK 再清。
     */
    if ( rw == 0u )
        MIIC_ClearAddrFlag();
    return HAL_OK;
}


HAL_StatusTypeDef MIIC_SendByte( uint8_t byte, uint32_t timeout_ms ) {
    HAL_StatusTypeDef res;

    /* 先确定TxE已排空. */
    res = MIIC_WaitReg( &hi2c_eeprom.Instance->SR1, I2C_SR1_TXE, 1u, timeout_ms );
    if ( res != HAL_OK )
        return res;

    hi2c_eeprom.Instance->DR = byte;
    /* BTF：移位寄存器和 DR 都空，本字节（含 ACK）已结束 */
    res = MIIC_WaitReg( &hi2c_eeprom.Instance->SR1, I2C_SR1_BTF, 1u, timeout_ms );
    if ( res != HAL_OK ) {
        HX_UART_Printf( "[WARN] I2C byte transfer error. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        return res;
    }

    return HAL_OK;
}


HAL_StatusTypeDef MIIC_ReadByte( uint32_t ack, uint8_t *out, uint32_t timeout_ms ) {
    HAL_StatusTypeDef res;
    uint8_t addr_pending;

    if ( out == NULL )
        return HAL_ERROR;

    if ( ack )
        hi2c_eeprom.Instance->CR1 |= I2C_CR1_ACK;
    else
        hi2c_eeprom.Instance->CR1 &= ~I2C_CR1_ACK;

    /* ADDR 只能读 SR1 再 SR2 清。单字节必须先 NACK 再清 ADDR，并马上 STOP。 */
    addr_pending = ( hi2c_eeprom.Instance->SR1 & I2C_SR1_ADDR ) ? 1u : 0u;
    if ( addr_pending ) {
        MIIC_ClearAddrFlag();
        if ( !ack )
            hi2c_eeprom.Instance->CR1 |= I2C_CR1_STOP;
    }

    res = MIIC_WaitReg( &hi2c_eeprom.Instance->SR1, I2C_SR1_RXNE, 1u, timeout_ms );
    if ( res != HAL_OK ) {
        HX_UART_Printf( "[WARN] I2C RXNE timeout. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        return res;
    }

    *out = (uint8_t)hi2c_eeprom.Instance->DR;
    return HAL_OK;
}


HAL_StatusTypeDef MIIC_Read( uint8_t *buf, uint16_t len, uint32_t timeout_ms ) {
    I2C_TypeDef *i2c = hi2c_eeprom.Instance;
    uint16_t left = len;
    HAL_StatusTypeDef res = HAL_OK;

    if ( buf == NULL || len == 0u )
        return HAL_ERROR;

    /*
     * ACK 在字节移位结束时发出。清 ADDR 之后硬件马上开始收，
     * 所以 NACK / POS / STOP 必须在清 ADDR 之前写好，不能等 RXNE 再关 ACK。
     */
    if ( left == 1u ) {
        i2c->CR1 &= ~( I2C_CR1_ACK | I2C_CR1_POS );
        MIIC_ClearAddrFlag();
        i2c->CR1 |= I2C_CR1_STOP;
    }
    else if ( left == 2u ) {
        i2c->CR1 &= ~I2C_CR1_ACK;
        i2c->CR1 |= I2C_CR1_POS;
        MIIC_ClearAddrFlag();
    }
    else {
        i2c->CR1 |= I2C_CR1_ACK;
        i2c->CR1 &= ~I2C_CR1_POS;
        MIIC_ClearAddrFlag();
    }

    while ( left > 0u && res == HAL_OK ) {
        if ( left > 3u ) {
            res = MIIC_WaitReg( &i2c->SR1, I2C_SR1_RXNE, 1u, timeout_ms );
            if ( res != HAL_OK )
                break;
            *buf++ = (uint8_t)i2c->DR;
            left--;
            if ( i2c->SR1 & I2C_SR1_BTF ) {
                *buf++ = (uint8_t)i2c->DR;
                left--;
            }
        }
        else if ( left == 1u ) {
            res = MIIC_WaitReg( &i2c->SR1, I2C_SR1_RXNE, 1u, timeout_ms );
            if ( res != HAL_OK )
                break;
            *buf++ = (uint8_t)i2c->DR;
            left--;
        }
        else if ( left == 2u ) {
            res = MIIC_WaitReg( &i2c->SR1, I2C_SR1_BTF, 1u, timeout_ms );
            if ( res != HAL_OK )
                break;
            i2c->CR1 |= I2C_CR1_STOP;
            *buf++ = (uint8_t)i2c->DR;
            *buf++ = (uint8_t)i2c->DR;
            left = 0u;
        }
        else {
            /* 还剩 3 字节：BTF 时关 ACK，使第 3 字节收到 NACK */
            res = MIIC_WaitReg( &i2c->SR1, I2C_SR1_BTF, 1u, timeout_ms );
            if ( res != HAL_OK )
                break;
            i2c->CR1 &= ~I2C_CR1_ACK;
            *buf++ = (uint8_t)i2c->DR;
            left--;

            res = MIIC_WaitReg( &i2c->SR1, I2C_SR1_BTF, 1u, timeout_ms );
            if ( res != HAL_OK )
                break;
            i2c->CR1 |= I2C_CR1_STOP;
            *buf++ = (uint8_t)i2c->DR;
            *buf++ = (uint8_t)i2c->DR;
            left = 0u;
        }
    }

    i2c->CR1 &= ~I2C_CR1_POS;
    if ( res != HAL_OK ) {
        HX_UART_Printf( "[WARN] I2C read timeout, left=%u. Files: %s, Lines: %d\n",
                (unsigned)left, __FILE__, __LINE__ );
        (void)MIIC_Abort( timeout_ms );
        return res;
    }

    return MIIC_WaitReg( &i2c->SR2, I2C_SR2_BUSY, 0u, timeout_ms );
}


HAL_StatusTypeDef MIIC_Write( const uint8_t *buf, uint16_t len, uint32_t timeout_ms ) {
    I2C_TypeDef *i2c = hi2c_eeprom.Instance;
    uint16_t left = len;
    uint16_t idx = 0u;
    HAL_StatusTypeDef res;

    if ( buf == NULL || len == 0u )
        return HAL_ERROR;

    while ( left > 0u ) {
        /* NACK：这一字节从机不要了，后面的不能再塞 */
        if ( i2c->SR1 & I2C_SR1_AF ) {
            res = MIIC_Abort( timeout_ms );
            return ( res == HAL_OK ) ? HAL_ERROR : res;
        }

        res = MIIC_WaitReg( &i2c->SR1, I2C_SR1_TXE, 1u, timeout_ms );
        if ( res != HAL_OK ) {
            (void)MIIC_Abort( timeout_ms );
            return res;
        }

        i2c->DR = buf[idx++];
        left--;

        /* 两级都空（CPU 没跟上）时马上再填一字节，但不要在 left==0 时多写 */
        if ( left > 0u && ( i2c->SR1 & I2C_SR1_BTF ) ) {
            i2c->DR = buf[idx++];
            left--;
        }
    }

    /* 全部入队后只等一次 BTF：末字节已移出且收到 ACK，再 STOP */
    res = MIIC_WaitReg( &i2c->SR1, I2C_SR1_BTF, 1u, timeout_ms );
    if ( res != HAL_OK || ( i2c->SR1 & I2C_SR1_AF ) ) {
        uint8_t nack = ( i2c->SR1 & I2C_SR1_AF ) ? 1u : 0u;
        HAL_StatusTypeDef stop = MIIC_Abort( timeout_ms );

        if ( nack )
            return ( stop == HAL_OK ) ? HAL_ERROR : stop;
        return res;
    }

    return MIIC_Stop( timeout_ms );
}
