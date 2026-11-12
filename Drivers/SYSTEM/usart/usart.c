#include "usart.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "./SYSTEM/delay/delay.h"
#include "./SYSTEM/delay/dwt.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"


/* ******************************************************* */
static __attribute__((section(".ccmram"))) UART_HandleTypeDef huart1;
static DMA_HandleTypeDef hdma_uart1_rx;
static DMA_HandleTypeDef hdma_uart1_tx;

static uint8_t DMARcvBuf[UART_RCV_BUF_LEN];
static uint8_t DMARcvBuf_Back[UART_RCV_BUF_LEN];

static uint8_t g_uart_tx_buf[UART_TX_BUF_LEN];
static __attribute__((section(".ccmram"))) volatile uint8_t g_uart_tx_busy;
static __attribute__((section(".ccmram"))) uint16_t g_uart_tx_len;

static __attribute__((section(".ccmram"))) volatile uint8_t is_BufValiable = 1;
static __attribute__((section(".ccmram"))) volatile uint8_t is_BufBackValiable = 1;
static __attribute__((section(".ccmram"))) uint16_t gs_frame_length_buf1;
static __attribute__((section(".ccmram"))) uint16_t gs_frame_length_bufback;
static __attribute__((section(".ccmram"))) uint16_t gs_overflowCnt;
static __attribute__((section(".ccmram"))) char g_uart_print_buf[192];
/* ******************************************************* */

extern SemaphoreHandle_t gConsoleCmd_Signal;
extern SemaphoreHandle_t gConsoleMutex;


#pragma import(__use_no_semihosting)

struct __FILE {
    int handle;
};

FILE __stdout;


int fputc( int ch, FILE *f ) {
    uint8_t retryCnt = 5;
    while ( !(huart1.Instance->SR & USART_SR_TXE_Msk) && retryCnt ) {
        delay_ms( 1 );
        retryCnt--;
    }
    if ( retryCnt == 0 )
        return EOF;

    huart1.Instance->DR = (uint8_t)ch;
    return ch;
}


void _ttywrch( int ch ) {
    fputc( ch, &__stdout );
}


void _sys_exit( int x ) {
    x = x;
    while (1);
}


static void HX_UART_DMA_RX_Init( void ) {
    __HAL_RCC_DMA2_CLK_ENABLE();

    hdma_uart1_rx.Instance = DMA2_Stream2;
    hdma_uart1_rx.Init.Channel = DMA_CHANNEL_4;
    hdma_uart1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_uart1_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    hdma_uart1_rx.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_HALFFULL;
    hdma_uart1_rx.Init.MemBurst = DMA_MBURST_SINGLE;
    hdma_uart1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_uart1_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_uart1_rx.Init.Mode = DMA_NORMAL;
    hdma_uart1_rx.Init.PeriphBurst = DMA_PBURST_SINGLE;
    hdma_uart1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_uart1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_uart1_rx.Init.Priority = DMA_PRIORITY_MEDIUM;

    if ( HAL_DMA_Init( &hdma_uart1_rx ) != HAL_OK ) {

    }

    __HAL_LINKDMA( &huart1, hdmarx, hdma_uart1_rx );

    hdma_uart1_rx.Instance->M0AR = (uint32_t)DMARcvBuf;
    hdma_uart1_rx.Instance->PAR = (uint32_t)&( USE_UART_INSTANCE->DR );
    hdma_uart1_rx.Instance->NDTR = UART_RCV_BUF_LEN;
}


static void HX_UART_RxFlush( void ) {
    volatile uint32_t tmp;

    tmp = huart1.Instance->SR;
    tmp = huart1.Instance->DR;
    (void)tmp;
}


