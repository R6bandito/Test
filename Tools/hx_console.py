#!/usr/bin/env python3
"""定时向 Debug Console 发请求，用来测板子。

线格式与板端 proto.c 一致：
  AA 55 | LEN(LE) | CLASS | CMD | SEQ | PAYLOAD | CRC8
CRC8：多项式 0x07，初值 0x00，覆盖 LEN 至 PAYLOAD 末字节。

依赖：pip install pyserial

示例：
  python Tools/hx_console.py COM5 115200 ping -n 1
  python Tools/hx_console.py COM5 115200 i2c-ping -n 1
  python Tools/hx_console.py COM5 115200 i2c-test -n 1
  python Tools/hx_console.py COM5 115200 data-adc -n 1
  python Tools/hx_console.py COM5 115200 data-dht -n 1
  python Tools/hx_console.py COM5 115200 data-all -n 1
"""

from __future__ import annotations

import argparse
import struct
import sys
import time

try:
    import serial
except ImportError:
    print("需要 pyserial：pip install pyserial", file=sys.stderr)
    sys.exit(1)


MAGIC = b"\xAA\x55"

# 与 proto.h 对齐
CLASS_SYS = 0x01
CLASS_I2C = 0x03
CLASS_DATA = 0x04
CMD_SYS_PING = 0x01
CMD_I2C_PING = 0x10
CMD_I2C_TEST = 0x11
CMD_DATA_TEST = 0x13
DATA_ARG_ALL = 0x00
DATA_ARG_ADC = 0x01
DATA_ARG_DHT = 0x02
# 自检读回应为这 8 字节（擦除发生在读回之后）
I2C_TEST_MAGIC = bytes([0xAF, 0xFA, 0xDE, 0xAD, 0xAF, 0xFA, 0xDE, 0xAD])

STAT_NAME = {
    0x00: "OK",
    0x01: "CRC_ERR",
    0x02: "BAD_LEN",
    0x03: "BAD_CMD",
    0x04: "TIMEOUT",
    0x05: "BUSY",
    0x06: "INVALID",
}

# 名字 -> (class, cmd, payload)
COMMANDS = {
    "ping": (CLASS_SYS, CMD_SYS_PING, b""),
    "ping-arg": (CLASS_SYS, CMD_SYS_PING, b"\x00"),  # 期望 STAT_BAD_LEN
    "badcmd": (CLASS_SYS, 0xFF, b""),  # 期望 STAT_BAD_CMD
    "i2c-ping": (CLASS_I2C, CMD_I2C_PING, b""),  # 期望 STAT_OK，无 data
    "i2c-test": (CLASS_I2C, CMD_I2C_TEST, b""),  # 期望 STAT_OK，data 为魔数
    # 只问一路：STAT 即该路结果，失败无 data。ADC 成功是 uint16 LE；DHT 成功是两个 float32 LE。
    # 两路：STAT 恒为 OK。每路先 1 字节结果码，OK 才紧跟该路数据。
    "data-adc": (CLASS_DATA, CMD_DATA_TEST, bytes([DATA_ARG_ADC])),
    "data-dht": (CLASS_DATA, CMD_DATA_TEST, bytes([DATA_ARG_DHT])),
    "data-all": (CLASS_DATA, CMD_DATA_TEST, bytes([DATA_ARG_ALL])),
}


