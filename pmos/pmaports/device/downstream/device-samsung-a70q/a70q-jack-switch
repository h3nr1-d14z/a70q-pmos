#!/usr/bin/env python3
# Mainline kernel: switch audio between the speaker (WSA881x) and the 3.5 mm jack (WCD937x)
# following the "Samsung Galaxy A70 Headset Jack" input device (no UCM for this card yet).
#   headphones in -> MultiMedia1 to RX codec DMA (HPHL/HPHR), speaker path off
#   headphones out -> MultiMedia1 to WSA codec DMA (speaker)
#   headset mic in -> capture from AMIC2 (WCD ADC2, INP2), else main mic AMIC1 (ADC1)
import fcntl
import glob
import os
import struct
import subprocess
import sys
import time

EV_SW = 0x05
SW_HEADPHONE_INSERT = 0x02
SW_MICROPHONE_INSERT = 0x04
EVIOCGSW = lambda n: (2 << 30) | (n << 16) | (ord('E') << 8) | 0x1b  # noqa: E731


def cset(name, value):
    subprocess.run(["amixer", "-q", "-c", "0", "cset", f"name={name}", str(value)],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def route_playback(headphones):
    if headphones:
        for n, v in (("RX_MACRO RX0 MUX", "AIF1_PB"), ("RX_MACRO RX1 MUX", "AIF1_PB"),
                     ("RX INT0_1 MIX1 INP0", "RX0"), ("RX INT1_1 MIX1 INP0", "RX1"),
                     ("RX INT0 DEM MUX", "CLSH_DSM_OUT"), ("RX INT1 DEM MUX", "CLSH_DSM_OUT"),
                     ("HPHL_RDAC Switch", 1), ("HPHR_RDAC Switch", 1),
                     ("HPHL Switch", 1), ("HPHR Switch", 1),
                     ("HPHL Volume", 20), ("HPHR Volume", 20),
                     ("RX_RX0 Digital Volume", 94), ("RX_RX1 Digital Volume", 94),
                     ("RX_CODEC_DMA_RX_0 Audio Mixer MultiMedia1", 1),
                     ("WSA_CODEC_DMA_RX_0 Audio Mixer MultiMedia1", 0)):
            cset(n, v)
    else:
        cset("WSA_CODEC_DMA_RX_0 Audio Mixer MultiMedia1", 1)
        cset("RX_CODEC_DMA_RX_0 Audio Mixer MultiMedia1", 0)


def route_capture(headset_mic):
    for b in (1, 2, 3):
        cset(f"ADC{b} Switch", 0)
    if headset_mic:
        cset("ADC2 MUX", "INP2")
        cset("ADC2 Switch", 1)
        cset("ADC2_MIXER Switch", 1)
        cset("ADC2 Volume", 16)
        cset("TX SMIC MUX0", "ADC2")
    else:
        cset("ADC1 Switch", 1)
        cset("ADC1_MIXER Switch", 1)
        cset("ADC1 Volume", 16)
        cset("TX SMIC MUX0", "ADC0")


def find_jack():
    for d in glob.glob("/sys/class/input/event*"):
        try:
            name = open(f"{d}/device/name").read().strip()
        except OSError:
            continue
        if name.endswith("Headset Jack"):
            return "/dev/input/" + os.path.basename(d)
    return None


def main():
    dev = None
    for _ in range(120):
        dev = find_jack()
        if dev:
            break
        time.sleep(1)
    if not dev:
        print("no headset jack input device", file=sys.stderr)
        return 1
    fd = os.open(dev, os.O_RDONLY)
    buf = bytearray(8)
    fcntl.ioctl(fd, EVIOCGSW(len(buf)), buf)
    sw = int.from_bytes(buf, "little")
    hp = bool(sw & (1 << SW_HEADPHONE_INSERT))
    mic = bool(sw & (1 << SW_MICROPHONE_INSERT))
    route_playback(hp)
    route_capture(mic)
    print(f"jack: headphones={hp} mic={mic}", flush=True)
    fmt = "llHHi"
    size = struct.calcsize(fmt)
    while True:
        data = os.read(fd, size)
        if len(data) < size:
            continue
        _, _, typ, code, value = struct.unpack(fmt, data)
        if typ != EV_SW:
            continue
        if code == SW_HEADPHONE_INSERT and bool(value) != hp:
            hp = bool(value)
            route_playback(hp)
            print(f"jack: headphones={hp}", flush=True)
        elif code == SW_MICROPHONE_INSERT and bool(value) != mic:
            mic = bool(value)
            route_capture(mic)
            print(f"jack: mic={mic}", flush=True)


if __name__ == "__main__":
    sys.exit(main())
