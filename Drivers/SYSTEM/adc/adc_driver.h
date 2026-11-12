#ifndef __ADC_DRI_H__
#define __ADC_DRI_H__


#include "stm32f4xx_hal.h"


#define  ADC_USE_INSTANCE       (ADC1)
#define  ADC_GPIO_PORT          (GPIOA)
#define  ADC_GPIO_PIN           (GPIO_PIN_5)
#define  ADC_USE_CHANNEL        (ADC_CHANNEL_5)
#define  ADC_CLK_EN()           do { __HAL_RCC_ADC1_CLK_ENABLE(); } while(0)
#define  ADC_GPIO_CLK_EN()      do { __HAL_RCC_GPIOA_CLK_ENABLE(); } while(0) 


HAL_StatusTypeDef HX_ADC_Init( void );
HAL_StatusTypeDef HX_ADC_Poll( uint16_t *raw );




#endif /* __ADC_DRI_H__ */
