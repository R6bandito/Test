#ifndef __EEPROM_H__
#define __EEPROM_H__

#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"


#define AT24C02_ADDR              (0x50u)
#define EEPROM_SIZE_BYTES         (256u)
#define EEPROM_PAGE_VOL_BYTES     (8u)
#define EEPROM_SELFTEST_MAGIC     (0xAFFADEADu)
#define EEPROM_QUEUE_LENGTH       (5u)


typedef enum {
    EEPROM_OP_PROBE = 0,
    EEPROM_OP_READ,
    EEPROM_OP_WRITE,
} EEPROM_Op;


typedef struct {
    EEPROM_Op          op;
    uint8_t            addr;
    uint16_t           len;
    uint8_t           *buf;      /* 读：任务写入；写：任务只读。探测不用 */
    uint16_t           buf_len;
    HAL_StatusTypeDef  status;   /* 任务写完再 Give */
    SemaphoreHandle_t  done;     /* 二值信号量，调用方自己建 */
} EEPROM_Request;


HAL_StatusTypeDef EEPROM_Init( void );

/* 投递请求并等待任务完成。req 和 buf 必须保持到返回。队列里放的是指针。 */
HAL_StatusTypeDef EEPROM_Submit( EEPROM_Request *req );


/* START + 写地址 0x50 + STOP。NACK 表示器件不在，或 24C02 还在内部写周期。 */
HAL_StatusTypeDef EEPROM_Probe( void );

/*
 * 随机读：写片内地址 → 重复起始 → 读地址 → MIIC_Read。
 * MIIC_Read 自己发 STOP。read_len 不得超过 buf_len。
 */
HAL_StatusTypeDef EEPROM_Read( uint8_t regAddr, uint16_t read_len, uint8_t *buf, uint16_t buf_len );

/*
 * 按 8 字节页切分后写。从页内偏移起算，本页剩余不足 buf_len 时
 * 先写完本页再写下一页，避免 24C02 页内回卷。写完一页后 ACK 查询。
 * regAddr + buf_len 不得超过 256。
 */
HAL_StatusTypeDef EEPROM_WritePage( uint8_t regAddr, const uint8_t *buf, uint16_t buf_len );


void EEPROM_Task( void *parametar );


#endif /* __EEPROM_H__ */
