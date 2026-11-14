#!/usr/bin/env python3
# a70q: clickable Sxmo status bar (swaybar status_command, see 50-a70q-sway.conf).
#
# Sxmo feeds swaybar plain pango text, so swaybar cannot tell which icon was clicked.
# sxmobar can print the i3bar protocol, but without block names (and with its default
# colour #FDFDFD for both text and background); `sxmobar -D` lists the component names in
# the same order. This wrapper adds the names, drops the default colours, enables
# click_events and runs an action for the clicked block (mouse click or finger tap).
# State circle = quick settings (sxmo_hook_quickactions.sh).
#
# Extra blocks computed here over D-Bus (no helper processes), merged into Sxmo's line:
#   WiFi signal % (NetworkManager, after the WiFi icon), Bluetooth icon while a device is
#   connected (BlueZ), CPU temperature (thermal zones), VI/EN of the fcitx5 input method.
# Polled every 3 s while the screen is unlocked (VI/EN), the rest every 15 s; slower when locked.
import json
import os
import subprocess
import sys
import threading
import time

try:
    import gi
    gi.require_version('Gio', '2.0')
    from gi.repository import Gio, GLib
except (ImportError, ValueError):
    Gio = None

DEFAULT_COLOUR = '#FDFDFD'
STATE = os.path.join(os.environ.get('XDG_RUNTIME_DIR', '/run/user/%d' % os.getuid()), 'sxmo.state')

lock = threading.Lock()
base = []        # last blocks from sxmobar (with names)
extra = {}       # name -> full_text
out = sys.stdout


def spawn(*cmd):
    subprocess.Popen(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                     stderr=subprocess.DEVNULL, start_new_session=True)


def notify(title, body=''):
    spawn('notify-send', title, body)


def rd(path, default=''):
    try:
        with open(path) as f:
            return f.read().strip()
    except OSError:
        return default


def sxmo_icon(name, fallback):
    for line in rd('/usr/share/sxmo/default_hooks/sxmo_hook_icons.sh').splitlines():
        if line.startswith(name + '='):
            return line.split('=', 1)[1].split('#')[0].strip().strip('"')
    return fallback


ICON_BT = sxmo_icon('icon_bth', 'BT')


# ---------- data sources ----------

def bus(kind):
    try:
        return Gio.bus_get_sync(kind, None)
    except (GLib.Error, AttributeError):
        return None


def call(conn, dest, path, iface, method, args=None, sig=None):
    try:
        return conn.call_sync(dest, path, iface, method, args, sig and GLib.VariantType(sig),
                              Gio.DBusCallFlags.NONE, 1000, None).unpack()
    except (GLib.Error, AttributeError):
        return None


def prop(conn, dest, path, iface, name):
    r = call(conn, dest, path, 'org.freedesktop.DBus.Properties', 'Get',
             GLib.Variant('(ss)', (iface, name)))
    return r[0] if r else None


def wifi_strength(sysbus):
    nm = 'org.freedesktop.NetworkManager'
    for dev in prop(sysbus, nm, '/org/freedesktop/NetworkManager', nm, 'Devices') or []:
        if prop(sysbus, nm, dev, nm + '.Device', 'DeviceType') != 2:   # NM_DEVICE_TYPE_WIFI
            continue
        ap = prop(sysbus, nm, dev, nm + '.Device.Wireless', 'ActiveAccessPoint')
        if ap and ap != '/':
            return prop(sysbus, nm, ap, nm + '.AccessPoint', 'Strength')
    return None


def bt_connected(sysbus):
    objs = call(sysbus, 'org.bluez', '/', 'org.freedesktop.DBus.ObjectManager', 'GetManagedObjects')
    if not objs:
        return False
    return any(i.get('org.bluez.Device1', {}).get('Connected') for i in objs[0].values())


def cpu_temp():
    t = 0
    base_dir = '/sys/class/thermal'
    for z in os.listdir(base_dir):
        if rd(f'{base_dir}/{z}/type').startswith(('cpu-', 'cpuss-')):
            try:
                t = max(t, int(rd(f'{base_dir}/{z}/temp', '0')))
            except ValueError:
                pass
    return t // 1000 if t else None


def fcitx_state(sessbus):
    r = call(sessbus, 'org.fcitx.Fcitx5', '/controller', 'org.fcitx.Fcitx.Controller1', 'State')
    return r[0] if r else None   # 0 closed, 1 inactive (EN), 2 active (VI)


def fcitx_toggle():
    if Gio:
        call(bus(Gio.BusType.SESSION), 'org.fcitx.Fcitx5', '/controller',
             'org.fcitx.Fcitx.Controller1', 'Toggle')


