#include "dht_driver.h"
#include "./SYSTEM/usart/usart.h"
#include "FreeRTOS.h"
#include "task.h"

static __attribute__((section(".ccmram"))) USART_HandleTypeDef husart6;
/* DMA 看不见 CCM。一笔：2 字节应答 + 5 字节数据。 */
static uint8_t dhtDataBuf[7];
static DMA_HandleTypeDef hdhtDma_rx;

#define  DHT_START_DELAY        ( 20u )
#define  DHT_FRAME_BYTES        ( 7u )
#define  DHT_RSP_BYTES          ( 2u )
#define  DHT_DMA_WAIT_MS        ( 15u )


static HAL_StatusTypeDef dht_dma_init( void ) {
    __HAL_RCC_DMA2_CLK_ENABLE();

    hdhtDma_rx.Instance = DMA2_Stream1;
    hdhtDma_rx.Init.Channel = DMA_CHANNEL_5;
    hdhtDma_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdhtDma_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    hdhtDma_rx.Init.FIFOThreshold = DMA_FIFO_THRESHOLD_HALFFULL;
    hdhtDma_rx.Init.Mode = DMA_NORMAL;
    hdhtDma_rx.Init.MemBurst = DMA_MBURST_SINGLE;
    hdhtDma_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdhtDma_rx.Init.PeriphBurst = DMA_PBURST_SINGLE;
    hdhtDma_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdhtDma_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdhtDma_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdhtDma_rx.Init.Priority = DMA_PRIORITY_MEDIUM;

    if ( HAL_DMA_Init( &hdhtDma_rx ) != HAL_OK ) {
        HX_UART_Printf( "[ERROR] DHT_DMA Init Error. Files:%s, Lines:%d\n", __FILE__, __LINE__ );
        return HAL_ERROR;
    }

    hdhtDma_rx.Instance->PAR = (uint32_t)( &husart6.Instance->DR );
    return HAL_OK;
}


static void dht_dma_stop( void ) {
    DMA_Stream_TypeDef *dma = hdhtDma_rx.Instance;

    husart6.Instance->CR3 &= ~USART_CR3_DMAR;
    dma->CR &= ~DMA_SxCR_EN_Msk;
    while ( dma->CR & DMA_SxCR_EN_Msk );
    DMA2->LIFCR = ( DMA_LIFCR_CTCIF1 | DMA_LIFCR_CHTIF1 | DMA_LIFCR_CTEIF1 |
                    DMA_LIFCR_CDMEIF1 | DMA_LIFCR_CFEIF1 );
}


/* 只装这一笔的 7 字节。上一笔已经停掉，CPU 不会和 DMA 同时碰这个缓冲。 */
static void dht_dma_arm( void ) {
    DMA_Stream_TypeDef *dma = hdhtDma_rx.Instance;
    volatile uint32_t tmp;

    dht_dma_stop();
    tmp = husart6.Instance->SR;
    tmp = husart6.Instance->DR;
    (void)tmp;

    dma->M0AR = (uint32_t)dhtDataBuf;
    dma->NDTR = DHT_FRAME_BYTES;
    dma->CR |= DMA_SxCR_EN_Msk;
    husart6.Instance->CR3 |= USART_CR3_DMAR;
}


static HAL_StatusTypeDef dht_dma_wait( void ) {
    uint8_t ms;

    for ( ms = 0u; ms < DHT_DMA_WAIT_MS; ms++ ) {
        if ( DMA2->LISR & DMA_LISR_TCIF1 )
            return HAL_OK;
        if ( DMA2->LISR & ( DMA_LISR_TEIF1 | DMA_LISR_DMEIF1 | DMA_LISR_FEIF1 ) )
            return HAL_ERROR;
        vTaskDelay( 1 );
    }
    return HAL_TIMEOUT;
}


HAL_StatusTypeDef DHT_Init( void ) {
    DHT_USART_DQ_PORT_EN();

    GPIO_InitTypeDef dhtio_initstructure;
    dhtio_initstructure.Alternate = GPIO_AF8_USART6;
    dhtio_initstructure.Mode = GPIO_MODE_AF_OD;  /* 开漏. 只听 */
    dhtio_initstructure.Pin = DHT_USART_DQ_PIN;
    dhtio_initstructure.Pull = GPIO_PULLUP;
    dhtio_initstructure.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init( DHT_USART_DQ_PORT, &dhtio_initstructure );

    DHT_USART_CLK_EN();
    husart6.Instance = DHT_USART_INSTANCE;
    husart6.Init.BaudRate = DHT_USART_BAUDRATE;
    husart6.Init.Mode = USART_MODE_RX;
    husart6.Init.Parity = USART_PARITY_NONE;
    husart6.Init.StopBits = USART_STOPBITS_1;
    husart6.Init.WordLength = USART_WORDLENGTH_8B;
    if ( HAL_USART_Init( &husart6 ) != HAL_OK ) {
        HX_UART_Printf( "[ERROR] DHT_USART Init Error. Files:%s, Lines:%d\n", __FILE__, __LINE__ );
        return HAL_ERROR;
    }
    if ( dht_dma_init() != HAL_OK )
        return HAL_ERROR;

    return HAL_OK;
}


HAL_StatusTypeDef DHT_Poll( float *humidity, float *temp ) {
    if ( humidity == NULL || temp == NULL ) 
        return HAL_ERROR;

    DHT_GPIO_SWITCH_OUT_MODE( DHT_USART_DQ_PORT, DHT_USART_DQ_PIN );
    HAL_GPIO_WritePin( DHT_USART_DQ_PORT, DHT_USART_DQ_PIN, GPIO_PIN_RESET );
    vTaskDelay( DHT_START_DELAY );
    HAL_GPIO_WritePin( DHT_USART_DQ_PORT, DHT_USART_DQ_PIN, GPIO_PIN_SET );

    /* 先武装 DMA 再切 AF，应答的两个沿落在 buf[0]、buf[1]。 */
    dht_dma_arm();
    DHT_GPIO_SWITCH_AF_MODE( DHT_USART_DQ_PORT, DHT_USART_DQ_PIN );

    if ( dht_dma_wait() != HAL_OK ) {
        dht_dma_stop();
        HX_UART_Printf( "[ERROR] DHT frame timeout. Files:%s, Lines:%d\n", __FILE__, __LINE__ );
        return HAL_TIMEOUT;
    }
    dht_dma_stop();

    /* buf[2..6] 才是这一笔的湿度、温度和校验。DMA 已停，不会再被下一笔盖掉。 */
    if ( (uint8_t)( dhtDataBuf[2] + dhtDataBuf[3] + dhtDataBuf[4] + dhtDataBuf[5] ) != dhtDataBuf[6] ) {
        HX_UART_Printf( "[ERROR] DHT checksum. Files:%s, Lines:%d\n", __FILE__, __LINE__ );
        return HAL_ERROR;
    }

    *humidity = (float)dhtDataBuf[2] + (float)dhtDataBuf[3] * 0.1f;
    *temp = (float)dhtDataBuf[4] + (float)dhtDataBuf[5] * 0.1f;
    return HAL_OK;
}




