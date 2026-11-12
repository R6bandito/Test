#include "eeprom.h"
#include "SYSTEM/iic/mi2c.h"
#include "SYSTEM/usart/usart.h"
#include "SYSTEM/delay/delay.h"


#define EEPROM_XFER_TIMEOUT_MS      ( 10u )
#define EEPROM_STOP_TIMEOUT_MS      ( 20u )
#define EEPROM_ACK_POLL_MAX         ( 20u )

static __attribute__((section(".ccmram"))) QueueHandle_t gEEPROM_EventQueue;
static __attribute__((section(".ccmram"))) SemaphoreHandle_t s_eeprom_done;


/* 事务还开着时收尾。地址 NACK 在 SendAddr 里只清了 AF，STOP 要由这里发。 */
static HAL_StatusTypeDef eeprom_stop( HAL_StatusTypeDef res ) {
    HAL_StatusTypeDef stop = MIIC_Stop( EEPROM_STOP_TIMEOUT_MS );
    if ( res != HAL_OK )
        return res;
    return stop;
}


static void eeprom_poll_delay( void ) {
    if ( xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED )
        vTaskDelay( 1 );
    else
        delay_ms( 1 );
}


/* STOP 之后等内部写周期结束。地址 NACK 是忙，不是硬故障。 */
static HAL_StatusTypeDef eeprom_wait_ready( void ) {
    uint32_t tries;
    HAL_StatusTypeDef res;

    for ( tries = 0u; tries < EEPROM_ACK_POLL_MAX; tries++ ) {
        res = MIIC_Start( EEPROM_XFER_TIMEOUT_MS );
        if ( res != HAL_OK )
            return eeprom_stop( res );

        res = MIIC_SendAddr( AT24C02_ADDR, 0u, EEPROM_XFER_TIMEOUT_MS );
        (void)eeprom_stop( res );
        if ( res == HAL_OK )
            return HAL_OK;

        eeprom_poll_delay();
    }

    HX_UART_Printf( "[ERROR] EEPROM write cycle timeout. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
    return HAL_TIMEOUT;
}


HAL_StatusTypeDef EEPROM_Init( void ) {
    /* 队列项是 EEPROM_Request *。按值拷贝的话，任务写的 status 在副本上，调用方看不见。 */
    gEEPROM_EventQueue = xQueueCreate( EEPROM_QUEUE_LENGTH, sizeof( EEPROM_Request * ) );
    if ( gEEPROM_EventQueue == NULL ) {
        HX_UART_Printf( "[ERROR] EEPROM_Task Queue create failed.Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        return HAL_ERROR;
    }

    s_eeprom_done = xSemaphoreCreateBinary();
    if ( s_eeprom_done == NULL ) {
        HX_UART_Printf( "[ERROR] EEPROM sync semaphore create failed. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        return HAL_ERROR;
    }
    return HAL_OK;
}


HAL_StatusTypeDef EEPROM_Submit( EEPROM_Request *req ) {
    if ( req == NULL || req->done == NULL || gEEPROM_EventQueue == NULL )
        return HAL_ERROR;

    req->status = HAL_ERROR;
    if ( xQueueSend( gEEPROM_EventQueue, &req, portMAX_DELAY ) != pdPASS )
        return HAL_ERROR;

    if ( xSemaphoreTake( req->done, portMAX_DELAY ) != pdPASS )
        return HAL_ERROR;
    return req->status;
}


static HAL_StatusTypeDef eeprom_submit_sync( EEPROM_Op op, uint8_t addr,
                                            uint16_t len, uint8_t *buf ) {
    EEPROM_Request req;

    if ( s_eeprom_done == NULL )
        return HAL_ERROR;

    req.op = op;
    req.addr = addr;
    req.len = len;
    req.buf = buf;
    req.buf_len = len;
    req.status = HAL_ERROR;
    req.done = s_eeprom_done;
    return EEPROM_Submit( &req );
}


/*
 * 一页内连续写，len 不得超过本页剩余。不检查用户区，自检可写最后一页。
 * 不放头文件。MIIC_Write 自己发 STOP。成功后 ACK 查询。
 */
HAL_StatusTypeDef eeprom_write_chunk( uint8_t regAddr, const uint8_t *buf, uint16_t len ) {
    HAL_StatusTypeDef res;

    res = MIIC_Start( EEPROM_XFER_TIMEOUT_MS );
    if ( res != HAL_OK ) {
        HX_UART_Printf( "[ERROR] BUS maybe locked. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        return eeprom_stop( res );
    }

    res = MIIC_SendAddr( AT24C02_ADDR, 0u, EEPROM_XFER_TIMEOUT_MS );
    if ( res != HAL_OK ) {
        HX_UART_Printf( "[ERROR] EEPROM no response. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        return eeprom_stop( res );
    }

    res = MIIC_SendByte( regAddr, EEPROM_XFER_TIMEOUT_MS );
    if ( res != HAL_OK ) {
        HX_UART_Printf( "[ERROR] EEPROM set addr failed. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        return eeprom_stop( res );
    }

    res = MIIC_Write( buf, len, EEPROM_XFER_TIMEOUT_MS );
    if ( res != HAL_OK ) {
        HX_UART_Printf( "[ERROR] EEPROM write failed. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        return res;
    }

    return eeprom_wait_ready();
}


static HAL_StatusTypeDef eeprom_probe_bus( void ) {
    HAL_StatusTypeDef res;

    res = MIIC_Start( EEPROM_XFER_TIMEOUT_MS );
    if ( res != HAL_OK ) {
        HX_UART_Printf( "[ERROR] BUS maybe locked. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        return eeprom_stop( res );
    }

    /* 写地址。NACK = 器件不在或正忙；成功则 ADDR 已清，可以直接 STOP。 */
    res = MIIC_SendAddr( AT24C02_ADDR, 0u, EEPROM_XFER_TIMEOUT_MS );
    if ( res != HAL_OK ) {
        HX_UART_Printf( "[ERROR] EEPROM no response. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        return eeprom_stop( res );
    }

    res = eeprom_stop( HAL_OK );
    if ( res != HAL_OK )
        HX_UART_Printf( "[ERROR] BUS maybe locked. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
    return res;
}


HAL_StatusTypeDef EEPROM_Probe( void ) {
    return eeprom_submit_sync( EEPROM_OP_PROBE, 0u, 0u, NULL );
}


/*
 * 随机读，不检查用户区。自检通过 EEPROM_Submit 投递，不放头文件。
 * 仍拒绝越过 256 字节芯片末尾。
 */
HAL_StatusTypeDef eeprom_read( uint8_t regAddr, uint16_t read_len, uint8_t *buf, uint16_t buf_len ) {
    HAL_StatusTypeDef res;

    if ( buf == NULL || buf_len == 0u || read_len == 0u )
        return HAL_ERROR;

    if ( read_len > buf_len ) {
        HX_UART_Printf( "[ERROR] Read length out of buf range. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        return HAL_ERROR;
    }

    if ( ( (uint16_t)regAddr + read_len ) > EEPROM_SIZE_BYTES ) {
        HX_UART_Printf( "[ERROR] Read out of EEPROM range. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        return HAL_ERROR;
    }

    res = MIIC_Start( EEPROM_XFER_TIMEOUT_MS );
    if ( res != HAL_OK ) {
        HX_UART_Printf( "[ERROR] BUS maybe locked. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        return eeprom_stop( res );
    }

    res = MIIC_SendAddr( AT24C02_ADDR, 0u, EEPROM_XFER_TIMEOUT_MS );
    if ( res != HAL_OK ) {
        HX_UART_Printf( "[ERROR] EEPROM no response. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        return eeprom_stop( res );
    }

    res = MIIC_SendByte( regAddr, EEPROM_XFER_TIMEOUT_MS );
    if ( res != HAL_OK ) {
        HX_UART_Printf( "[ERROR] EEPROM set addr failed. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        return eeprom_stop( res );
    }

    /* 重复起始：BUSY 此时为 1，Start 里面不会等它掉。 */
    res = MIIC_Start( EEPROM_XFER_TIMEOUT_MS );
    if ( res != HAL_OK ) {
        HX_UART_Printf( "[ERROR] BUS maybe locked. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        return eeprom_stop( res );
    }

    /* 读方向留下 ADDR，交给 MIIC_Read 在改 ACK 之后再清。 */
    res = MIIC_SendAddr( AT24C02_ADDR, 1u, EEPROM_XFER_TIMEOUT_MS );
    if ( res != HAL_OK ) {
        HX_UART_Printf( "[ERROR] EEPROM no response. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        return eeprom_stop( res );
    }

    /*
     * MIIC_Read 自己发 STOP 并等 BUSY 掉。失败时内部已经 Abort。
     * 这里再 Stop 会在空闲总线上多写一次 STOP。
     */
    res = MIIC_Read( buf, read_len, EEPROM_XFER_TIMEOUT_MS );
    if ( res != HAL_OK )
        HX_UART_Printf( "[ERROR] EEPROM read failed. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
    return res;
}


HAL_StatusTypeDef EEPROM_Read( uint8_t regAddr, uint16_t read_len, uint8_t *buf, uint16_t buf_len ) {
    /* AT24C02 最后一页 8 字节留给自检。用户态不允许读。 */
    if ( ( (uint16_t)regAddr + read_len ) > ( EEPROM_SIZE_BYTES - EEPROM_PAGE_VOL_BYTES ) ) {
        HX_UART_Printf( "[ERROR] Read action invalid. Check your param. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        return HAL_ERROR;
    }

    return eeprom_submit_sync( EEPROM_OP_READ, regAddr, read_len, buf );
}


static HAL_StatusTypeDef eeprom_write_pages( uint8_t regAddr, const uint8_t *buf, uint16_t buf_len ) {
    uint16_t left;
    uint16_t off;
    uint8_t addr;

    if ( buf == NULL || buf_len == 0u )
        return HAL_ERROR;
    if ( ( (uint16_t)regAddr + buf_len ) > EEPROM_SIZE_BYTES )
        return HAL_ERROR;

    left = buf_len;
    off = 0u;
    addr = regAddr;
    while ( left > 0u ) {
        uint16_t page_off = (uint16_t)( addr & ( EEPROM_PAGE_VOL_BYTES - 1u ) );
        uint16_t chunk = (uint16_t)( EEPROM_PAGE_VOL_BYTES - page_off );
        HAL_StatusTypeDef res;

        /* 当剩余数据小于一页剩余容量后，只写入剩余的数据量. */
        if ( chunk > left )
            chunk = left;

        res = eeprom_write_chunk( addr, buf + off, chunk );
        if ( res != HAL_OK )
            return res;

        addr = (uint8_t)( addr + chunk );
        off = (uint16_t)( off + chunk );
        left = (uint16_t)( left - chunk );
    }

    return HAL_OK;
}


HAL_StatusTypeDef EEPROM_WritePage( uint8_t regAddr, const uint8_t *buf, uint16_t buf_len ) {
    if ( buf == NULL || buf_len == 0u )
        return HAL_ERROR;

    /* 地址偏移 + 写入数据不允许大于芯片总容量(除开最后一页). */
    if ( ( (uint16_t)regAddr + buf_len ) > ( EEPROM_SIZE_BYTES - EEPROM_PAGE_VOL_BYTES ) ) {
        HX_UART_Printf( "[ERROR] Write out of EEPROM range. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        return HAL_ERROR;
    }

    return eeprom_submit_sync( EEPROM_OP_WRITE, regAddr, buf_len, (uint8_t *)buf );
}


void EEPROM_Task( void *parametar ) {
    (void)parametar;

    while ( 1 ) {
        EEPROM_Request *request = NULL;
        HAL_StatusTypeDef res = HAL_ERROR;

        if ( xQueueReceive( gEEPROM_EventQueue, &request, portMAX_DELAY ) != pdPASS )
            continue;
        if ( request == NULL )
            continue;

        switch ( request->op ) {
            case EEPROM_OP_PROBE:
                res = eeprom_probe_bus();
                break;

            case EEPROM_OP_READ:
                res = eeprom_read( request->addr, request->len, request->buf, request->buf_len );
                break;

            case EEPROM_OP_WRITE:
                res = eeprom_write_pages( request->addr, request->buf, request->len );
                break;

            default:
                res = HAL_ERROR;
                break;
        }

        /* 先写 status 再 Give。成功也要给，否则调用方一直停在 Take。 */
        request->status = res;
        if ( request->done != NULL )
            (void)xSemaphoreGive( request->done );
    }
}
