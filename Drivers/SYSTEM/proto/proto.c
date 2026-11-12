#include "proto.h"
#include <string.h>


/* CRC-8，多项式 0x07，初值 0x00，不反射、不异或输出 */
uint8_t HX_Proto_Crc8( const uint8_t *data, uint16_t len ) {
    uint8_t crc = 0u;
    uint16_t i;
    uint8_t bit;

    if ( data == NULL )
        return 0u;

    for ( i = 0; i < len; i++ ) {
        crc ^= data[i];
        for ( bit = 0; bit < 8u; bit++ ) {
            if ( crc & 0x80u )
                crc = (uint8_t)( ( crc << 1 ) ^ 0x07u );
            else
                crc = (uint8_t)( crc << 1 );
        }
    }
    return crc;
}


bool HX_Proto_Crc8Check( const uint8_t *data, uint16_t len, uint8_t expect_crc ) {
    return HX_Proto_Crc8( data, len ) == expect_crc;
}


HX_ProtoErr HX_Proto_FindHeader( const uint8_t *buf, uint16_t len, uint16_t start, uint16_t *offset ) {
    uint16_t idx;

    if ( buf == NULL || offset == NULL )
        return HX_PROTO_ERR_ARG;
    if ( len < 2u || start >= len )
        return HX_PROTO_ERR_SYNC;

    for ( idx = start; idx + 1u < len; idx++ ) {
        if ( buf[idx] == HX_PROTO_MAGIC0 && buf[idx + 1u] == HX_PROTO_MAGIC1 ) {
            *offset = idx;
            return HX_PROTO_OK;
        }
    }

    return HX_PROTO_ERR_SYNC;
}


HX_ProtoErr HX_Proto_ParseRequest( const uint8_t *buf, uint16_t len, uint16_t sync,
                                   HX_ProtoRequest *out, uint16_t *consumed ) {
    uint16_t len_field;
    uint16_t payload_len;
    uint16_t frame_len;
    uint16_t crc_len;
    uint8_t crc_expect;
    uint8_t crc_calc;

    if ( buf == NULL || out == NULL || consumed == NULL )
        return HX_PROTO_ERR_ARG;
    if ( sync >= len )
        return HX_PROTO_ERR_SYNC;

    if ( buf[sync] != HX_PROTO_MAGIC0 || buf[sync + 1u] != HX_PROTO_MAGIC1 )
        return HX_PROTO_ERR_SYNC;

    /* 读本段 len 内是否放得下 LEN 字段 */
    if ( sync + 2u + HX_PROTO_LEN_FIELD_SIZE > len )
        return HX_PROTO_ERR_SHORT;

    len_field = (uint16_t)buf[sync + 2u] | ( (uint16_t)buf[sync + 3u] << 8 );
    if ( len_field < HX_PROTO_FIXED_BODY_SIZE )
        return HX_PROTO_ERR_LEN;
    if ( len_field > HX_PROTO_FIXED_BODY_SIZE + HX_PROTO_MAX_REQ_PAYLOAD )
        return HX_PROTO_ERR_LEN;

    /*
     * 整帧 = AA55(2) + LEN(2) + [CLASS|CMD|SEQ|参数…](len_field) + CRC(1)
     * 线上没有第二个「Payload 长度」字段；参数长度 = len_field - 3
     */
    frame_len = (uint16_t)( 2u + HX_PROTO_LEN_FIELD_SIZE + len_field + HX_PROTO_CRC_SIZE );
    if ( sync + frame_len > len )
        return HX_PROTO_ERR_SHORT;

    payload_len = (uint16_t)( len_field - HX_PROTO_FIXED_BODY_SIZE );

    crc_len = (uint16_t)( HX_PROTO_LEN_FIELD_SIZE + len_field );
    crc_expect = buf[sync + 2u + crc_len];
    crc_calc = HX_Proto_Crc8( &buf[sync + 2u], crc_len );
    if ( crc_calc != crc_expect )
        return HX_PROTO_ERR_CRC;

    out->frame_len = frame_len;
    out->class_id = buf[sync + 4u];
    out->cmd = buf[sync + 5u];
    out->seq = buf[sync + 6u];
    out->args_len = payload_len;
    out->args = ( payload_len > 0u ) ? &buf[sync + 7u] : NULL;

    *consumed = frame_len;
    return HX_PROTO_OK;
}


HX_ProtoErr HX_Proto_BuildResponse( const HX_ProtoResponse *in, uint8_t *wire, uint16_t wire_cap,
                                    uint16_t *wire_len ) {
    uint16_t frame_len;
    uint16_t len_field;
    uint16_t idx;
    uint8_t crc;

    if ( in == NULL || wire == NULL || wire_len == NULL )
        return HX_PROTO_ERR_ARG;

    if ( in->data_len > HX_PROTO_MAX_RSP_DATA )
        return HX_PROTO_ERR_LEN;

    frame_len = HX_PROTO_RSP_FRAME_SIZE( in->data_len );
    if ( wire_cap < frame_len )
        return HX_PROTO_ERR_NO_ROOM;

    len_field = HX_PROTO_RSP_LEN_FIELD( in->data_len );

    idx = 0u;
    wire[idx++] = HX_PROTO_MAGIC0;
    wire[idx++] = HX_PROTO_MAGIC1;
    wire[idx++] = (uint8_t)( len_field & 0xFFu );
    wire[idx++] = (uint8_t)( ( len_field >> 8 ) & 0xFFu );
    wire[idx++] = in->class_id;
    wire[idx++] = in->cmd;
    wire[idx++] = in->seq;
    wire[idx++] = in->stat;
    if ( in->data_len > 0u )
        memcpy( &wire[idx], in->data, in->data_len );
    idx = (uint16_t)( idx + in->data_len );

    crc = HX_Proto_Crc8( &wire[2], (uint16_t)( idx - 2u ) );
    wire[idx++] = crc;

    *wire_len = frame_len;
    return HX_PROTO_OK;
}
