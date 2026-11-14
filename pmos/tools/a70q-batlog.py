#!/usr/bin/env python3
# Battery / suspend logger for the a70q (run as root on the phone, in the background):
#   start-stop-daemon --start --background --make-pidfile --pidfile /run/a70q-batlog.pid \
#       --exec /usr/bin/python3 -- /path/a70q-batlog.py /var/log/a70q-batlog.csv
# One CSV line every INTERVAL seconds while awake and one right after every resume
# (detected as a jump between CLOCK_BOOTTIME and CLOCK_MONOTONIC: time spent suspended).
# Analyse with: a70q-batlog.py --report /var/log/a70q-batlog.csv
import os
import sys
import time

INTERVAL = 300
PS = '/sys/class/power_supply/battery/'


def rd(path, default=''):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return default


def suspend_ok():
    for line in rd('/sys/kernel/debug/suspend_stats').splitlines():
        if line.startswith('success:'):
            return line.split()[1]
    return ''


def sample(out):
    boot = time.clock_gettime(time.CLOCK_BOOTTIME)
    mono = time.clock_gettime(time.CLOCK_MONOTONIC)
    row = [time.strftime('%Y-%m-%d %H:%M:%S'), f'{boot:.0f}', f'{boot - mono:.0f}',
           rd(PS + 'capacity'), rd(PS + 'voltage_now'), rd(PS + 'current_now'), rd(PS + 'status'),
           suspend_ok(), rd('/run/user/10000/sxmo.state', '?')]
    out.write(','.join(row) + '\n')
    out.flush()


def report(path):
    rows = [l.strip().split(',') for l in open(path) if l[0].isdigit()]
    rows = [r for r in rows if r[6] == 'Discharging']   # unplugged stretch only
    if len(rows) < 2:
        print('not enough samples')
        return
    # boottime/suspended restart at every reboot: sum the per-boot deltas
    hours = slept = 0.0
    for a, b in zip(rows, rows[1:]):
        if float(b[1]) >= float(a[1]):
            hours += (float(b[1]) - float(a[1])) / 3600
            slept += (float(b[2]) - float(a[2])) / 3600
    first, last = rows[0], rows[-1]
    drop = int(first[3]) - int(last[3])
    print(f'{first[0]} -> {last[0]}: {hours:.2f} h, suspended {slept:.2f} h '
          f'({100 * slept / hours if hours else 0:.0f}%), battery {first[3]}% -> {last[3]}% '
          f'({drop / hours if hours else 0:.2f} %/h)')


def main():
    if sys.argv[1:2] == ['--report']:
        return report(sys.argv[2])
    path = sys.argv[1] if len(sys.argv) > 1 else '/var/log/a70q-batlog.csv'
    new = not os.path.exists(path)
    with open(path, 'a') as out:
        if new:
            out.write('time,boottime_s,suspended_s,capacity,voltage_uV,current_uA,status,'
                      'suspend_ok,sxmo_state\n')
        last_gap = None
        last_sample = 0.0
        while True:
            boot = time.clock_gettime(time.CLOCK_BOOTTIME)
            gap = boot - time.clock_gettime(time.CLOCK_MONOTONIC)
            resumed = last_gap is not None and gap - last_gap > 5
            if resumed or boot - last_sample >= INTERVAL:
                sample(out)
                last_sample = boot
            last_gap = gap
            time.sleep(10)


if __name__ == '__main__':
    main()
