#!/opt/zfs-led-monitor/venv/bin/python
"""
Checks ZFS pool health and sends a one-letter status to the Arduino:
  G = healthy, Y = warning, R = critical

Checks every --interval seconds (heartbeat), and immediately when it
receives SIGUSR1 (sent by the ZED zedlet on any ZFS event).

Requires: pyserial (installed in /opt/zfs-led-monitor/venv)
Usage:    sudo /opt/zfs-led-monitor/venv/bin/python zfs_status_sender.py \
              --port /dev/serial/by-id/<your-arduino> [--pool tank] [--interval 10]
"""

import argparse
import signal
import subprocess
import sys
import time

import serial

RED_STATES = {"FAULTED", "UNAVAIL", "SUSPENDED", "REMOVED", "OFFLINE"}
YELLOW_STATES = {"DEGRADED"}


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=30, check=True).stdout


def get_status(pool=None):
    """Return (code, reason) for the worst state across the monitored pools."""
    target = [pool] if pool else []

    try:
        out = run(["zpool", "list", "-H", "-o", "name,health"] + target)
    except Exception as e:
        return "R", f"zpool list failed: {e}"

    pools = [line.split("\t") for line in out.strip().splitlines() if line.strip()]
    if not pools:
        return "R", "no pools found"

    for name, health in pools:
        if health in RED_STATES:
            return "R", f"{name} is {health}"
    for name, health in pools:
        if health in YELLOW_STATES:
            return "Y", f"{name} is {health}"

    # All ONLINE, but check for read/write/checksum errors or data errors
    try:
        status_x = run(["zpool", "status", "-x"] + target)
        if "is healthy" not in status_x and "all pools are healthy" not in status_x:
            return "Y", "errors reported by 'zpool status -x'"

        full = run(["zpool", "status"] + target)
        if "resilver in progress" in full:
            return "Y", "resilver in progress"
    except Exception as e:
        return "R", f"zpool status failed: {e}"

    return "G", "all pools ONLINE"


def open_serial(port, baud):
    ser = serial.Serial(port, baud, timeout=1)
    time.sleep(2)  # opening the port resets most Arduinos; wait for boot
    ser.reset_input_buffer()
    return ser


def main():
    p = argparse.ArgumentParser(description="Send ZFS pool health to an Arduino LED indicator")
    p.add_argument("--port", default="/dev/ttyACM0", help="serial port of the Arduino")
    p.add_argument("--baud", type=int, default=9600)
    p.add_argument("--pool", help="monitor only this pool (default: all pools)")
    p.add_argument("--interval", type=int, default=10, help="seconds between heartbeat checks")
    args = p.parse_args()

    # Block SIGUSR1 so it queues up instead of killing the process;
    # sigtimedwait() below picks it up. Signals arriving mid-check stay
    # pending, so a burst of ZED events causes just one extra re-check.
    signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGUSR1})

    ser = None
    last_code = None
    trigger = "startup"

    while True:
        try:
            if ser is None:
                ser = open_serial(args.port, args.baud)
                print(f"Connected to {args.port}", flush=True)

            code, reason = get_status(args.pool)
            ser.write(f"{code}\n".encode())

            if code != last_code:
                print(f"[{time.strftime('%Y-%m-%d %H:%M:%S')}] {code}: {reason} "
                      f"(trigger: {trigger})", flush=True)
                last_code = code

        except serial.SerialException as e:
            print(f"Serial error: {e}; retrying...", file=sys.stderr, flush=True)
            if ser:
                ser.close()
            ser = None

        # Sleep until the next heartbeat, or wake early on a ZED event
        woken = signal.sigtimedwait({signal.SIGUSR1}, args.interval)
        trigger = "zed event" if woken else "heartbeat"


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
