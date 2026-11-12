#include "console.h"
#include "./SYSTEM/usart/usart.h"
#include "data_task.h"
#include "eeprom.h"
#include <string.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"


static __attribute__((section(".ccmram"))) uint8_t s_rx_seg[UART_RCV_BUF_LEN];
static __attribute__((section(".ccmram"))) uint8_t s_tx_wire[HX_PROTO_MAX_RSP_FRAME_SIZE];

static __attribute__((section(".ccmram"))) HX_ConsoleDispatchEntry s_dispatch[HX_CONSOLE_DISPATCH_MAX];
static __attribute__((section(".ccmram"))) uint8_t s_dispatch_count;

__attribute__((section(".ccmram"))) SemaphoreHandle_t gConsoleCmd_Signal;
__attribute__((section(".ccmram"))) SemaphoreHandle_t gConsoleMutex;

static SemaphoreHandle_t s_eeprom_req_done;


static void console_fill_rsp_header( const HX_ProtoRequest *req, HX_ProtoResponse *rsp,
                                     uint8_t stat ) {
    /* 根据参数中提供的 stat 状态填充 Response 响应头. */
    rsp->class_id = req->class_id;
    rsp->cmd = req->cmd;
    rsp->seq = req->seq;
    rsp->stat = stat;
    rsp->data_len = 0u;
}


static HX_ProtoStat console_ping_handler( const HX_ProtoRequest *req, HX_ProtoResponse *rsp ) {
    if ( req == NULL || rsp == NULL )
        return HX_PROTO_STAT_INVALID;

    if ( req->args_len != 0u ) {
        console_fill_rsp_header( req, rsp, HX_PROTO_STAT_BAD_LEN );
        return HX_PROTO_STAT_BAD_LEN;
    }

    console_fill_rsp_header( req, rsp, HX_PROTO_STAT_OK );
    return HX_PROTO_STAT_OK;
}


static HAL_StatusTypeDef console_eeprom_post( EEPROM_Op op, uint8_t addr,
                                              uint16_t len, uint8_t *buf ) {
    EEPROM_Request request;

    if ( s_eeprom_req_done == NULL )
        return HAL_ERROR;

    request.op = op;
    request.addr = addr;
    request.len = len;
    request.buf = buf;
    request.buf_len = len;
    request.status = HAL_ERROR;
    request.done = s_eeprom_req_done;
    return EEPROM_Submit( &request );
}


static HX_ProtoStat console_i2c_ping_handler( const HX_ProtoRequest *req, HX_ProtoResponse *rsp ) {
    if ( req == NULL || rsp == NULL )
        return HX_PROTO_STAT_INVALID;

    if ( req->args_len != 0 ) {
        console_fill_rsp_header( req, rsp, HX_PROTO_STAT_BAD_LEN );
        return HX_PROTO_STAT_BAD_LEN;
    }

    HAL_StatusTypeDef res = console_eeprom_post( EEPROM_OP_PROBE, 0u, 0u, NULL );
    if ( res != HAL_OK ) {
        console_fill_rsp_header( req, rsp, HX_PROTO_STAT_INVALID );
        return HX_PROTO_STAT_INVALID;
    }

    console_fill_rsp_header( req, rsp, HX_PROTO_STAT_OK );
    return HX_PROTO_STAT_OK;
}


#define EEPROM_SELFTEST_ADDR        ( EEPROM_SIZE_BYTES - EEPROM_PAGE_VOL_BYTES )