static void HX_UART_DMA_TX_Init( void ) {
    /*
     * USART1_TX: DMA2 Stream7, Channel 4
     * 在 HX_UART_TxFrame 内启动传输；此处仅预配置句柄
     */
    __HAL_RCC_DMA2_CLK_ENABLE();

    hdma_uart1_tx.Instance = DMA2_Stream7;
    hdma_uart1_tx.Init.Channel = DMA_CHANNEL_4;
    hdma_uart1_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_uart1_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    hdma_uart1_tx.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_HALFFULL;
    hdma_uart1_tx.Init.MemBurst = DMA_MBURST_SINGLE;
    hdma_uart1_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_uart1_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_uart1_tx.Init.Mode = DMA_NORMAL;
    hdma_uart1_tx.Init.PeriphBurst = DMA_PBURST_SINGLE;
    hdma_uart1_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_uart1_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_uart1_tx.Init.Priority = DMA_PRIORITY_MEDIUM;

    if ( HAL_DMA_Init( &hdma_uart1_tx ) != HAL_OK ) {

    }

    __HAL_LINKDMA( &huart1, hdmatx, hdma_uart1_tx );
}


static uint32_t HX_UART_TimeoutMsToCount( uint32_t timeout_ms ) {
    return timeout_ms * ( HAL_RCC_GetHCLKFreq() / 1000u );
}


static void HX_UART_TxDmaAbort( void ) {
    DMA_Stream_TypeDef *pDma = hdma_uart1_tx.Instance;

    pDma->CR &= ~DMA_SxCR_EN_Msk;
    while ( pDma->CR & DMA_SxCR_EN_Msk );

    huart1.Instance->CR3 &= ~USART_CR3_DMAT;
    DMA2->HIFCR = ( DMA_HIFCR_CTCIF7 | DMA_HIFCR_CHTIF7 | DMA_HIFCR_CTEIF7 |
                    DMA_HIFCR_CDMEIF7 | DMA_HIFCR_CFEIF7 );

    g_uart_tx_busy = 0u;
}


static HAL_StatusTypeDef HX_UART_WaitTc( uint32_t timeout_ms ) {
    uint32_t start = HX_DWT_GetCount();
    uint32_t limit = 0u;

    if ( timeout_ms != UART_TX_WAIT_FOREVER )
        limit = HX_UART_TimeoutMsToCount( timeout_ms );

    while ( !( huart1.Instance->SR & USART_SR_TC ) ) {
        if ( timeout_ms != UART_TX_WAIT_FOREVER && HX_DWT_Elapsed( start ) >= limit ) {
            HX_UART_TxDmaAbort();
            return HAL_TIMEOUT;
        }
        if ( xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED )
            vTaskDelay( 1 );
        else
            delay_ms( 1 );
    }

    HX_UART_TxDmaAbort();
    __HAL_UART_CLEAR_FLAG( &huart1, UART_FLAG_TC );
    return HAL_OK;
}


/* 调度器起来且 mutex 已创建才加锁。返回 1 表示本次 Take 成功，调用方必须配对 Unlock。 */
static uint8_t HX_UART_LockTx( void ) {
    if ( gConsoleMutex == NULL )
        return 0u;
    if ( xTaskGetSchedulerState() == taskSCHEDULER_NOT_STARTED )
        return 0u;
    xSemaphoreTake( gConsoleMutex, portMAX_DELAY );
    return 1u;
}


static void HX_UART_UnlockTx( uint8_t held ) {
    if ( held )
        xSemaphoreGive( gConsoleMutex );
}


