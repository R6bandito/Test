#ifndef __MTIM_H
#define __MTIM_H

#include "stm32f4xx_hal.h"

void MTIM_Init( void );

TIM_HandleTypeDef *GetMTIMHandle( void ); 

#endif /* __MTIM_H */