def crc8(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def build_request(class_id: int, cmd: int, seq: int, payload: bytes = b"") -> bytes:
    len_field = 3 + len(payload)
    body = bytes(
        [
            len_field & 0xFF,
            (len_field >> 8) & 0xFF,
            class_id & 0xFF,
            cmd & 0xFF,
            seq & 0xFF,
        ]
    ) + payload
    return MAGIC + body + bytes([crc8(body)])


def hexdump(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data)


def try_parse_response(buf: bytes) -> tuple[int, dict | None]:
    """从 buf 里找第一帧完整响应。返回 (consumed, info 或 None)。"""
    idx = buf.find(MAGIC)
    if idx < 0:
        return (max(0, len(buf) - 1), None) if len(buf) > 1 else (0, None)

    rest = buf[idx:]
    if len(rest) < 7:
        return (idx, None)

    len_field = rest[2] | (rest[3] << 8)
    frame_len = 2 + 2 + len_field + 1
    if len(rest) < frame_len:
        return (idx, None)

    frame = rest[:frame_len]
    body = frame[2:-1]
    crc_ok = crc8(body) == frame[-1]
    payload = frame[7:-1]
    stat = payload[0] if payload else None
    data = payload[1:] if len(payload) > 1 else b""
    info = {
        "frame": frame,
        "class": frame[4],
        "cmd": frame[5],
        "seq": frame[6],
        "stat": stat,
        "data": data,
        "crc_ok": crc_ok,
    }
    return (idx + frame_len, info)


def _take_sensor(data: bytes, i: int, kind: str) -> tuple[int, str] | None:
    """读一路：1 字节结果码，OK 才跟数值。长度不够返回 None。"""
    if i >= len(data):
        return None
    st = data[i]
    i += 1
    name = STAT_NAME.get(st, "?")
    if st != 0x00:
        return (i, f"{kind}={name}")
    if kind == "ADC":
        if i + 2 > len(data):
            return None
        raw = struct.unpack_from("<H", data, i)[0]
        return (i + 2, f"ADC={raw}")
    if i + 8 > len(data):
        return None
    hum, temp = struct.unpack_from("<ff", data, i)
    return (i + 8, f"DHT={hum:.1f}% {temp:.1f}C")


def format_data_reading(req_arg: int, frame_stat: int | None, data: bytes) -> str:
    """按 §7.2 把数据区解成人能读的一行。对不上布局时标 LAYOUT_BAD。"""
    if frame_stat is None:
        return "  LAYOUT_BAD"
    if req_arg in (DATA_ARG_ADC, DATA_ARG_DHT):
        if frame_stat != 0x00:
            return "" if not data else "  LAYOUT_BAD"
        if req_arg == DATA_ARG_ADC:
            if len(data) != 2:
                return "  LAYOUT_BAD"
            raw = struct.unpack_from("<H", data)[0]
            return f"  ADC={raw}"
        if len(data) != 8:
            return "  LAYOUT_BAD"
        hum, temp = struct.unpack_from("<ff", data)
        return f"  DHT={hum:.1f}% {temp:.1f}C"

    if frame_stat != 0x00:
        return "" if not data else "  LAYOUT_BAD"
    adc = _take_sensor(data, 0, "ADC")
    if adc is None:
        return "  LAYOUT_BAD"
    i, adc_s = adc
    dht = _take_sensor(data, i, "DHT")
    if dht is None or dht[0] != len(data):
        return "  LAYOUT_BAD"
    return f"  {adc_s}  {dht[1]}"


def drain_and_print(ser: serial.Serial, wait_s: float, req_payload: bytes) -> bool:
    deadline = time.monotonic() + wait_s
    acc = bytearray()
    got = False
    while time.monotonic() < deadline:
        waiting = ser.in_waiting
        chunk = ser.read(waiting or 1)
        if chunk:
            got = True
            acc.extend(chunk)
            while True:
                consumed, info = try_parse_response(bytes(acc))
                if info is None:
                    if consumed:
                        acc = acc[consumed:]
                    break
                acc = acc[consumed:]
                stat = info["stat"]
                stat_s = STAT_NAME.get(stat, "?") if stat is not None else "—"
                crc_s = "CRC_OK" if info["crc_ok"] else "CRC_BAD"
                data = info["data"]
                data_s = hexdump(data) if data else "—"
                extra = ""
                if info["cmd"] == CMD_I2C_TEST and info["class"] == CLASS_I2C and data:
                    extra = "  MAGIC_OK" if data == I2C_TEST_MAGIC else "  MAGIC_BAD"
                elif (
                    info["class"] == CLASS_DATA
                    and info["cmd"] == CMD_DATA_TEST
                    and len(req_payload) == 1
                ):
                    extra = format_data_reading(req_payload[0], stat, data)
                print(
                    f"  RX {hexdump(info['frame'])}  "
                    f"cls={info['class']:02X} cmd={info['cmd']:02X} "
                    f"seq={info['seq']} STAT={stat_s}({stat}) {crc_s} "
                    f"data={data_s}{extra}"
                )
        else:
            time.sleep(0.01)

    leftover = bytes(acc)
    if leftover:
        text = leftover.decode("ascii", errors="replace").strip()
        if text.isprintable() or "\n" in text:
            print(f"  RX-ascii: {text}")
        else:
            print(f"  RX-raw: {hexdump(leftover)}")
    elif not got:
        print("  RX: 无数据")
    return got


def main() -> int:
    parser = argparse.ArgumentParser(description="定时发送 Debug Console 请求")
    parser.add_argument("port", help="串口，例如 COM5 / /dev/ttyUSB0")
    parser.add_argument("baud", type=int, help="波特率，例如 115200")
    parser.add_argument(
        "cmd",
        help="命令名：ping / i2c-ping / i2c-test / data-adc / data-dht / data-all，或 CLASS:CMD 如 04:13",
    )
    parser.add_argument("-i", "--interval", type=float, default=1.0, help="发送间隔秒，默认 1")
    parser.add_argument("-n", "--count", type=int, default=0, help="次数，0 表示一直发")
    parser.add_argument("--timeout", type=float, default=1.0, help="每次发送后等回包的秒数")
    args = parser.parse_args()

    payload = b""
    if args.cmd.lower() in COMMANDS:
        class_id, cmd, payload = COMMANDS[args.cmd.lower()]
    elif ":" in args.cmd:
        parts = args.cmd.split(":")
        if len(parts) != 2:
            print("CLASS:CMD 格式不对，例如 01:01", file=sys.stderr)
            return 2
        class_id = int(parts[0], 16)
        cmd = int(parts[1], 16)
    else:
        print(f"未知命令 {args.cmd}，可用：{', '.join(COMMANDS)} 或 CLASS:CMD", file=sys.stderr)
        return 2

    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = args.baud
    ser.timeout = 0.05
    # 默认 DTR/RTS 为高，CH340 打开串口会把板子复位，命令会在 USART 初始化前发出去。
    ser.dtr = False
    ser.rts = False
    try:
        ser.open()
    except serial.SerialException as exc:
        print(f"打不开 {args.port}：{exc}", file=sys.stderr)
        print("串口被占用。先关掉串口助手或其它已打开该口的程序，再重跑。", file=sys.stderr)
        return 1
    ser.dtr = False
    ser.rts = False
    time.sleep(0.5)
    print(f"open {args.port} {args.baud}  cmd={args.cmd}  interval={args.interval}s")
    seq = 1
    n = 0
    try:
        while args.count == 0 or n < args.count:
            frame = build_request(class_id, cmd, seq & 0xFF, payload)
            # 不要在 write 前 PurgeComm。Windows 上清除是异步的，
            # 板子回包很快时，这 9 字节会和残留一起被清掉。
            ser.write(frame)
            ser.flush()
            n += 1
            print(f"[{n}] TX {hexdump(frame)}  seq={seq & 0xFF}")
            drain_and_print(ser, args.timeout, payload)
            seq = (seq + 1) & 0xFF or 1
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\nstop")
    finally:
        ser.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
