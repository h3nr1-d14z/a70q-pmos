#!/usr/bin/env python3
# a70q-autobrightness: panel brightness from the ambient light sensor (Config menu "Adaptive
# Brightness", replacing Sxmo's sxmo_adaptivebrightness.sh); SSC light sensor via
# iio-sensor-proxy (ERRORS C61). Runs as the superd service "a70q-autobrightness".
#   a70q-autobrightness            daemon
#   a70q-autobrightness toggle     enable/disable (remembered in ~/.config/a70q/autobrightness-off)
#   a70q-autobrightness status     prints on|off
#
# - brightness % follows log10(lux) (perceived brightness), with hysteresis and a smooth ramp
# - a manual change (Sxmo brightness keys/menu) is kept as an offset on top of the curve
# - the sensor is only read while the screen is unlocked (sxmo.state "unlock")
import math
import os
import select
import signal
import subprocess
import sys
import time

BL = '/sys/class/backlight/panel0-backlight/'
FLAG = os.path.expanduser('~/.config/a70q/autobrightness-off')
STATE = os.path.join(os.environ.get('XDG_RUNTIME_DIR', '/run/user/%d' % os.getuid()), 'sxmo.state')
MIN_PCT, MAX_PCT = 3.0, 100.0
HYSTERESIS = 4.0     # % change of the target needed before moving
SERVICE = 'a70q-autobrightness'


def rd(path, default=''):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return default


MAX_RAW = int(rd(BL + 'max_brightness', '255') or 255)


def get_raw():
    return int(rd(BL + 'brightness', '0') or 0)


def set_raw(v):
    with open(BL + 'brightness', 'w') as f:
        f.write(str(int(v)))


def curve(lux):
    # 0 lux -> 3 %, 10 -> 28 %, 100 -> 52 %, 1000 -> 76 %, >= 10000 -> 100 %
    return max(MIN_PCT, min(MAX_PCT, 3 + 24.25 * math.log10(max(lux, 0) + 1)))


def unlocked():
    return rd(STATE, 'unlock') == 'unlock'


class Daemon:
    def __init__(self):
        self.mon = None
        self.lux = None
        self.bias = 0.0           # manual offset in %
        self.target = None        # % currently aimed at
        self.written = None       # last raw value written by us

    def start_sensor(self):
        if self.mon is None:
            self.mon = subprocess.Popen(['monitor-sensor', '--light'], stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL, text=True, bufsize=1)
            os.set_blocking(self.mon.stdout.fileno(), False)

    def stop_sensor(self):
        if self.mon is not None:
            self.mon.terminate()
            self.mon.wait()
            self.mon = None
            self.lux = None
            self.written = None   # brightness may change while locked (Sxmo dims on lock)

    def read_sensor(self):
        while True:
            try:
                line = self.mon.stdout.readline()
            except (OSError, ValueError):
                return
            if not line:
                if self.mon.poll() is not None:
                    self.mon = None
                return
            # "Light changed: 113.000000 (lux)" / "=== Has ambient light sensor (value: 0.0, ..."
            for key in ('Light changed:', 'value:'):
                if key in line:
                    try:
                        self.lux = float(line.split(key)[1].split()[0].rstrip(','))
                    except (IndexError, ValueError):
                        pass

    def step(self):
        raw = get_raw()
        if self.written is not None and abs(raw - self.written) > 2:
            # someone else (the user) changed the brightness: keep it as an offset
            if self.lux is not None:
                self.bias = max(-60.0, min(60.0, 100.0 * raw / MAX_RAW - curve(self.lux)))
            self.target = 100.0 * raw / MAX_RAW
            self.written = raw
            return
        if self.lux is None:
            return
        want = max(MIN_PCT, min(MAX_PCT, curve(self.lux) + self.bias))
        if self.target is None or abs(want - self.target) >= HYSTERESIS:
            self.target = want
        goal = round(MAX_RAW * self.target / 100.0)
        if raw != goal:
            # ramp: 15 % of the remaining distance per step, at least 1
            delta = goal - raw
            nxt = raw + (max(1, abs(delta) * 0.15) * (1 if delta > 0 else -1))
            nxt = round(min(max(nxt, min(raw, goal)), max(raw, goal)))
            set_raw(nxt)
            self.written = nxt
        else:
            self.written = raw

    def run(self):
        while True:
            if os.path.exists(FLAG):
                self.stop_sensor()
                time.sleep(5)
                continue
            if unlocked():
                self.start_sensor()
                self.read_sensor()
                self.step()
                ramping = self.target is not None and get_raw() != round(MAX_RAW * self.target / 100.0)
                if ramping or self.mon is None:
                    time.sleep(0.1)
                else:
                    # idle: sleep until the sensor reports (or 2 s, to notice lock/manual changes)
                    select.select([self.mon.stdout], [], [], 2.0)
            else:
                self.stop_sensor()
                time.sleep(1)


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'run'
    if cmd == 'status':
        print('off' if os.path.exists(FLAG) else 'on')
    elif cmd == 'toggle':
        if os.path.exists(FLAG):
            os.remove(FLAG)
            subprocess.run(['superctl', 'start', SERVICE], stdout=subprocess.DEVNULL)
            subprocess.run(['notify-send', 'Auto brightness on'])
        else:
            os.makedirs(os.path.dirname(FLAG), exist_ok=True)
            open(FLAG, 'w').close()
            subprocess.run(['superctl', 'stop', SERVICE], stdout=subprocess.DEVNULL)
            subprocess.run(['notify-send', 'Auto brightness off'])
    else:
        d = Daemon()

        def bye(*_):
            d.stop_sensor()
            sys.exit(0)
        signal.signal(signal.SIGTERM, bye)
        signal.signal(signal.SIGINT, bye)
        d.run()


if __name__ == '__main__':
    main()
