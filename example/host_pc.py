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
uint8  cmd
  - 0x00: 状态快照，后续字段与旧协议一致
  - 0x01: 设备重启（后续可无payload）
int16  speed_kmh
int16  engine_speed_rpm
int32  odo_m
int32  trip_odo_m
int16  outside_temp_c
int16  inside_temp_c
int16  battery_mv
uint16 curr_time_min   // 0..1439（HH:MM -> 分钟数）
uint16 trip_time_min   // 行程分钟数
uint16 fuel_left_dl    // 油箱余量，单位0.1L
uint16 fuel_total_dl   // 油箱总量，单位0.1L
... reserve(可选)

依赖：
pip install pyserial pillow
"""

import argparse
import random
import struct
import time
import requests
import io
from dataclasses import dataclass
from typing import Optional, List

import serial
from PIL import Image

TRACK_URL = "http://43.128.232.127:8123/track/image"
MAX_PNG_SIZE = 200 * 1024  # 200KB

MAGIC_MSGF = b"MSGF"
MAGIC_IMGF = b"IMGF"
MSG_CMD_SNAPSHOT = 0x00
MSG_CMD_REBOOT = 0x01


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


class TrackImageFetcher:
    def __init__(self, points: List[List[float]]):
        self.points = points
        self.count = 0

    def fetch_next(self) -> bytes:
        now = time.time()
        self.count += 1
        if self.count == 1:
            body = {"points": self.points[:2]}
        else:
            body = {"points": self.points[:self.count]}
        header = {
            "Content-Type": "application/json",
            "accept": "application/json"
        }

        resp = requests.post(
            TRACK_URL,
            json=body,
            headers=header,
            timeout=10,
        )
        resp.raise_for_status()

        png = resp.content
        if len(png) > MAX_PNG_SIZE:
            raise ValueError(f"PNG too large: {len(png)} bytes")
        
        print(f"\r\n Fetched {len(png)} bytes in {int((time.time() - now) * 1000):03d} ms")

        return png


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
    fuel_left_dl: int = 0        # 油箱余量，单位0.1L
    fuel_total_dl: int = 0       # 油箱总量，单位0.1L

    def pack(self) -> bytes:
        # < h h i i h h h H H H H = 2+2+4+4+2+2+2+2+2+2+2 = 26 bytes
        return struct.pack(
            "<hhiihhhHHHH",
            int(self.speed_kmh),
            int(self.engine_rpm),
            int(self.odo_m),
            int(self.trip_odo_m),
            int(self.outside_temp_c),
            int(self.inside_temp_c),
            int(self.battery_mv),
            int(self.curr_time_min) & 0xFFFF,
            int(self.trip_time_min) & 0xFFFF,
            int(self.fuel_left_dl) & 0xFFFF,
            int(self.fuel_total_dl) & 0xFFFF,
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

    def send_msgf(self, snap: MsgfSnapshot, cmd: int = MSG_CMD_SNAPSHOT):
        payload = struct.pack("<B", cmd & 0xFF) + snap.pack()
        self.send_frame(MAGIC_MSGF, payload)

    def send_msgf_cmd_only(self, cmd: int):
        payload = struct.pack("<B", cmd & 0xFF)
        self.send_frame(MAGIC_MSGF, payload)

    def send_imgf(self, png_path: str):
        with open(png_path, "rb") as f:
            png = f.read()
        self.send_frame(MAGIC_IMGF, png)
    
    def send_imgf_bytes(self, png: bytes):
        now = time.time()
        self.send_frame(MAGIC_IMGF, png)
        print(f" Sent IMGF {len(png)} bytes in {int((time.time() - now) * 1000):03d} ms")

    def send_imgf_r565_bytes(self, frame: bytes):
        now = time.time()
        self.send_frame(MAGIC_IMGF, frame)
        print(f" Sent IMGF(R565) {len(frame)} bytes in {int((time.time() - now) * 1000):03d} ms")


def png_to_r565_frame(png: bytes, resize_to: Optional[tuple[int, int]] = None, swap_bytes: bool = False) -> bytes:
    img = Image.open(io.BytesIO(png)).convert("RGB")
    if resize_to:
        try:
            resample = Image.Resampling.BILINEAR
        except AttributeError:
            resample = Image.BILINEAR
        img = img.resize(resize_to, resample)
    w, h = img.size
    raw = bytearray()
    for r, g, b in img.getdata():
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        if swap_bytes:
            raw.extend(struct.pack(">H", v))
        else:
            raw.extend(struct.pack("<H", v))
    # 8 bytes header: "R565" + w + h
    return b"R565" + struct.pack("<HH", w, h) + bytes(raw)



def run_demo(
    sender: HostSender, hz: float, 
    png_path: Optional[str], 
    png_every_s: float,
    fetcher: Optional[TrackImageFetcher],
    track_every_s: float,
    img_mode: str,
    img_w: Optional[int],
    img_h: Optional[int],
    r565_swap_bytes: bool,
    ):
    """演示：MSGF 按 hz 发送；IMGF 每 png_every_s 秒发一次（如果提供 png_path）"""
    period = 1.0 / hz
    next_png = time.time() + png_every_s if (png_path and png_every_s > 0) else float("inf")
    next_fetch = time.time() + track_every_s if fetcher else float("inf")
    
    # 初始化一些演示数据
    speed = 80
    rpm = 1800
    odo = 123_000
    trip = 12_340
    out_t = 50
    in_t = 22
    batt = 14200
    fuel_total_dl = 520  # 52.0L，固定油箱容量
    fuel_left_dl = 360   # 36.0L
    out_t_target = random.randint(-200, 280)  # 单位 0.1°C
    next_temp_tick = time.time()
    next_batt_tick = time.time()

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
        speed = max(0, min(132, speed + (1 if int(now*2) % 7 == 0 else 0) - (1 if int(now*3) % 11 == 0 else 0)))
        rpm = max(0, min(8000, rpm + (50 if int(now*5) % 13 == 0 else 0) - (50 if int(now*7) % 17 == 0 else 0)))
        odo += max(0, speed)  # 粗略累加
        trip += max(0, speed)

        # 外温缓慢变化：每秒变动 0.1°C，范围 -20.0~+28.0°C
        if now >= next_temp_tick:
            if abs(out_t - out_t_target) <= 1:
                out_t_target = random.randint(-200, 280)
            out_t += 1 if out_t_target > out_t else -1
            out_t = max(-200, min(280, out_t))
            next_temp_tick = now + 1.0

        # 电池电压随机变化：13.8V~14.7V（单位 mV）
        if now >= next_batt_tick:
            batt = random.randint(13800, 14700)
            next_batt_tick = now + 0.5

        # 演示用：每秒按0.1L缓慢消耗，降到0后循环回满箱
        fuel_left_dl = fuel_left_dl - 1 if fuel_left_dl > 0 else fuel_total_dl

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
            fuel_left_dl=fuel_left_dl,
            fuel_total_dl=fuel_total_dl,
        )
        print(
            f" Sent MSGF {speed:03d} {rpm:04d} {odo:08d} {trip:08d} "
            f"out={out_t/10:+05.1f}C in={in_t:02d}C batt={batt/1000:.2f}V "
            f"{curr_min:04d} {trip_min:04d} {fuel_left_dl/10:.1f}/{fuel_total_dl/10:.1f}",
            end="\r",
        )
        sender.send_msgf(snap, cmd=MSG_CMD_SNAPSHOT)
        
        # 原有：本地 PNG demo
        if now >= next_png and png_path:
            with open(png_path, "rb") as f:
                png = f.read()
            if img_mode == "r565":
                frame = png_to_r565_frame(
                    png,
                    (img_w, img_h) if img_w and img_h else None,
                    swap_bytes=r565_swap_bytes,
                )
                sender.send_imgf_r565_bytes(frame)
            else:
                sender.send_imgf_bytes(png)
            next_png = now + png_every_s

        # 新增：远程轨迹 PNG
        if fetcher and now >= next_fetch:
            png = fetcher.fetch_next()
            if img_mode == "r565":
                frame = png_to_r565_frame(
                    png,
                    (img_w, img_h) if img_w and img_h else None,
                    swap_bytes=r565_swap_bytes,
                )
                sender.send_imgf_r565_bytes(frame)
            else:
                sender.send_imgf_bytes(png)
            next_fetch = now + track_every_s


        # 固定频率循环
        elapsed = time.time() - last
        sleep_s = period - elapsed
        if sleep_s > 0:
            time.sleep(sleep_s)
        last = time.time()


def run_once(sender: HostSender, speed: int, rpm: int, odo: int, trip: int, out_t: int, in_t: int, batt: int,
             curr_time: str, trip_min: int, fuel_left: float, fuel_total: float, png_path: Optional[str], img_mode: str,
             img_w: Optional[int], img_h: Optional[int], r565_swap_bytes: bool, reboot_cmd: bool):
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
        fuel_left_dl=int(round(fuel_left * 10)),
        fuel_total_dl=int(round(fuel_total * 10)),
    )
    sender.send_msgf(snap, cmd=MSG_CMD_SNAPSHOT)
    if reboot_cmd:
        sender.send_msgf_cmd_only(MSG_CMD_REBOOT)
    if png_path:
        with open(png_path, "rb") as f:
            png = f.read()
        if img_mode == "r565":
            frame = png_to_r565_frame(
                png,
                (img_w, img_h) if img_w and img_h else None,
                swap_bytes=r565_swap_bytes,
            )
            sender.send_imgf_r565_bytes(frame)
        else:
            sender.send_imgf_bytes(png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", required=True, help="例如 /dev/ttyACM0 或 COM5")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--mode", choices=["demo", "once"], default="demo")

    # demo 参数
    ap.add_argument("--hz", type=float, default=24.0, help="MSGF 发送频率")
    ap.add_argument("--png", type=str, default=None, help="PNG 文件路径（IMGF）")
    ap.add_argument("--png-every", type=float, default=29.0, help="多少秒发送一次 PNG（demo 模式）")
    ap.add_argument("--track", action="store_true", help="启用远程轨迹 PNG")
    ap.add_argument("--track-every", type=float, default=25.0)
    ap.add_argument("--img-mode", choices=["png", "r565"], default="png", help="图片发送模式：PNG或RGB565原始帧")
    ap.add_argument("--img-w", type=int, default=None, help="r565模式可选：重采样宽度")
    ap.add_argument("--img-h", type=int, default=None, help="r565模式可选：重采样高度")
    ap.add_argument("--r565-swap-bytes", action="store_true", help="r565模式可选：交换RGB565高低字节")


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
    ap.add_argument("--fuel-left", type=float, default=36.0, help="油箱余量，单位L（once 模式）")
    ap.add_argument("--fuel-total", type=float, default=52.0, help="油箱总量，单位L（once 模式）")
    ap.add_argument("--reboot-cmd", action="store_true", help="once模式额外发送CMD=0x01重启命令")

    args = ap.parse_args()

    sender = HostSender(args.port, args.baud)
    fetcher = None
    if args.track:
        TRACK_POINTS = [
            [121.154031, 31.157299],
            [121.154365, 31.155985],
            [121.154744, 31.154187],
            [121.154915, 31.152359],
            [121.154964, 31.151833],
            [121.153753, 31.151841],
            [121.152769, 31.151813],
            [121.151304, 31.151563],
            [121.149513, 31.150837],
            [121.148349, 31.150036],
            [121.146422, 31.148348],
            [121.14429, 31.146466],
            [121.141302, 31.143829],
            [121.139483, 31.142251],
            [121.137232, 31.140924],
            [121.134626, 31.140155],
            [121.132587, 31.139938],
            [121.130001, 31.139823],
            [121.125761, 31.139634],
            [121.122411, 31.139488],
            [121.121591, 31.139449],
            [121.121424, 31.138729],
            [121.121332, 31.137331],
            [121.121301, 31.135821],
            [121.121329, 31.134526],
            [121.121266, 31.133201],
            [121.120599, 31.133007],
            [121.119659, 31.133548],
            [121.11915, 31.134453],
            [121.118852, 31.136648],
            [121.11944, 31.137946],
            [121.120364, 31.13758],
            [121.11965, 31.136552],
            [121.117848, 31.135905],
            [121.115562, 31.134899],
            [121.113321, 31.133638],
            [121.110643, 31.131903],
            [121.106918, 31.129432],
            [121.099019, 31.124187],
            [121.092517, 31.119858],
            [121.087278, 31.116388],
            [121.078716, 31.110698],
            [121.073428, 31.107181],
            [121.068503, 31.103923],
            [121.063857, 31.101107],
            [121.060714, 31.099495],
            [121.056252, 31.0976],
            [121.052679, 31.096383],
            [121.049606, 31.095503],
            [121.045493, 31.094575],
            [121.042038, 31.094007],
            [121.038549, 31.093621],
            [121.035305, 31.093432],
            [121.032624, 31.093385],
            [121.029315, 31.093359],
            [121.023084, 31.093309],
            [121.014997, 31.093246],
            [121.009183, 31.0932],
            [121.004524, 31.092894],
            [120.999189, 31.091772],
            [120.994219, 31.089922],
            [120.98963, 31.08734],
            [120.983975, 31.083578],
            [120.979236, 31.079077],
            [120.974736, 31.077291],
            [120.968544, 31.074847],
            [120.962446, 31.074348],
            [120.961548, 31.072195],
            [120.964472, 31.07324],
            [120.974736, 31.077291],
            [120.983928, 31.076962]
        ]
        fetcher = TrackImageFetcher(TRACK_POINTS)
    try:
        if args.mode == "demo":
            run_demo(
                sender,
                hz=args.hz,
                png_path=args.png,
                png_every_s=args.png_every,
                fetcher=fetcher,
                track_every_s=args.track_every,
                img_mode=args.img_mode,
                img_w=args.img_w,
                img_h=args.img_h,
                r565_swap_bytes=args.r565_swap_bytes,
            )
        else:
            run_once(sender, args.speed, args.rpm, args.odo, args.trip,
                    args.out_t, args.in_t, args.batt,
                    args.time, args.trip_min, args.fuel_left, args.fuel_total, args.png, args.img_mode, args.img_w, args.img_h,
                    args.r565_swap_bytes, args.reboot_cmd)
    finally:
        sender.close()


if __name__ == "__main__":
    main()
