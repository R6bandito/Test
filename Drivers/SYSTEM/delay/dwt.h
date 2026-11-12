#ifndef __HX_DWT_H__
#define __HX_DWT_H__

#include "stm32f4xx_hal.h"

/*
 * CYCCNT 每加 1 是一个 HCLK，不是转完一圈。
 * 168MHz 时一拍约 5.95ns。计数器从 0 加到回绕才约 25.5s。
 */

void HX_DWT_Init( void );
uint32_t HX_DWT_GetCount( void );
uint32_t HX_DWT_Elapsed( uint32_t start );
void HX_DWT_DelayCount( uint32_t count );

#endif /* __HX_DWT_H__ */
