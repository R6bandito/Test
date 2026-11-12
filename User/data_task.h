#ifndef __DATA_TASK_H__
#define __DATA_TASK_H__


#include "stm32f4xx_hal.h"


typedef struct {
    float humidity;
    float temp;
    HAL_StatusTypeDef dhtFlag;
    uint16_t adc_val;
    HAL_StatusTypeDef adcFlag;
    uint32_t dht_sample_tick;
    uint32_t adc_sample_tick;
} Snapshot_t;

void Data_Task( void *parameter );
void Data_GetSnapshot( Snapshot_t *out );


#endif /* __DATA_TASK_H__ */
