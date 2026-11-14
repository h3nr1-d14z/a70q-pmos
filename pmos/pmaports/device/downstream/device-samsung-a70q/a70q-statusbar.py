#!/usr/bin/env python3
# a70q: clickable Sxmo status bar (swaybar status_command, see 50-a70q-sway.conf).
#
# Sxmo feeds swaybar plain pango text, so swaybar cannot tell which icon was clicked.
# sxmobar can print the i3bar protocol, but without block names (and with its default
# colour #FDFDFD for both text and background); `sxmobar -D` lists the component names in
# the same order. This wrapper adds the names, drops the default colours, enables
# click_events and runs an action for the clicked block (mouse click or finger tap).
import json
import os
import subprocess
import sys
import threading
import time

DEFAULT_COLOUR = '#FDFDFD'


def spawn(*cmd):
    subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL, start_new_session=True)


def notify(title, body=''):
    spawn('notify-send', title, body)


def battery_info():
    try:
        out = subprocess.run(['upower', '-i', '/org/freedesktop/UPower/devices/battery_battery'],
                             capture_output=True, text=True, timeout=5).stdout
    except (OSError, subprocess.TimeoutExpired):
        return
    info = {}
    for line in out.splitlines():
        k, sep, v = line.strip().partition(':')
        if sep:
            info[k.strip()] = v.strip()
    body = [info.get('state', '?')]
    for key in ('time to empty', 'time to full', 'energy-rate', 'temperature'):
        if key in info:
            body.append(f'{key}: {info[key]}')
    notify(f"Battery {info.get('percentage', '?')}", '\n'.join(body))


def date_info():
    try:
        cal = subprocess.run(['cal'], capture_output=True, text=True, timeout=5).stdout
    except (OSError, subprocess.TimeoutExpired):
        cal = ''
    date = subprocess.run(['date', '+%A %d %B %Y'], capture_output=True, text=True).stdout
    notify(date.strip(), cal)


def on_click(ev):
    name = ev.get('name', '')
    button = ev.get('button', 1)
    if button == 3:  # right click anywhere on the bar: main menu (also bar bindsym button3)
        spawn('sxmo_appmenu.sh')
    elif name == 'volume':
        if button == 4:
            spawn('sxmo_audio.sh', 'vol', 'up')
        elif button == 5:
            spawn('sxmo_audio.sh', 'vol', 'down')
        elif button == 2:
            spawn('sxmo_audio.sh', 'vol', 'togglemute')
        else:
            spawn('sxmo_audio.sh', 'menu')
    elif button != 1:
        return
    elif name.startswith(('wifi', 'ethernet', 'network')):
        spawn('sxmo_networks.sh')
    elif name.startswith('bluetooth'):
        spawn('sxmo_bluetoothmenu.sh')
    elif name.startswith('battery'):
        battery_info()
    elif name == 'time':
        date_info()
    elif name == 'notifs':
        spawn('sxmo_notifs.sh', 'menu')
    else:  # state (lock/unlock icon), notch, unknown
        spawn('sxmo_appmenu.sh', 'sys')


def read_clicks():
    for line in sys.stdin:
        line = line.strip().lstrip(',').strip()
        if not line.startswith('{'):
            continue
        try:
            on_click(json.loads(line))
        except Exception as e:  # never let a bad event kill the bar
            print(f'a70q-statusbar: {e}', file=sys.stderr)


def names():
    try:
        out = subprocess.run(['sxmobar', '-D'], capture_output=True, text=True, timeout=5).stdout
    except (OSError, subprocess.TimeoutExpired):
        return []
    # lines look like ">content< priority name "
    res = []
    for line in out.splitlines():
        head, sep, tail = line.rpartition('< ')
        if sep:
            parts = tail.split()
            res.append(parts[1] if len(parts) > 1 else '')
    return res


def main():
    out = sys.stdout
    out.write(json.dumps({'version': 1, 'click_events': True}) + '\n[\n')
    out.flush()
    threading.Thread(target=read_clicks, daemon=True).start()
    # sxmobar -w can exit while the session starts (its state is created by Sxmo's
    # statusbar hook): restart it instead of ending the status line
    while True:
        try:
            watch = subprocess.Popen(['sxmobar', '-w', '-o', 'i3bar'], stdout=subprocess.PIPE,
                                     text=True)
        except OSError as e:
            print(f'a70q-statusbar: {e}', file=sys.stderr)
            time.sleep(2)
            continue
        for line in watch.stdout:
            emit(out, line)
        print(f'a70q-statusbar: sxmobar -w exited ({watch.wait()}), restarting', file=sys.stderr)
        time.sleep(1)


def emit(out, line):
    line = line.strip()
    if not line.startswith('['):
        return  # sxmobar's own header
    try:
        blocks = json.loads(line)
    except ValueError:
        return
    n = names()
    for i, b in enumerate(blocks):
        if i < len(n) and n[i]:
            b['name'] = n[i]
        for k in ('color', 'background'):
            if b.get(k, '').upper() == DEFAULT_COLOUR:
                del b[k]
        b['separator'] = False
        b['separator_block_width'] = 8
    try:
        out.write(json.dumps(blocks, ensure_ascii=False) + ',\n')
        out.flush()
    except BrokenPipeError:
        os._exit(0)

if __name__ == '__main__':
    main()