def poll():
    if Gio is None:
        return
    sysbus, sessbus = bus(Gio.BusType.SYSTEM), bus(Gio.BusType.SESSION)
    n = 0
    while True:
        unlocked = rd(STATE, 'unlock') == 'unlock'
        new = dict(extra)
        st = fcitx_state(sessbus)
        new['a70q-vi'] = {2: 'VI', 1: 'EN'}.get(st, '')
        if n % 5 == 0:
            s = wifi_strength(sysbus)
            new['a70q-wifi'] = f'{s}%' if s is not None else ''
            new['a70q-bt'] = ICON_BT if bt_connected(sysbus) else ''
            t = cpu_temp()
            new['a70q-temp'] = f'{t}°' if t is not None else ''
        n += 1
        if new != extra:
            with lock:
                extra.clear()
                extra.update(new)
            render()
        time.sleep(3 if unlocked else 15)


# ---------- clicks ----------

def battery_info():
    try:
        res = subprocess.run(['upower', '-i', '/org/freedesktop/UPower/devices/battery_battery'],
                             capture_output=True, text=True, timeout=5).stdout
    except (OSError, subprocess.TimeoutExpired):
        return
    info = {}
    for line in res.splitlines():
        k, sep, v = line.strip().partition(':')
        if sep:
            info[k.strip()] = v.strip()
    body = [info.get('state', '?')]
    for key in ('time to empty', 'time to full', 'energy-rate', 'temperature'):
        if key in info:
            body.append(f'{key}: {info[key]}')
    notify(f"Battery {info.get('percentage', '?')}", '\n'.join(body))


def temp_info():
    zones = {}
    for z in sorted(os.listdir('/sys/class/thermal')):
        ty = rd(f'/sys/class/thermal/{z}/type')
        if ty.endswith('-usr') or ty in ('xo-therm-adc', 'quiet-therm-adc', 'pa-therm1-adc'):
            try:
                zones[ty] = int(rd(f'/sys/class/thermal/{z}/temp', '0')) / 1000
            except ValueError:
                pass
    cpu = max([v for k, v in zones.items() if k.startswith(('cpu-', 'cpuss-'))] or [0])
    gpu = zones.get('gpu-usr', 0)
    bat = int(rd('/sys/class/power_supply/battery/temp', '0') or 0) / 10
    notify('Temperature', f'CPU {cpu:.0f}°C\nGPU {gpu:.0f}°C\nBattery {bat:.1f}°C')


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
    elif name == 'a70q-vi':
        fcitx_toggle()
    elif name == 'a70q-temp':
        temp_info()
    elif name.startswith(('wifi', 'ethernet', 'network', 'a70q-wifi')):
        spawn('sxmo_networks.sh')
    elif name.startswith(('bluetooth', 'a70q-bt')):
        spawn('sxmo_bluetoothmenu.sh')
    elif name.startswith('battery'):
        battery_info()
    elif name == 'time':
        date_info()
    elif name == 'notifs':
        spawn('sxmo_notifs.sh', 'menu')
    else:  # state (lock/unlock circle), notch, unknown: quick settings
        spawn('sxmo_hook_quickactions.sh')


def read_clicks():
    for line in sys.stdin:
        line = line.strip().lstrip(',').strip()
        if not line.startswith('{'):
            continue
        try:
            on_click(json.loads(line))
        except Exception as e:  # never let a bad event kill the bar
            print(f'a70q-statusbar: {e}', file=sys.stderr)


# ---------- output ----------

def names():
    try:
        res = subprocess.run(['sxmobar', '-D'], capture_output=True, text=True, timeout=5).stdout
    except (OSError, subprocess.TimeoutExpired):
        return []
    # lines look like ">content< priority name "
    r = []
    for line in res.splitlines():
        head, sep, tail = line.rpartition('< ')
        if sep:
            parts = tail.split()
            r.append(parts[1] if len(parts) > 1 else '')
    return r


def block(name, text):
    return {'full_text': text, 'name': name, 'separator': False, 'separator_block_width': 8}


def render():
    with lock:
        blocks = []
        for b in base:
            n = b.get('name', '')
            if n == 'time':
                for e in ('a70q-temp', 'a70q-vi'):
                    if extra.get(e):
                        blocks.append(block(e, extra[e]))
            blocks.append(b)
            if n == 'wifi-status':
                for e in ('a70q-wifi', 'a70q-bt'):
                    if extra.get(e):
                        blocks.append(block(e, extra[e]))
        try:
            out.write(json.dumps(blocks, ensure_ascii=False) + ',\n')
            out.flush()
        except BrokenPipeError:
            os._exit(0)


def on_sxmobar(line):
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
    with lock:
        base[:] = blocks
    render()


def main():
    out.write(json.dumps({'version': 1, 'click_events': True}) + '\n[\n')
    out.flush()
    threading.Thread(target=read_clicks, daemon=True).start()
    threading.Thread(target=poll, daemon=True).start()
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
            on_sxmobar(line)
        print(f'a70q-statusbar: sxmobar -w exited ({watch.wait()}), restarting', file=sys.stderr)
        time.sleep(1)


if __name__ == '__main__':
    main()
