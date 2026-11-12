#ifndef __HX_DELAY_H__
#define __HX_DELAY_H__

#include "stm32f4xx_hal.h"

void delay_init( uint32_t sysclk_mhz );
void delay_us( uint32_t nus );
void delay_ms( uint32_t nms );

#endif /* __HX_DELAY_H__ */
