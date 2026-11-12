#ifndef __HX_PROTO_H__
#define __HX_PROTO_H__

#include <stdint.h>
#include <stdbool.h>


/* 帧头 */
#define HX_PROTO_MAGIC0             ( 0xAAu )
#define HX_PROTO_MAGIC1             ( 0x55u )

/*
 * LEN：小端，表示 CLASS + CMD + SEQ + PAYLOAD 的字节数（不含 CRC、不含帧头与 LEN 自身）
 * CRC8：覆盖 LEN(2) ~ PAYLOAD 末字节；多项式 0x07，初值 0x00（见 proto.c）
 *
 * 一段 UART/DMA 缓冲内可连续多帧；请求解析为轻型视图，payload 指向该缓冲，不内嵌大数组。
 */
#define HX_PROTO_LEN_FIELD_SIZE     ( 2u )
#define HX_PROTO_FIXED_BODY_SIZE    ( 3u )   /* CLASS + CMD + SEQ（在 LEN 计数内，紧挨在 LEN 后） */
#define HX_PROTO_RSP_STAT_SIZE      ( 1u )   /* 响应 PAYLOAD 首字节 STAT，计入 LEN，不在 HEADER_SIZE 里 */
#define HX_PROTO_CRC_SIZE           ( 1u )

/*
 * 线上顺序：AA55(2) | LEN(2) | CLASS | CMD | SEQ | PAYLOAD | CRC8
 * HEADER_SIZE = 帧同步 + LEN + CLASS/CMD/SEQ，共 7 字节；其后才是 PAYLOAD。
 * 响应 PAYLOAD = STAT(1) + DATA(data_len)；请求 PAYLOAD 由上位机定义，无固定 STAT。
 */
#define HX_PROTO_HEADER_SIZE        ( 2u + HX_PROTO_LEN_FIELD_SIZE + HX_PROTO_FIXED_BODY_SIZE )

#define HX_PROTO_MAX_REQ_PAYLOAD    ( 240u )
#define HX_PROTO_MAX_RSP_DATA       ( 239u )

#define HX_PROTO_MAX_FRAME_SIZE     ( HX_PROTO_HEADER_SIZE + HX_PROTO_MAX_REQ_PAYLOAD + HX_PROTO_CRC_SIZE )
#define HX_PROTO_MAX_RSP_PAYLOAD    ( HX_PROTO_RSP_STAT_SIZE + HX_PROTO_MAX_RSP_DATA )
#define HX_PROTO_MAX_RSP_FRAME_SIZE ( HX_PROTO_HEADER_SIZE + HX_PROTO_MAX_RSP_PAYLOAD + HX_PROTO_CRC_SIZE )

/* 响应：wire 总长度 = 7 + 1 + data_len + 1 */
#define HX_PROTO_RSP_FRAME_SIZE( data_len ) \
    ( HX_PROTO_HEADER_SIZE + HX_PROTO_RSP_STAT_SIZE + (uint16_t)( data_len ) + HX_PROTO_CRC_SIZE )

/* 请求：wire 总长度 = 7 + payload_len + 1；LEN = 3 + payload_len（无 STAT） */
#define HX_PROTO_REQ_LEN_FIELD( payload_len ) \
    ( (uint16_t)HX_PROTO_FIXED_BODY_SIZE + (uint16_t)( payload_len ) )

#define HX_PROTO_REQ_FRAME_SIZE( payload_len ) \
    ( HX_PROTO_HEADER_SIZE + (uint16_t)( payload_len ) + HX_PROTO_CRC_SIZE )

/* LEN 字段值 = CLASS + CMD + SEQ + PAYLOAD = 3 + 1 + data_len */
#define HX_PROTO_RSP_LEN_FIELD( data_len ) \
    ( (uint16_t)HX_PROTO_FIXED_BODY_SIZE + HX_PROTO_RSP_STAT_SIZE + (uint16_t)( data_len ) )


typedef enum {
    HX_PROTO_CLASS_SYS = 0x01,
    HX_PROTO_CLASS_SPI = 0x02,
    HX_PROTO_CLASS_I2C = 0x03,
    HX_PROTO_CLASS_DATA = 0x04,
} HX_ProtoClass;


typedef enum {
    HX_PROTO_CMD_SYS_PING = 0x01,
} HX_ProtoCmdSys;


