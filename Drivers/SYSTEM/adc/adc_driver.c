#include "adc_driver.h"
#include "./SYSTEM/usart/usart.h"
#include "./SYSTEM/delay/dwt.h"


#define  ADC_POLL_TIMEOUT_MS        ( 1u )

static __attribute__((section(".ccmram"))) ADC_HandleTypeDef hadc1;
static __attribute__((section(".ccmram"))) uint16_t raw_ad_val;


static void adc_enable( void ) {
    ADC_TypeDef *adc = hadc1.Instance;

    if ( adc->CR2 & ADC_CR2_ADON )
        return;

    adc->CR2 |= ADC_CR2_ADON;
    /* 从掉电唤醒后要等 tSTAB，大约 3 µs，之后才能 SWSTART。 */
    HX_DWT_DelayCount( 3u * ( HAL_RCC_GetHCLKFreq() / 1000000u ) );
}

HAL_StatusTypeDef HX_ADC_Init( void ) {
    ADC_GPIO_CLK_EN();

    GPIO_InitTypeDef adc_initstructure;
    adc_initstructure.Mode = GPIO_MODE_ANALOG;
    adc_initstructure.Pin = ADC_GPIO_PIN;
    adc_initstructure.Pull = GPIO_NOPULL;
    HAL_GPIO_Init( ADC_GPIO_PORT, &adc_initstructure );

    ADC_CLK_EN();
    hadc1.Instance = ADC_USE_INSTANCE;
    hadc1.Init.ClockPrescaler = ADC_CLOCKPRESCALER_PCLK_DIV4;
    hadc1.Init.ContinuousConvMode = DISABLE;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.ScanConvMode = DISABLE;
    hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
    hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
    hadc1.Init.NbrOfConversion = 1;
    hadc1.Init.NbrOfDiscConversion = 0;
    hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc1.Init.DMAContinuousRequests = DISABLE;
    hadc1.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    hadc1.Init.Resolution = ADC_RESOLUTION_12B;

    if ( HAL_ADC_Init( &hadc1 ) != HAL_OK ) {
        HX_UART_Printf( "[ERROR] ADC init failed.\n" );
        return HAL_ERROR;
    }

    ADC_ChannelConfTypeDef ch = { 0 };
    ch.Channel = ADC_USE_CHANNEL;
    ch.Rank = 1;
    ch.SamplingTime = ADC_SAMPLETIME_480CYCLES;
    ch.Offset = 0;
    if ( HAL_ADC_ConfigChannel( &hadc1, &ch ) != HAL_OK ) {
        HX_UART_Printf( "[ERROR] ADC channel config failed.\n" );
        return HAL_ERROR;
    }

    adc_enable();
    return HAL_OK;
}


HAL_StatusTypeDef HX_ADC_Poll( uint16_t *raw ) {
    ADC_TypeDef *adc = hadc1.Instance;
    uint32_t start;
    uint32_t limit;

    if ( raw == NULL )
        return HAL_ERROR;

    adc->SR = ~( ADC_SR_EOC | ADC_SR_OVR );
    adc->CR2 |= ADC_CR2_SWSTART;

    start = HX_DWT_GetCount();
    limit = ADC_POLL_TIMEOUT_MS * ( HAL_RCC_GetHCLKFreq() / 1000u );
    while ( ( adc->SR & ADC_SR_EOC ) == 0u ) {
        if ( HX_DWT_Elapsed( start ) >= limit )
            return HAL_TIMEOUT;
    }

    raw_ad_val = (uint16_t)( adc->DR & 0x0FFFu );
    *raw = raw_ad_val;
    return HAL_OK;
}





