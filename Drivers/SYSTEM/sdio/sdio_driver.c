#include "sdio_driver.h"
#include "SYSTEM/delay/delay.h"
#include "SYSTEM/delay/dwt.h"


#define SDIO_CMD_FLAGS_CLEAR    ( SDIO_ICR_CMDSENTC | SDIO_ICR_CMDRENDC | \
                                  SDIO_ICR_CTIMEOUTC | SDIO_ICR_CCRCFAILC )
#define SD_R1_ERROR_MASK        ( 0xFDFFE008u )
#define SD_R1_APP_CMD           ( 1u << 5 )
#define SD_R1_READY_FOR_DATA    ( 1u << 8 )
#define SD_R1_STATE_MASK        ( 0xFu << 9 )
#define SD_R1_STATE_TRANSFER    ( 4u << 9 )
#define SD_R6_ERROR_MASK        ( 0xE000u )
#define SD_OCR_READY            ( 1u << 31 )
#define SD_ACMD41_HCS           ( 1u << 30 )
#define SD_OCR_VOLTAGE_3V3      ( ( 1u << 20 ) | ( 1u << 21 ) )

typedef enum {
    SD_RESP_NONE,
    SD_RESP_SHORT,
    SD_RESP_LONG,
    SD_RESP_R3,
} SD_Response;

static HAL_StatusTypeDef sdio_send_cmd( uint32_t index, uint32_t arg,
                                       SD_Response response, uint32_t start,
                                       uint32_t limit ) {
    uint32_t wait_resp = 0u;
    uint32_t done = SDIO_STA_CMDSENT;
    uint32_t sta;

    if ( response != SD_RESP_NONE ) {
        wait_resp = ( response == SD_RESP_LONG )
                    ? SDIO_CMD_WAITRESP_Msk : SDIO_CMD_WAITRESP_0;
        done = SDIO_STA_CMDREND;
    }

    if ( HX_DWT_Elapsed( start ) >= limit )
        return HAL_TIMEOUT;

    SDIO->ICR = SDIO_CMD_FLAGS_CLEAR;
    SDIO->ARG = arg;
    SDIO->CMD = ( index << SDIO_CMD_CMDINDEX_Pos ) | wait_resp | SDIO_CMD_CPSMEN_Msk;

    do {
        sta = SDIO->STA;
        if ( sta & ( done | SDIO_STA_CTIMEOUT | SDIO_STA_CCRCFAIL ) )
            break;

        if ( HX_DWT_Elapsed( start ) >= limit ) {
            SDIO->CMD = 0u;  /* 软件超时，停止 CPSM。 */
            SDIO->ICR = SDIO_CMD_FLAGS_CLEAR;
            return HAL_TIMEOUT;
        }
    } while ( 1 );

    SDIO->ICR = SDIO_CMD_FLAGS_CLEAR;
    if ( sta & SDIO_STA_CTIMEOUT )
        return HAL_TIMEOUT;

    /* R3 无响应 CRC，F407 可能以 CCRCFAIL 报告接收完成。 */
    if ( ( sta & SDIO_STA_CCRCFAIL ) && response != SD_RESP_R3 )
        return HAL_ERROR;

    if ( response == SD_RESP_SHORT &&
         ( SDIO->RESPCMD & SDIO_RESPCMD_RESPCMD_Msk ) != index )
        return HAL_ERROR;

    return HAL_OK;
}

static void sdio_read_long_response( uint32_t words[4] ) {
    words[0] = SDIO->RESP1;
    words[1] = SDIO->RESP2;
    words[2] = SDIO->RESP3;
    words[3] = SDIO->RESP4;
}


/* 内部接口沿用调用流程的 DWT 起点和周期预算，不重新计时。
 * 每轮必须收到新的 CMD13 响应；不信任 card->state 的旧快照。
 * 只有卡尚未就绪才重查，通信错误或卡错误立即交给上层处理。
 * 调用者须独占控制器。超时后 state=INVALID，不能当作就绪使用。 */