void HX_UART_Init( uint32_t baudrate ) {
    GPIO_InitTypeDef uartgpio_initstructure;

    if ( baudrate == 0 )
        baudrate = 115200;

    USE_UART_GPIO_CLK_EN();

    uartgpio_initstructure.Alternate = GPIO_AF7_USART1;
    uartgpio_initstructure.Mode = GPIO_MODE_AF_PP;
    uartgpio_initstructure.Pin = USE_UART_TX_PIN | USE_UART_RX_PIN;
    uartgpio_initstructure.Pull = GPIO_PULLUP;
    uartgpio_initstructure.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init( USE_UART_GPIO_PORT, &uartgpio_initstructure );

    USE_UART_CLK_EN();

    memset( &huart1, 0, sizeof( huart1 ) );
    huart1.Instance = USE_UART_INSTANCE;
    huart1.Init.BaudRate = baudrate;
    huart1.Init.WordLength = UART_WORDLENGTH_8B;
    huart1.Init.StopBits = UART_STOPBITS_1;
    huart1.Init.Parity = UART_PARITY_NONE;
    huart1.Init.Mode = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    if ( HAL_UART_Init( &huart1 ) != HAL_OK ) {

    }

    HX_UART_DMA_RX_Init();
    HX_UART_DMA_TX_Init();
    HX_UART_RxFlush();

    huart1.Instance->CR3 |= USART_CR3_DMAR;
    hdma_uart1_rx.Instance->CR |= DMA_SxCR_EN;

    HAL_NVIC_SetPriority( USART1_IRQn, 5, 0 );
    HAL_NVIC_EnableIRQ( USART1_IRQn );
    __HAL_UART_ENABLE_IT( &huart1, UART_IT_IDLE );

    HX_UART_Printf( "[INFO] Debug Console Initialize OK.\n" );
}


void HX_UART_OnIdle( void ) {
    DMA_Stream_TypeDef *pDma = hdma_uart1_rx.Instance;
    uint16_t len;

    if ( !( huart1.Instance->SR & USART_SR_IDLE ) )
        return;

    __HAL_UART_CLEAR_IDLEFLAG( &huart1 );

    huart1.Instance->CR3 &= ~USART_CR3_DMAR;

    pDma->CR &= ~DMA_SxCR_EN_Msk;
    while ( pDma->CR & DMA_SxCR_EN_Msk );

    len = UART_RCV_BUF_LEN - (uint16_t)pDma->NDTR;
    if ( len > 0u ) {
        if ( pDma->M0AR == (uint32_t)DMARcvBuf && is_BufBackValiable ) {
            gs_frame_length_buf1 = len;
            is_BufValiable = 0u;
            pDma->M0AR = (uint32_t)DMARcvBuf_Back;
        }
        else if ( pDma->M0AR == (uint32_t)DMARcvBuf_Back && is_BufValiable ) {
            gs_frame_length_bufback = len;
            is_BufBackValiable = 0u;
            pDma->M0AR = (uint32_t)DMARcvBuf;
        }
        else {
            gs_overflowCnt++;
        }
    }

    pDma->NDTR = UART_RCV_BUF_LEN;
    DMA2->LIFCR = ( DMA_LIFCR_CTCIF2 | DMA_LIFCR_CHTIF2 | DMA_LIFCR_CTEIF2 |
                    DMA_LIFCR_CDMEIF2 | DMA_LIFCR_CFEIF2 );
    pDma->CR |= DMA_SxCR_EN_Msk;
    huart1.Instance->CR3 |= USART_CR3_DMAR;

    /* 向处理任务发出信号. */
    if ( gConsoleCmd_Signal != NULL ) {
        BaseType_t pxHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR( gConsoleCmd_Signal, &pxHigherPriorityTaskWoken );
        portYIELD_FROM_ISR( pxHigherPriorityTaskWoken );
    }
}


uint8_t HX_UART_RxPending( void ) {
    return ( !is_BufValiable || !is_BufBackValiable ) ? 1u : 0u;
}


