#include "mtim.h"


static __attribute__((section(".ccmram"))) TIM_HandleTypeDef htim7; /**< TIM7 handle */


void MTIM_Init( void ) {
    __HAL_RCC_TIM7_CLK_ENABLE();

    htim7.Instance = TIM7;
    htim7.Init.Prescaler = 84 - 1;
    htim7.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim7.Init.Period = 1000 - 1;
    htim7.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim7.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if ( HAL_TIM_Base_Init( &htim7 ) != HAL_OK ) {

    }

    HAL_NVIC_EnableIRQ( TIM7_IRQn );
    HAL_NVIC_SetPriority( TIM7_IRQn, 6, 0 );

    HAL_TIM_Base_Start_IT( &htim7 );
}


TIM_HandleTypeDef *GetMTIMHandle( void ) {
    return &htim7;
}


HAL_StatusTypeDef HAL_InitTick( uint32_t TickPriority ) {
    (void)TickPriority;
    MTIM_Init();
    uwTickPrio = TickPriority; /* !! */
    return HAL_OK;
}

