#include "delay.h"
#include "dwt.h"

/* ************************************* */

/* 168MHz 下，计数器加 1 就是 1/168 us */
static uint32_t g_count_per_us = 168u;

/* 单次比较能表示的最大拍数，再加 1 就和 0 拍重合 */
#define DWT_DELAY_MAX_COUNT     ( 0xFFFFFFFFu )

void delay_init( uint32_t sysclk_mhz ) {
    if ( sysclk_mhz == 0u )
        sysclk_mhz = 168u;

    g_count_per_us = sysclk_mhz;
    HX_DWT_Init();
}

void delay_us( uint32_t nus ) {
    uint64_t count = (uint64_t)nus * (uint64_t)g_count_per_us;

    /*
     * 168MHz 下，0xFFFFFFFF 拍约 25.5s。
     * 更长的延时拆成多段，每段都不超过这个拍数。
     */
    while ( count > DWT_DELAY_MAX_COUNT ) {
        HX_DWT_DelayCount( DWT_DELAY_MAX_COUNT );
        count -= DWT_DELAY_MAX_COUNT;
    }

    HX_DWT_DelayCount( (uint32_t)count );
}

void delay_ms( uint32_t nms ) {
    /* 1000s 对应的微秒数仍放得进 uint32_t，避免 nms * 1000 溢出 */
    const uint32_t step_ms = 1000000u;

    while ( nms >= step_ms ) {
        delay_us( step_ms * 1000u );
        nms -= step_ms;
    }

    if ( nms != 0u )
        delay_us( nms * 1000u );
}
