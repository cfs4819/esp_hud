#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
上位机：通过串口(USB CDC / ttyACM / COM)向下位机发送两类帧
- MSGF：高频短消息（建议 24Hz）
- IMGF：低频 PNG（<=100KB）

帧格式（固定 20 bytes header，小端）：
magic u32   // 'MSGF' 'IMGF'
type  u8
flags u8
rsv  u16
len  u32
crc32 u32   // 本脚本默认不启用（0）
seq  u32

短消息 MSGF payload（小端）：
int16  speed_kmh
int16  engine_speed_rpm
int32  odo_m
int32  trip_odo_m
int16  outside_temp_c
int16  inside_temp_c
int16  battery_mv
uint16 curr_time_min   // 0..1439（HH:MM -> 分钟数）
uint16 trip_time_min   // 行程分钟数
... reserve(可选)

依赖：
pip install pyserial
"""

import argparse
import struct
import time
from dataclasses import dataclass
from typing import Optional

import serial


MAGIC_MSGF = b"MSGF"
MAGIC_IMGF = b"IMGF"


def u32_le_from_magic(magic4: bytes) -> int:
    if len(magic4) != 4:
        raise ValueError("magic must be 4 bytes")
    return struct.unpack("<I", magic4)[0]


def pack_header(magic4: bytes, payload_len: int, seq: int, typ: int = 0, flags: int = 0, crc32: int = 0) -> bytes:
    # <I B B H I I I  = 4 +1+1+2 +4+4+4 = 20 bytes
    return struct.pack("<IBBHIII", u32_le_from_magic(magic4), typ & 0xFF, flags & 0xFF, 0, payload_len, crc32 & 0xFFFFFFFF, seq & 0xFFFFFFFF)


def hhmm_to_minutes(hhmm: str) -> int:
    # "HH:MM" -> minutes
    hhmm = hhmm.strip()
    hh, mm = hhmm.split(":")
    h = int(hh)
    m = int(mm)
    if not (0 <= h <= 23 and 0 <= m <= 59):
        raise ValueError("time must be HH:MM within 00:00..23:59")
    return h * 60 + m


@dataclass
class MsgfSnapshot:
    speed_kmh: int = 0
    engine_rpm: int = 0
    odo_m: int = 0
    trip_odo_m: int = 0
    outside_temp_c: int = 0
    inside_temp_c: int = 0
    battery_mv: int = 12000
    curr_time_min: int = 0       # 0..1439
    trip_time_min: int = 0       # >=0

    def pack(self) -> bytes:
        # < h h i i h h h H H  = 2+2+4+4+2+2+2+2+2 = 22 bytes
        return struct.pack(
            "<hhiihhhHH",
            int(self.speed_kmh),
            int(self.engine_rpm),
            int(self.odo_m),
            int(self.trip_odo_m),
            int(self.outside_temp_c),
            int(self.inside_temp_c),
            int(self.battery_mv),
            int(self.curr_time_min) & 0xFFFF,
            int(self.trip_time_min) & 0xFFFF,
        )


class HostSender:
    def __init__(self, port: str, baud: int = 115200, timeout: float = 0):
        # 对 CDC ACM，波特率一般无意义，但 pyserial 仍要求填
        self.ser = serial.Serial(port=port, baudrate=baud, timeout=timeout, write_timeout=1)
        self.seq = 1

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    def send_frame(self, magic4: bytes, payload: bytes, typ: int = 0, flags: int = 0):
        hdr = pack_header(magic4, len(payload), self.seq, typ=typ, flags=flags, crc32=0)
        self.ser.write(hdr)
        self.ser.write(payload)
        self.seq += 1

    def send_msgf(self, snap: MsgfSnapshot):
        self.send_frame(MAGIC_MSGF, snap.pack())

    def send_imgf(self, png_path: str):
        with open(png_path, "rb") as f:
            png = f.read()
        self.send_frame(MAGIC_IMGF, png)


def run_demo(sender: HostSender, hz: float, png_path: Optional[str], png_every_s: float):
    """演示：MSGF 按 hz 发送；IMGF 每 png_every_s 秒发一次（如果提供 png_path）"""
    period = 1.0 / hz
    next_png = time.time() + png_every_s if (png_path and png_every_s > 0) else float("inf")

    # 初始化一些演示数据
    speed = 80
    rpm = 1800
    odo = 123_000
    trip = 12_340
    out_t = 5
    in_t = 22
    batt = 12150

    t0 = time.time()
    last = time.time()

    while True:
        now = time.time()

        # 每秒更新一次“当前时间/行程时间”
        # 当前时间取本机当地时间（HH:MM），行程时间为运行分钟数
        lt = time.localtime(now)
        curr_min = lt.tm_hour * 60 + lt.tm_min
        trip_min = int((now - t0) // 60)

        # 做一点随机抖动（可替换为真实数据源）
        # 为了不引入 random，这里用时间相位做伪变化
        speed = max(0, min(132, speed + (1 if int(now*2) % 7 == 0 else 0) - (1 if int(now*3) % 11 == 0 else 0)))
        rpm = max(0, min(8000, rpm + (50 if int(now*5) % 13 == 0 else 0) - (50 if int(now*7) % 17 == 0 else 0)))
        odo += max(0, speed)  # 粗略累加
        trip += max(0, speed)

        snap = MsgfSnapshot(
            speed_kmh=speed,
            engine_rpm=rpm,
            odo_m=odo,
            trip_odo_m=trip,
            outside_temp_c=out_t,
            inside_temp_c=in_t,
            battery_mv=batt,
            curr_time_min=curr_min,
            trip_time_min=trip_min,
        )

        sender.send_msgf(snap)

        # 可选：定时发送 PNG
        if now >= next_png:
            sender.send_imgf(png_path)
            next_png = now + png_every_s

        # 固定频率循环
        elapsed = time.time() - last
        sleep_s = period - elapsed
        if sleep_s > 0:
            time.sleep(sleep_s)
        last = time.time()


def run_once(sender: HostSender, speed: int, rpm: int, odo: int, trip: int, out_t: int, in_t: int, batt: int,
             curr_time: str, trip_min: int, png_path: Optional[str]):
    snap = MsgfSnapshot(
        speed_kmh=speed,
        engine_rpm=rpm,
        odo_m=odo,
        trip_odo_m=trip,
        outside_temp_c=out_t,
        inside_temp_c=in_t,
        battery_mv=batt,
        curr_time_min=hhmm_to_minutes(curr_time),
        trip_time_min=trip_min,
    )
    sender.send_msgf(snap)
    if png_path:
        sender.send_imgf(png_path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True, help="例如 /dev/ttyACM0 或 COM5")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--mode", choices=["demo", "once"], default="demo")

    # demo 参数
    ap.add_argument("--hz", type=float, default=24.0, help="MSGF 发送频率")
    ap.add_argument("--png", type=str, default=None, help="PNG 文件路径（IMGF）")
    ap.add_argument("--png-every", type=float, default=29.0, help="多少秒发送一次 PNG（demo 模式）")

    # once 参数
    ap.add_argument("--speed", type=int, default=80)
    ap.add_argument("--rpm", type=int, default=1800)
    ap.add_argument("--odo", type=int, default=123000)
    ap.add_argument("--trip", type=int, default=12340)
    ap.add_argument("--out-t", type=int, default=5)
    ap.add_argument("--in-t", type=int, default=22)
    ap.add_argument("--batt", type=int, default=12150)
    ap.add_argument("--time", type=str, default="12:34", help="当前时间 HH:MM（once 模式）")
    ap.add_argument("--trip-min", type=int, default=0, help="行程分钟数（once 模式）")

    args = ap.parse_args()

    sender = HostSender(args.port, args.baud)
    try:
        if args.mode == "demo":
            run_demo(sender, hz=args.hz, png_path=args.png, png_every_s=args.png_every)
        else:
            run_once(sender, args.speed, args.rpm, args.odo, args.trip, args.out_t, args.in_t, args.batt, args.time, args.trip_min, args.png)
    finally:
        sender.close()


if __name__ == "__main__":
    main()
