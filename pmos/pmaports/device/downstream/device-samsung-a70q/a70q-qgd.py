#!/usr/bin/env python3
# a70q-qgd: state-of-charge for the PM6150 "QG" fuel gauge (ERRORS C72).
#
# The downstream qpnp-qg driver only measures: it hands voltage/current FIFOs to userspace through
# /dev/qg and waits for the SOC to be written back (QG_SYS_SOC, 0..10000); on Android a closed
# Qualcomm daemon does that. Without it the SOC computed at boot stays forever (the phone showed
# 97 % all night while the battery went from 4.20 V to 3.83 V, i.e. ~45 %), and the kernel's
# low-battery shutdown never triggers. This daemon:
#   - anchors the SOC on the battery profile's OCV->SOC table (/dev/qg_battery BPIOCXSOC) whenever
#     the kernel reports a rested-battery OCV (QG_GOOD_OCV_UV, e.g. after suspend) or at start,
#   - coulomb-counts every FIFO sample in between (I > 0 = discharging, uA; count x interval ms),
#   - pulls slowly towards the voltage-derived SOC while the current is tiny (drift correction),
#   - sets 100 % when the charger reports Full, and keeps the value across reboots.
import fcntl
import os
import select
import signal
import struct
import sys
import time

QG_MAX = 18
QG_SOC, QG_OCV_UV, QG_GOOD_OCV_UV, QG_PON_OCV_UV, QG_SYS_SOC = 0, 1, 4, 3, 16
KFMT = '3I' + '4I' * 16 + 'I?3x' * QG_MAX            # struct qg_kernel_data
UFMT = 'I?3x' * QG_MAX                                # struct qg_user_data
BPFMT = '7i'                                          # struct battery_params
BPIOCXSOC = (3 << 30) | (struct.calcsize(BPFMT) << 16) | (ord('B') << 8) | 1
BPIOCXFCC = (3 << 30) | (struct.calcsize(BPFMT) << 16) | (ord('B') << 8) | 3
TABLE_SOC_OCV1, TABLE_SOC_OCV2 = 0, 1                 # charging, discharging (lookup_soc_ocv)
PS = '/sys/class/power_supply/'
STATE = '/var/lib/a70q-qgd/soc'
DESIGN_MAH = 4238
REST_UA = 50000          # |I| below this: battery close to rest, voltage ~ OCV
REST_WEIGHT = 0.05       # per FIFO batch (~1 min awake)


def rd(path, default=''):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return default


def log(msg):
    print(f'a70q-qgd: {msg}', flush=True)


class Gauge:
    def __init__(self):
        self.qg = os.open('/dev/qg', os.O_RDWR)
        self.bp = os.open('/dev/qg_battery', os.O_RDWR)
        self.soc = None                    # 0..10000
        self.last_save = 0

    def temp(self):
        try:
            return int(rd(PS + 'bms/temp', '250'))          # deci-degC
        except ValueError:
            return 250

    def charging(self):
        return rd(PS + 'battery/status') in ('Charging', 'Full')

    def ocv_to_soc(self, ocv_uv, charging=False):
        b = bytearray(struct.pack(BPFMT, 0, ocv_uv, 0, 0, 0, self.temp(),
                                  TABLE_SOC_OCV1 if charging else TABLE_SOC_OCV2))
        fcntl.ioctl(self.bp, BPIOCXSOC, b)
        return struct.unpack(BPFMT, b)[0]

    def fcc_mah(self):
        try:
            b = bytearray(struct.pack(BPFMT, 0, 0, 0, 0, 0, self.temp(), 0))
            fcntl.ioctl(self.bp, BPIOCXFCC, b)
            f = struct.unpack(BPFMT, b)[2]
            return f if 1000 < f < 10000 else DESIGN_MAH
        except OSError:
            return DESIGN_MAH

    def load(self):
        try:
            soc, ts = rd(STATE).split()
            if time.time() - float(ts) < 7 * 86400:
                return float(soc)
        except ValueError:
            pass
        return None

    def save(self, force=False):
        if self.soc is None or (not force and time.time() - self.last_save < 300):
            return
        os.makedirs(os.path.dirname(STATE), exist_ok=True)
        with open(STATE + '.tmp', 'w') as f:
            f.write(f'{self.soc:.1f} {time.time():.0f}\n')
        os.replace(STATE + '.tmp', STATE)
        self.last_save = time.time()

    def write(self):
        vals = [(0, False)] * QG_MAX
        s = int(round(max(0, min(10000, self.soc))))
        vals[QG_SYS_SOC] = (s, True)
        vals[QG_SOC] = ((s + 50) // 100, True)
        os.write(self.qg, struct.pack(UFMT, *[x for v in vals for x in v]))

    def handle(self, buf):
        v = struct.unpack(KFMT, buf)
        n = v[2]
        fifo = [v[3 + 4 * k:7 + 4 * k] for k in range(min(n, 16))]
        p = v[3 + 64:]
        param = {k: p[2 * k] for k in range(QG_MAX) if p[2 * k + 1]}
        charging = self.charging()

        if self.soc is None:
            self.soc = self.load()
            if self.soc is not None:
                log(f'start from saved SOC {self.soc / 100:.1f} %')
        ocv = param.get(QG_GOOD_OCV_UV) or (param.get(QG_PON_OCV_UV) if self.soc is None else None)
        if ocv:
            self.soc = float(self.ocv_to_soc(ocv, charging))
            log(f'OCV {ocv / 1e6:.3f} V -> SOC {self.soc / 100:.1f} %')
        if self.soc is None:   # no OCV yet: estimate from the last sample
            vv, ii = fifo[-1][0], struct.unpack('i', struct.pack('I', fifo[-1][1]))[0]
            self.soc = float(self.ocv_to_soc(vv + ii * 0.15, charging))   # ~0.15 ohm
            log(f'start from loaded voltage {vv / 1e6:.3f} V -> SOC {self.soc / 100:.1f} %')

        fcc = self.fcc_mah()
        sum_i = sum_v = 0
        for vv, iraw, count, interval in fifo:
            i_ua = struct.unpack('i', struct.pack('I', iraw))[0]
            dt_ms = count * interval
            self.soc -= i_ua * dt_ms / 3.6e6 / (fcc * 1000.0) * 10000.0
            sum_i += i_ua
            sum_v += vv
        if fifo:
            avg_i, avg_v = sum_i / len(fifo), sum_v / len(fifo)
            if abs(avg_i) < REST_UA and not charging:
                target = self.ocv_to_soc(int(avg_v + avg_i * 0.15), False)
                self.soc += (target - self.soc) * REST_WEIGHT
        if rd(PS + 'battery/status') == 'Full':
            self.soc = 10000.0
        self.soc = max(0.0, min(10000.0, self.soc))
        self.write()
        self.save()

    def run(self):
        size = struct.calcsize(KFMT)
        while True:
            r, _, _ = select.select([self.qg], [], [], 600)
            if not r:
                continue
            try:
                buf = os.read(self.qg, size)
            except OSError as e:
                log(f'read: {e}')
                time.sleep(5)
                continue
            if len(buf) == size:
                try:
                    self.handle(buf)
                except Exception as e:   # keep running whatever happens
                    log(f'error: {e}')


def main():
    g = Gauge()

    def bye(*_):
        g.save(force=True)
        sys.exit(0)
    signal.signal(signal.SIGTERM, bye)
    signal.signal(signal.SIGINT, bye)
    g.run()


if __name__ == '__main__':
    main()