HAL_StatusTypeDef HX_UART_ReadSegment( uint8_t *read_buf, uint16_t read_cap, uint16_t *seg_len ) {
    const uint8_t *src;
    uint16_t len;

    if ( read_buf == NULL || seg_len == NULL )
        return HAL_ERROR;

    if ( is_BufValiable && is_BufBackValiable )
        return HAL_BUSY;

    if ( !is_BufValiable ) {
        src = DMARcvBuf;
        len = gs_frame_length_buf1;
        if ( len == 0u ) {
            is_BufValiable = 1u;
            return HAL_BUSY;
        }
        if ( read_cap < len )
            return HAL_ERROR;
        memcpy( read_buf, src, len );
        *seg_len = len;
        is_BufValiable = 1u;
        gs_frame_length_buf1 = 0u;
        return HAL_OK;
    }

    if ( !is_BufBackValiable ) {
        src = DMARcvBuf_Back;
        len = gs_frame_length_bufback;
        if ( len == 0u ) {
            is_BufBackValiable = 1u;
            return HAL_BUSY;
        }
        if ( read_cap < len )
            return HAL_ERROR;
        memcpy( read_buf, src, len );
        *seg_len = len;
        is_BufBackValiable = 1u;
        gs_frame_length_bufback = 0u;
        return HAL_OK;
    }

    return HAL_BUSY;
}


uint16_t HX_UART_RxOverflowCount( void ) {
    return gs_overflowCnt;
}


HAL_StatusTypeDef HX_UART_TxFrame( const uint8_t *p, uint16_t len, uint32_t timeout_ms ) {
    HAL_StatusTypeDef st;
    uint8_t held;

    if ( p == NULL || len == 0u )
        return HAL_ERROR;
    if ( len > UART_TX_BUF_LEN )
        return HAL_ERROR;

    held = HX_UART_LockTx();

    if ( g_uart_tx_busy ) {
        st = HX_UART_WaitTc( timeout_ms );
        if ( st != HAL_OK ) {
            HX_UART_UnlockTx( held );
            return ( st == HAL_TIMEOUT ) ? HAL_BUSY : st;
        }
    }

    memcpy( g_uart_tx_buf, p, len );
    g_uart_tx_len = len;
    g_uart_tx_busy = 1u;

    hdma_uart1_tx.Instance->M0AR = (uint32_t)g_uart_tx_buf;
    hdma_uart1_tx.Instance->PAR = (uint32_t)&( USE_UART_INSTANCE->DR );
    hdma_uart1_tx.Instance->NDTR = g_uart_tx_len;

    __HAL_UART_CLEAR_FLAG( &huart1, UART_FLAG_TC );
    huart1.Instance->CR3 |= USART_CR3_DMAT;
    hdma_uart1_tx.Instance->CR |= DMA_SxCR_EN_Msk;

    st = HX_UART_WaitTc( timeout_ms );
    HX_UART_UnlockTx( held );
    return st;
}


int HX_UART_Printf( const char *fmt, ... ) {
    va_list ap;
    int n;
    uint16_t i;
    uint16_t len;
    uint8_t held;

    if ( fmt == NULL )
        return -1;

    held = HX_UART_LockTx();
    va_start( ap, fmt );
    n = vsnprintf( g_uart_print_buf, sizeof( g_uart_print_buf ), fmt, ap );
    va_end( ap );
    if ( n <= 0 ) {
        HX_UART_UnlockTx( held );
        return n;
    }

    len = ( n >= (int)sizeof( g_uart_print_buf ) )
          ? (uint16_t)( sizeof( g_uart_print_buf ) - 1u )
          : (uint16_t)n;
    for ( i = 0u; i < len; i++ ) {
        while ( !( huart1.Instance->SR & USART_SR_TXE ) );
        huart1.Instance->DR = (uint8_t)g_uart_print_buf[i];
    }
    while ( !( huart1.Instance->SR & USART_SR_TC ) );

    HX_UART_UnlockTx( held );
    return n;
}


uint8_t HX_UART_TxBusy( void ) {
    return g_uart_tx_busy;
}


uint8_t *HX_UART_TxBuf( void ) {
    return g_uart_tx_buf;
}


UART_HandleTypeDef *GetConsoleUartHandle( void ) {
    return &huart1;
}


DMA_HandleTypeDef *GetConsoleDMARxHandle( void ) {
    return &hdma_uart1_rx;
}


DMA_HandleTypeDef *GetConsoleDMATxHandle( void ) {
    return &hdma_uart1_tx;
}