static HX_ProtoStat console_i2c_test_handler( const HX_ProtoRequest *req, HX_ProtoResponse *rsp ) {
    uint8_t magic[EEPROM_PAGE_VOL_BYTES];
    uint8_t blank[EEPROM_PAGE_VOL_BYTES];
    uint8_t got[EEPROM_PAGE_VOL_BYTES];
    uint8_t i;
    uint8_t have_got;
    HAL_StatusTypeDef res;
    HX_ProtoStat stat;

    if ( req == NULL || rsp == NULL )
        return HX_PROTO_STAT_INVALID;

    if ( req->args_len != 0u ) {
        console_fill_rsp_header( req, rsp, HX_PROTO_STAT_BAD_LEN );
        return HX_PROTO_STAT_BAD_LEN;
    }

    for ( i = 0u; i < EEPROM_PAGE_VOL_BYTES; i += 4u ) {
        magic[i] = (uint8_t)( EEPROM_SELFTEST_MAGIC >> 24 );
        magic[i + 1u] = (uint8_t)( EEPROM_SELFTEST_MAGIC >> 16 );
        magic[i + 2u] = (uint8_t)( EEPROM_SELFTEST_MAGIC >> 8 );
        magic[i + 3u] = (uint8_t)EEPROM_SELFTEST_MAGIC;
        blank[i] = 0xFFu;
        blank[i + 1u] = 0xFFu;
        blank[i + 2u] = 0xFFu;
        blank[i + 3u] = 0xFFu;
    }

    stat = HX_PROTO_STAT_OK;
    have_got = 0u;
    res = console_eeprom_post( EEPROM_OP_WRITE, EEPROM_SELFTEST_ADDR,
                               EEPROM_PAGE_VOL_BYTES, magic );
    if ( res != HAL_OK ) {
        stat = HX_PROTO_STAT_INVALID;
    }
    else {
        res = console_eeprom_post( EEPROM_OP_READ, EEPROM_SELFTEST_ADDR,
                                   EEPROM_PAGE_VOL_BYTES, got );
        if ( res == HAL_OK )
            have_got = 1u;
        if ( res != HAL_OK || memcmp( got, magic, EEPROM_PAGE_VOL_BYTES ) != 0 )
            stat = HX_PROTO_STAT_INVALID;
    }

    /* 无论比对成败都擦掉最后一页，避免留下魔数。 */
    res = console_eeprom_post( EEPROM_OP_WRITE, EEPROM_SELFTEST_ADDR,
                               EEPROM_PAGE_VOL_BYTES, blank );
    if ( res != HAL_OK )
        stat = HX_PROTO_STAT_INVALID;

    console_fill_rsp_header( req, rsp, stat );
    /* BuildResponse 从 rsp->data 拷进 s_tx_wire。读成功才带这 8 字节，比对失败也带上，方便看读回了什么。 */
    if ( have_got ) {
        memcpy( rsp->data, got, EEPROM_PAGE_VOL_BYTES );
        rsp->data_len = EEPROM_PAGE_VOL_BYTES;
    }
    return stat;
}


static HX_ProtoStat console_sensor_stat( HAL_StatusTypeDef flag ) {
    if ( flag == HAL_OK )
        return HX_PROTO_STAT_OK;
    if ( flag == HAL_TIMEOUT )
        return HX_PROTO_STAT_TIMEOUT;
    return HX_PROTO_STAT_INVALID;
}


static void console_append( HX_ProtoResponse *rsp, const void *src, uint16_t len ) {
    memcpy( &rsp->data[rsp->data_len], src, len );
    rsp->data_len = (uint16_t)( rsp->data_len + len );
}


/* 失败不拷数值。快照里留下的是上一笔成功，不能当成这次的读数。 */
static void console_append_adc( HX_ProtoResponse *rsp, const Snapshot_t *snap ) {
    if ( snap->adcFlag != HAL_OK )
        return;
    console_append( rsp, &snap->adc_val, (uint16_t)sizeof( snap->adc_val ) );
}


static void console_append_dht( HX_ProtoResponse *rsp, const Snapshot_t *snap ) {
    if ( snap->dhtFlag != HAL_OK )
        return;
    console_append( rsp, &snap->humidity, (uint16_t)sizeof( snap->humidity ) );
    console_append( rsp, &snap->temp, (uint16_t)sizeof( snap->temp ) );
}


