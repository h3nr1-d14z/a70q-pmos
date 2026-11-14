#!/usr/bin/env python3
# a70q-gnss: GNSS on the Galaxy A70 through the modem's QMI LOC service (16) over QRTR.
# QRTR clients are per process (a LOC client ID cannot be reused by another qmicli call), so the
# session start and the NMEA/position indications must live in one process: this script.
#   a70q-gnss.py [--seconds N] [--node 0]      NMEA on stdout, status on stderr
import argparse
import sys

import gi
gi.require_version('Qmi', '1.0')
gi.require_version('Qrtr', '1.0')
from gi.repository import GLib, Qmi, Qrtr  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument('--node', type=int, default=0, help='QRTR node of the modem (0)')
ap.add_argument('--seconds', type=int, default=0, help='stop after N seconds (0 = run forever)')
args = ap.parse_args()

loop = GLib.MainLoop()
state = {'client': None, 'device': None, 'fixes': 0}


def log(*a):
    print('a70q-gnss:', *a, file=sys.stderr, flush=True)


def fail(what, err):
    log(f'{what} failed: {err}')
    loop.quit()


def on_nmea(client, output):
    try:
        line = output.get_nmea_string()
    except GLib.Error:
        return
    sys.stdout.write(line if line.endswith('\n') else line + '\n')
    sys.stdout.flush()


def on_position(client, output):
    try:
        status = output.get_session_status().value_nick
    except GLib.Error:
        status = '?'
    try:
        lat = output.get_latitude()
        lon = output.get_longitude()
    except GLib.Error:
        if state.get('last_status') != status:
            log(f'session status: {status} (no fix yet)')
            state['last_status'] = status
        return
    state['fixes'] += 1
    log(f'position: {lat:.6f}, {lon:.6f} ({status})')


def on_engine_state(client, output):
    try:
        log(f'engine state: {output.get_engine_state().value_nick}')
    except GLib.Error:
        pass


def on_sv_info(client, output):
    try:
        svs = output.get_list()
        log(f'satellites in view: {len(svs)}')
    except GLib.Error:
        pass


def started(client, res):
    try:
        client.start_finish(res).get_result()
    except GLib.Error as e:
        return fail('LOC start', e)
    log('session started (standalone, periodic 1 s)')


def start_session(client):
    inp = Qmi.MessageLocStartInput.new()
    inp.set_session_id(1)
    inp.set_intermediate_report_state(Qmi.LocIntermediateReportState.ENABLE)
    inp.set_minimum_interval_between_position_reports(1000)
    inp.set_fix_recurrence_type(Qmi.LocFixRecurrenceType.PERIODIC_FIXES)
    client.start(inp, 10, None, started)


def nmea_types_set(client, res):
    try:
        client.set_nmea_types_finish(res).get_result()
    except GLib.Error as e:
        log(f'set NMEA types failed (keeping modem defaults): {e}')
    start_session(client)


def registered(client, res):
    try:
        client.register_events_finish(res).get_result()
    except GLib.Error as e:
        return fail('LOC register events', e)
    # the modem default is RMC + Qualcomm PQWP* only: enable the standard sentences
    t = Qmi.LocNmeaType
    inp = Qmi.MessageLocSetNmeaTypesInput.new()
    inp.set_nmea_types(t.GGA | t.GSA | t.GSV | t.RMC | t.VTG | t.GLGSV | t.GNGSA)
    client.set_nmea_types(inp, 10, None, nmea_types_set)


def client_ready(device, res):
    try:
        client = device.allocate_client_finish(res)
    except GLib.Error as e:
        return fail('allocate LOC client', e)
    state['client'] = client
    client.connect('nmea', on_nmea)
    client.connect('position-report', on_position)
    client.connect('gnss-sv-info', on_sv_info)
    client.connect('engine-state', on_engine_state)
    inp = Qmi.MessageLocRegisterEventsInput.new()
    inp.set_event_registration_mask(Qmi.LocEventRegistrationFlag.NMEA |
                                    Qmi.LocEventRegistrationFlag.POSITION_REPORT |
                                    Qmi.LocEventRegistrationFlag.GNSS_SATELLITE_INFO |
                                    Qmi.LocEventRegistrationFlag.ENGINE_STATE)
    client.register_events(inp, 10, None, registered)


def device_open(device, res):
    try:
        device.open_finish(res)
    except GLib.Error as e:
        return fail('QMI device open', e)
    device.allocate_client(Qmi.Service.LOC, Qmi.CID_NONE, 10, None, client_ready)


def device_new(obj, res):
    try:
        device = Qmi.Device.new_from_node_finish(res)
    except GLib.Error as e:
        return fail('QMI device', e)
    state['device'] = device
    device.open(Qmi.DeviceOpenFlags.AUTO, 10, None, device_open)


def node_ready(bus, res):
    try:
        node = bus.wait_for_node_finish(res)
    except GLib.Error as e:
        return fail(f'QRTR node {args.node}', e)
    Qmi.Device.new_from_node(node, None, device_new)


def bus_ready(obj, res):
    try:
        bus = Qrtr.Bus.new_finish(res)
    except GLib.Error as e:
        return fail('QRTR bus', e)
    state['bus'] = bus
    bus.wait_for_node(args.node, 10000, None, node_ready)


def stop():
    c = state.get('client')
    if c:
        inp = Qmi.MessageLocStopInput.new()
        inp.set_session_id(1)
        c.stop(inp, 5, None, lambda cl, r: loop.quit())
        GLib.timeout_add_seconds(3, loop.quit)
    else:
        loop.quit()
    return False


Qrtr.Bus.new(1000, None, bus_ready)
if args.seconds:
    GLib.timeout_add_seconds(args.seconds, stop)
GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, 15, stop)  # SIGTERM
GLib.unix_signal_add(GLib.PRIORITY_DEFAULT, 2, stop)
loop.run()
log(f'done, {state["fixes"]} position reports')