static HAL_StatusTypeDef sdio_wait_transfer_ready( SDCard *card, uint32_t start,
                                                  uint32_t limit ) {
    uint32_t r1;
    HAL_StatusTypeDef res;
    if ( card == NULL )
        return HAL_ERROR;

    card->state = CARD_STATE_INVALID;
    if ( limit == 0u || card->rca == 0u )
        return HAL_ERROR;

    do {
        if ( HX_DWT_Elapsed( start ) >= limit )
            return HAL_TIMEOUT;

        if ( SDIO->STA & SDIO_STA_CMDACT )
            return HAL_BUSY;

        res = sdio_send_cmd( 13u, (uint32_t)card->rca << 16, SD_RESP_SHORT, start, limit );
        if ( res != HAL_OK )
            return res;

        r1 = SDIO->RESP1;
        if ( r1 & SD_R1_ERROR_MASK )
            return HAL_ERROR;
        if ( ( ( r1 & SD_R1_STATE_MASK ) >> 9 ) > 8u )
            return HAL_ERROR; /* SD 存储卡保留状态编码。 */

        /* 响应返回时也检查总预算，不能在截止后报告成功。 */
        if ( HX_DWT_Elapsed( start ) >= limit )
            return HAL_TIMEOUT;

        if ( ( r1 & ( SD_R1_STATE_MASK | SD_R1_READY_FOR_DATA ) ) ==
             ( SD_R1_STATE_TRANSFER | SD_R1_READY_FOR_DATA ) ) {
            card->state = CARD_STATE_TRANSFER;
            return HAL_OK;
        }
    } while( 1 );
}


HAL_StatusTypeDef SDIO_CardConnection( SDCard *card, uint32_t timeout_ms ) {
    const uint32_t cmd8_arg = 0x1AAu;
    const uint64_t cycles = ( (uint64_t)SystemCoreClock * timeout_ms ) / 1000u;
    HAL_StatusTypeDef status;

    if ( cycles == 0u || cycles > UINT32_MAX || card == NULL )
        return HAL_ERROR;

    /* CMD0 会使原 RCA/身份快照失效，重连失败也不能留下旧的 TRANSFER。 */
    *card = (SDCard){ 0 };
    card->speed = CARD_SPEED_DEFAULT;

    status = sdio_send_cmd( 0u, 0u, SD_RESP_NONE, HX_DWT_GetCount(), (uint32_t)cycles );
    if ( status != HAL_OK )
        return status;

    status = sdio_send_cmd( 8u, cmd8_arg, SD_RESP_SHORT, HX_DWT_GetCount(), (uint32_t)cycles );
    if ( status != HAL_OK )
        return status;

    if ( ( SDIO->RESP1 & 0xFFFu ) != cmd8_arg )
        return HAL_ERROR;

    card->state = CARD_STATE_NOT_READY;
    return HAL_OK;
}

HAL_StatusTypeDef SDIO_TransferUP( SDCard *card, uint32_t timeout_ms ) {
    const uint64_t cycles = ( (uint64_t)SystemCoreClock * timeout_ms ) / 1000u;
    const uint32_t clkcr = SDIO->CLKCR;
    SDCard detected = { 0 };
    uint32_t start;
    uint32_t limit;
    uint32_t rca_arg;
    uint32_t response;
    HAL_StatusTypeDef status;

    if ( card == NULL || cycles == 0u || cycles > UINT32_MAX )
        return HAL_ERROR;

    /* 48 MHz 输入下，CLKDIV >= 119 才低于 400 kHz。 */
    if ( ( clkcr & ( SDIO_CLKCR_CLKEN_Msk | SDIO_CLKCR_BYPASS_Msk |
                     SDIO_CLKCR_WIDBUS_Msk ) ) != SDIO_CLKCR_CLKEN_Msk ||
         ( clkcr & SDIO_CLKCR_CLKDIV_Msk ) < 119u )
        return HAL_ERROR;

    limit = (uint32_t)cycles;
    start = HX_DWT_GetCount();
    card->state = CARD_STATE_INVALID;
    card->rca = 0u;

    do {
        status = sdio_send_cmd( 55u, 0u, SD_RESP_SHORT, start, limit );
        if ( status != HAL_OK )
            return status;

        response = SDIO->RESP1;
        if ( ( response & SD_R1_ERROR_MASK ) || !( response & SD_R1_APP_CMD ) )
            return HAL_ERROR;

        status = sdio_send_cmd( 41u, SD_ACMD41_HCS | SD_OCR_VOLTAGE_3V3,
                                SD_RESP_R3, start, limit );
        if ( status != HAL_OK )
            return status;

        detected.ocr = SDIO->RESP1;
    } while ( !( detected.ocr & SD_OCR_READY ) );

    status = sdio_send_cmd( 2u, 0u, SD_RESP_LONG, start, limit );
    if ( status != HAL_OK )
        return status;
    sdio_read_long_response( detected.cid );

    /* RCA 0 留给 CMD7 取消选卡。卡报 0 时重新请求。 */
    do {
        status = sdio_send_cmd( 3u, 0u, SD_RESP_SHORT, start, limit );
        if ( status != HAL_OK )
            return status;

        response = SDIO->RESP1;
        if ( response & SD_R6_ERROR_MASK )
            return HAL_ERROR;

        detected.rca = (uint16_t)( response >> 16 );
    } while ( detected.rca == 0u );
    rca_arg = (uint32_t)detected.rca << 16;

    status = sdio_send_cmd( 9u, rca_arg, SD_RESP_LONG, start, limit );
    if ( status != HAL_OK )
        return status;
    sdio_read_long_response( detected.csd );

    status = sdio_send_cmd( 7u, rca_arg, SD_RESP_SHORT, start, limit );
    if ( status != HAL_OK )
        return status;
    if ( SDIO->RESP1 & SD_R1_ERROR_MASK )
        return HAL_ERROR;

    /* CMD7 是 R1b。用 CMD13 确认卡进入 TRAN 且可接收数据。 */
    status = sdio_wait_transfer_ready( &detected, start, limit );
    if ( status != HAL_OK )
        return status;

    detected.speed = CARD_SPEED_DEFAULT;
    detected.state = CARD_STATE_TRANSFER;
    *card = detected;
    return HAL_OK;
}


