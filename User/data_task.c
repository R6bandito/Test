#include "data_task.h"
#include "./SYSTEM/dht/dht_driver.h"
#include "./SYSTEM/adc/adc_driver.h"
#include "./SYSTEM/usart/usart.h"
#include "FreeRTOS.h"
#include "task.h"

#define  DHT_SAMPLE_INTERVALS_MS        (1000u)
#define  TASK_CYCLE_MS                  (100u)
#define  DHT_SAMPLE_EVERY               (DHT_SAMPLE_INTERVALS_MS / TASK_CYCLE_MS)
#define  ADC_SAMPLE_INTERVALS_MS        (100u)
#define  ADC_SAMPLE_EVERY               (ADC_SAMPLE_INTERVALS_MS / TASK_CYCLE_MS)
#define  DHT_ERR_THRESHOLD              (4u)

static __attribute__((section(".ccmram"))) Snapshot_t s_snap;

void Data_GetSnapshot( Snapshot_t *out ) {
    if ( out == NULL )
        return;

    taskENTER_CRITICAL();
    *out = s_snap;
    taskEXIT_CRITICAL();
}

void Data_Task( void *parameter ) {
    (void)parameter;
    HAL_StatusTypeDef res;
    uint8_t dht_ready;
    uint8_t adc_ready;
    uint8_t dht_cycle = 0;
    uint8_t dht_err_cnt = 0;
    uint8_t adc_cycle = 0;
    uint8_t updated;
    Snapshot_t snap = { 0 };
    TickType_t wake;

    snap.dhtFlag = HAL_ERROR;
    snap.adcFlag = HAL_ERROR;
    s_snap = snap;

    res = DHT_Init();
    dht_ready = ( res == HAL_OK );
    if ( !dht_ready )
        HX_UART_Printf( "[ERROR] DHT uart init failed.\n" );

    res = HX_ADC_Init();
    adc_ready = ( res == HAL_OK );
    if ( !adc_ready )
        HX_UART_Printf( "[ERROR] ADC init failed.\n" );

    wake = xTaskGetTickCount();
    while ( 1 ) {
        vTaskDelayUntil( &wake, pdMS_TO_TICKS( TASK_CYCLE_MS ) );
        updated = 0;

        /* 各路只看自己的计数。同一拍都到期时先做 ADC，避免被 DHT 的起始低电平拖偏。 */
        if ( ++adc_cycle >= ADC_SAMPLE_EVERY ) {
            adc_cycle = 0;
            if ( adc_ready ) {
                res = HX_ADC_Poll( &snap.adc_val );
                if ( res == HAL_OK )
                    snap.adc_sample_tick = xTaskGetTickCount();
                snap.adcFlag = res;
                updated = 1;
            }
        }

        if ( ++dht_cycle >= DHT_SAMPLE_EVERY ) {
            dht_cycle = 0;
            if ( dht_ready ) {
                res = DHT_Poll( &snap.humidity, &snap.temp );
                if ( res == HAL_OK ) {
                    dht_err_cnt = 0;
                    snap.dht_sample_tick = xTaskGetTickCount();
                } else if ( ++dht_err_cnt > DHT_ERR_THRESHOLD ) {
                    /* TODO. 连续失败后再做总线静默或 USART 复位。 */
                    dht_err_cnt = 0;
                }
                snap.dhtFlag = res;
                updated = 1;
            }
        }

        if ( updated ) {
            taskENTER_CRITICAL();
            s_snap = snap;
            taskEXIT_CRITICAL();
        }
    }
}