typedef enum {
    HX_PROTO_CMD_I2C_PING = 0x10u,
    HX_PROTO_CMD_I2C_TEST = 0x11u,
} HX_ProtoCmdI2c;


typedef enum {
    HX_PROTO_CMD_DATA_PING = 0x12u,
    HX_PROTO_CMD_DATA_TEST = 0x13u,
} HX_ProtoCmdDATA;


typedef enum {
    DATA_CMD_ARG_ALL = 0x00u,
    DATA_CMD_ARG_ADC = 0x01u,
    DATA_CMD_ARG_DHT = 0x02u,
} DataCmdArgs;


typedef enum {
    HX_PROTO_STAT_OK = 0x00,
    HX_PROTO_STAT_CRC_ERR = 0x01,
    HX_PROTO_STAT_BAD_LEN = 0x02,
    HX_PROTO_STAT_BAD_CMD = 0x03,
    HX_PROTO_STAT_TIMEOUT = 0x04,
    HX_PROTO_STAT_BUSY = 0x05,
    HX_PROTO_STAT_INVALID = 0x06,
} HX_ProtoStat;


typedef enum {
    HX_PROTO_OK = 0,
    HX_PROTO_ERR_ARG = -1,
    HX_PROTO_ERR_SYNC = -2,
    HX_PROTO_ERR_SHORT = -3,
    HX_PROTO_ERR_LEN = -4,
    HX_PROTO_ERR_CRC = -5,
    HX_PROTO_ERR_NO_ROOM = -6,
    HX_PROTO_ERR_NOT_IMPL = -7,
} HX_ProtoErr;


/* 上位机 → 板子：解析结果视图（不拷贝参数区） */
typedef struct {
    uint16_t        frame_len;      /* 线上一整帧 AA55..CRC */
    uint8_t         class_id;
    uint8_t         cmd;
    uint8_t         seq;
    uint16_t        args_len;       /* 请求 PAYLOAD 长度；PING 等为 0 */
    const uint8_t   *args;          /* 参数区：指向本段 RX 内 CLASS/CMD/SEQ 后的字节 */
} HX_ProtoRequest;


/* 板子 → 上位机：组帧用；PAYLOAD 首字节为 stat，其后为 data */
typedef struct {
    uint8_t         class_id;
    uint8_t         cmd;
    uint8_t         seq;
    uint8_t         stat;
    uint16_t        data_len;
    uint8_t         data[HX_PROTO_MAX_RSP_DATA];
} HX_ProtoResponse;


/* ---------- CRC8 ---------- */

uint8_t HX_Proto_Crc8( const uint8_t *data, uint16_t len );
bool HX_Proto_Crc8Check( const uint8_t *data, uint16_t len, uint8_t expect_crc );


/* ---------- 解析 / 组帧（实现后续补） ---------- */

/*
 * 在 buf[start .. len-1] 内找第一个 AA 55。
 * offset：帧头下标；找不到返回 HX_PROTO_ERR_SYNC。
 * 载荷里可能出现 AA 55，FindHeader 只认同步头，真伪由 ParseRequest 的 LEN/CRC 判定。
 */
HX_ProtoErr HX_Proto_FindHeader( const uint8_t *buf, uint16_t len, uint16_t start, uint16_t *offset );

/*
 * 从 buf[sync] 起解析一帧请求（sync 为 FindHeader 得到的 offset）。
 * 成功：out 填视图，*consumed = 整帧线长（AA55..CRC）；外层 pos = sync + consumed。
 * 失败：不保证 out；CRC/LEN 错等可 pos = sync + 1 再 FindHeader。
 */
HX_ProtoErr HX_Proto_ParseRequest( const uint8_t *buf, uint16_t len, uint16_t sync,
                                   HX_ProtoRequest *out, uint16_t *consumed );

/*
 * 由响应结构组线帧。wire 是调用方的发送缓冲（console 里是 s_tx_wire），
 * 数据从 in->data 按 in->data_len 拷进去。
 */
HX_ProtoErr HX_Proto_BuildResponse( const HX_ProtoResponse *in, uint8_t *wire, uint16_t wire_cap,
                                    uint16_t *wire_len );


#endif /* __HX_PROTO_H__ */