/*
 * CLASS_DATA / CMD_DATA_TEST 的响应布局，板子和上位机自行约定。
 * 请求 PAYLOAD 1 字节：0 两路，1 只 ADC，2 只 DHT。
 *
 * 只问一路：帧 STAT 就是这一路的结果（OK / TIMEOUT / INVALID）。
 *   OK 才有数据。ADC 是 uint16 小端；DHT 是湿度、温度两个 float32 小端。
 *   失败 data 为空。快照里留下的上一笔成功值不上线。
 *
 * 两路：帧 STAT 固定 OK，表示这次查询有效。数据区按 ADC、DHT 顺序，
 *   每路先 1 字节自己的结果码，只有 OK 才紧跟该路数据。
 */
static HX_ProtoStat console_data_check_handler( const HX_ProtoRequest *req, HX_ProtoResponse *rsp ) {
    Snapshot_t snap;
    HX_ProtoStat adc_st;
    HX_ProtoStat dht_st;

    if ( req == NULL || rsp == NULL )
        return HX_PROTO_STAT_INVALID;

    if ( req->args_len != 1u ) {
        console_fill_rsp_header( req, rsp, HX_PROTO_STAT_BAD_LEN );
        return HX_PROTO_STAT_BAD_LEN;
    }

    Data_GetSnapshot( &snap );
    adc_st = console_sensor_stat( snap.adcFlag );
    dht_st = console_sensor_stat( snap.dhtFlag );

    switch ( req->args[0] ) {
        case DATA_CMD_ARG_ADC:
            console_fill_rsp_header( req, rsp, adc_st );
            console_append_adc( rsp, &snap );
            return adc_st;

        case DATA_CMD_ARG_DHT:
            console_fill_rsp_header( req, rsp, dht_st );
            console_append_dht( rsp, &snap );
            return dht_st;

        case DATA_CMD_ARG_ALL:
            /* 帧 STAT 表示这次查询有效。每路先放自己的结果码，只有 OK 才跟数据。 */
            console_fill_rsp_header( req, rsp, HX_PROTO_STAT_OK );
            rsp->data[rsp->data_len++] = (uint8_t)adc_st;
            console_append_adc( rsp, &snap );
            rsp->data[rsp->data_len++] = (uint8_t)dht_st;
            console_append_dht( rsp, &snap );
            return HX_PROTO_STAT_OK;

        default:
            console_fill_rsp_header( req, rsp, HX_PROTO_STAT_BAD_CMD );
            return HX_PROTO_STAT_BAD_CMD;
    }
}


static void console_dispatch( const HX_ProtoRequest *req, HX_ProtoResponse *rsp ) {
    uint8_t i;

    /* 收到 Request 查命令表. 命中则调用功能回调. */
    for ( i = 0; i < s_dispatch_count; i++ ) {
        if ( s_dispatch[i].class_id == req->class_id && s_dispatch[i].cmd == req->cmd ) {
            (void)s_dispatch[i].handler( req, rsp );
            return;
        }
    }

    /* 未命中命令表. 将错误错误提示填充进 Response 响应后发回给上位机侧. */
    console_fill_rsp_header( req, rsp, HX_PROTO_STAT_BAD_CMD );
}


static void console_reply( const HX_ProtoResponse *rsp ) {
    uint16_t wire_len;
    HX_ProtoErr pe;

    /* 构造响应报文(Response). */
    pe = HX_Proto_BuildResponse( rsp, s_tx_wire, (uint16_t)sizeof( s_tx_wire ), &wire_len );
    if ( pe != HX_PROTO_OK )
        return;

    /* 将构造成功的响应报文发回给上位机侧. */
    (void)HX_UART_TxFrame( s_tx_wire, wire_len, UART_TX_WAIT_FOREVER );
}


