#include "dwt.h"

/* ************************************* */

void HX_DWT_Init( void ) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    DWT->CYCCNT = 0u;
}

uint32_t HX_DWT_GetCount( void ) {
    return DWT->CYCCNT;
}

uint32_t HX_DWT_Elapsed( uint32_t start ) {
    return DWT->CYCCNT - start;
}

void HX_DWT_DelayCount( uint32_t count ) {
    uint32_t start;

    if ( count == 0u )
        return;

    /*
     * count 是要过去的拍数，不是转一圈。
     * 用无符号减法：CYCCNT 从 0xFFFFFFFF 回到 0 时，差值仍然连续。
     * 不要写成 GetCount() < start + count，加法会先溢出。
     * 单次最多等 0xFFFFFFFF 拍；再多一拍和 0 拍无法区分。
     */
    start = HX_DWT_GetCount();
    while ( HX_DWT_Elapsed( start ) < count ) {
    }
}