HAL_StatusTypeDef SDIO_GetCardState( SDCard *card, uint32_t timeout_ms ) {
    const uint64_t cycles = ( (uint64_t)SystemCoreClock * timeout_ms ) / 1000u;
    uint32_t start;
    uint32_t r1;
    HAL_StatusTypeDef res;
    if ( card == NULL )
        return HAL_ERROR;

    card->state = CARD_STATE_INVALID;
    if ( cycles == 0u || cycles > UINT32_MAX || card->rca == 0u )
        return HAL_ERROR;

    /* 不覆盖仍在进行的命令；此检查不能代替任务间互斥。 */
    if ( SDIO->STA & SDIO_STA_CMDACT )
        return HAL_BUSY;

    /* 发送命令CMD13. */
    start = HX_DWT_GetCount();
    res = sdio_send_cmd( 13u, (uint32_t)card->rca << 16,
                         SD_RESP_SHORT, start, (uint32_t)cycles );
    if ( res != HAL_OK )
        return res;

    r1 = SDIO->RESP1;
    if ( r1 & SD_R1_ERROR_MASK )
        return HAL_ERROR;

    switch ( ( r1 & SD_R1_STATE_MASK ) >> 9 )
    {
        case 5:
            card->state = CARD_STATE_SENDING_DATA;
            break;

        case 6:
            card->state = CARD_STATE_RCVING_DATA;
            break;

        case 7:
            card->state = CARD_STATE_PROGRAMING;
            break;

        case 4:
            card->state = ( r1 & SD_R1_READY_FOR_DATA )
                          ? CARD_STATE_TRANSFER : CARD_STATE_NOT_READY;
            break;

        case 0: /* IDLE */
        case 1: /* READY */
        case 2: /* IDENT */
        case 3: /* STBY */
        case 8: /* DIS */
            card->state = CARD_STATE_NOT_READY;
            break;

        default:  return HAL_ERROR; /* 保留编码，不当成有效存储卡状态。 */
    }

    return HAL_OK;
}


void SDIO_HostInit( void ) {
    SDIO_GPIO_DAT_PORT_EN();
    SDIO_GPIO_CMD_PORT_EN();
    SDIO_GPIO_CLK_PORT_EN();

    /* DATA. */
    GPIO_InitTypeDef init_structure;
    init_structure.Alternate = GPIO_AF12_SDIO;
    init_structure.Mode = GPIO_MODE_AF_PP;
    init_structure.Pin = SDIO_GPIO_DAT_0 | SDIO_GPIO_DAT_1 | SDIO_GPIO_DAT_2 | SDIO_GPIO_DAT_3;
    init_structure.Pull = GPIO_PULLUP;
    init_structure.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init( SDIO_GPIO_DAT_PORT, &init_structure );

    /* CMD. */
    init_structure.Pin = SDIO_GPIO_CMD;
    HAL_GPIO_Init( SDIO_GPIO_CMD_PORT, &init_structure );

    /* CLK. */
    init_structure.Pin = SDIO_GPIO_CLK;
    init_structure.Pull = GPIO_NOPULL;
    HAL_GPIO_Init( SDIO_GPIO_CLK_PORT, &init_structure );

    __HAL_RCC_SDIO_CLK_ENABLE();

    /* 使能 SDIO 控制器，卡槽供电由板级电路决定。 */
    SDIO->POWER = SDIO_POWER_PWRCTRL_Msk;
    delay_us( 50u );

    /* 48 MHz / (119 + 2) ≈ 396.7 kHz。1 位、上升沿、关闭硬件流控。 */
    SDIO->CLKCR = (119u << SDIO_CLKCR_CLKDIV_Pos) | SDIO_CLKCR_CLKEN_Msk;

    /* 卡供电稳定后，CMD0 前保持 CMD 为高并提供至少 74 个时钟。 */
    delay_us( 1000u );
}