static void console_process_segment( const uint8_t *seg, uint16_t seg_len ) {
    uint16_t pos = 0u;
    HX_ProtoRequest req;
    HX_ProtoResponse rsp;
    uint16_t sync;
    uint16_t consumed;
    HX_ProtoErr pe;

    while ( pos < seg_len ) {
        pe = HX_Proto_FindHeader( seg, seg_len, pos, &sync );
        if ( pe != HX_PROTO_OK )
            break;

        pe = HX_Proto_ParseRequest( seg, seg_len, sync, &req, &consumed );
        if ( pe == HX_PROTO_OK ) {
            memset( &rsp, 0, sizeof( rsp ) );
            console_dispatch( &req, &rsp );
            console_reply( &rsp );
            pos = sync + consumed;
            continue;
        }

        if ( pe == HX_PROTO_ERR_SHORT )
            break;

        pos = sync + 1u;
    }
}


HAL_StatusTypeDef HX_Console_RegisterHandler( uint8_t class_id, uint8_t cmd,
                                              HX_ConsoleCmdHandler handler ) {
    uint8_t i;

    if ( handler == NULL )
        return HAL_ERROR;

    /* 先按 (class_id, cmd) 查表，命中就覆盖 handler；没命中就尾插一条。 */
    for ( i = 0; i < s_dispatch_count; i++ ) {
        if ( s_dispatch[i].class_id == class_id && s_dispatch[i].cmd == cmd ) {
            s_dispatch[i].handler = handler;
            return HAL_OK;
        }
    }

    /* 最多只能插 HX_CONSOLE_DISPATCH_MAX 条. */
    if ( s_dispatch_count >= HX_CONSOLE_DISPATCH_MAX )
        return HAL_ERROR;

    s_dispatch[s_dispatch_count].class_id = class_id;
    s_dispatch[s_dispatch_count].cmd = cmd;
    s_dispatch[s_dispatch_count].handler = handler;
    s_dispatch_count++;
    return HAL_OK;
}


void HX_Console_Init( void ) {
    (void)HX_Console_RegisterHandler( HX_PROTO_CLASS_SYS, HX_PROTO_CMD_SYS_PING, console_ping_handler );
    (void)HX_Console_RegisterHandler( HX_PROTO_CLASS_I2C, HX_PROTO_CMD_I2C_PING, console_i2c_ping_handler );
    (void)HX_Console_RegisterHandler( HX_PROTO_CLASS_I2C, HX_PROTO_CMD_I2C_TEST, console_i2c_test_handler );
    (void)HX_Console_RegisterHandler( HX_PROTO_CLASS_DATA, HX_PROTO_CMD_DATA_TEST, console_data_check_handler );
    gConsoleCmd_Signal = xSemaphoreCreateBinary();
    if ( gConsoleCmd_Signal == NULL ) {
        /* Signal Create Error. */
        HX_UART_Printf( "[ERROR] Console signal create error. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        /* TODO */
    }

    s_eeprom_req_done = xSemaphoreCreateBinary();
    if ( s_eeprom_req_done == NULL ) {
        HX_UART_Printf( "[ERROR] EEPROM request done create error. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        /* TODO */
    }

    gConsoleMutex = xSemaphoreCreateMutex();
    if ( gConsoleMutex == NULL ) {
        HX_UART_Printf( "[ERROR] Console Mutex create error. Files: %s, Lines: %d\n", __FILE__, __LINE__ );
        /* TODO */
    }
}


void HX_Console_Poll( void ) {
    uint16_t seg_len;
    HAL_StatusTypeDef st;

    if ( !HX_UART_RxPending() )
        return;

    st = HX_UART_ReadSegment( s_rx_seg, (uint16_t)sizeof( s_rx_seg ), &seg_len );
    if ( st != HAL_OK )
        return;

    console_process_segment( s_rx_seg, seg_len );
}


void HX_DebugConsole_Task( void *arg ) {
    (void)arg;

    for ( ;; ) {
        if ( gConsoleCmd_Signal != NULL )
            (void)xSemaphoreTake( gConsoleCmd_Signal, portMAX_DELAY );

        while ( HX_UART_RxPending() )
            HX_Console_Poll();
    }
}
