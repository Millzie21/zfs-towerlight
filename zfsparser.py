#!/usr/bin/env python3
"""Send current ZFS pool health to the Arduino over serial (one-shot).
Called by the ZED hook (on state change) and by the heartbeat timer.
An flock serializes concurrent invocations so serial writes can't interleave.
Config via flags or env: ZFS_LED_POOL, ZFS_LED_PORT, ZFS_LED_BAUD.
Needs: pip install pyserial"""

import argparse, fcntl, os, subprocess, sys

try:
    import serial
except ImportError:
    sys.exit("pyserial not installed. Run: pip install pyserial")

LOCK_PATH = "/run/zfs-led.lock"


def query_health(pool):
    try:
        r = subprocess.run(["zpool", "list", "-Ho", "name,health", pool],
                           capture_output=True, text=True, timeout=15)
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return None
    if r.returncode != 0:
        return None
    parts = r.stdout.split()          # name<TAB>health
    return parts[-1].upper() if parts else None


def main():
    ap = argparse.ArgumentParser(description="One-shot ZFS health -> Arduino serial.")
    ap.add_argument("--pool", default=os.environ.get("ZFS_LED_POOL", "tank"))
    ap.add_argument("--port", default=os.environ.get("ZFS_LED_PORT", "/dev/ttyACM0"))
    ap.add_argument("--baud", type=int,
                    default=int(os.environ.get("ZFS_LED_BAUD", "9600")))
    ap.add_argument("--health", default=None,
                    help="health word to send; if omitted, query the pool")
    args = ap.parse_args()

    health = (args.health or query_health(args.pool) or "FAULTED").upper()

    lock = open(LOCK_PATH, "w")
    fcntl.flock(lock, fcntl.LOCK_EX)
    try:
        try:
            ser = serial.Serial(args.port, args.baud, timeout=1)
        except serial.SerialException as e:
            print(f"serial open failed ({args.port}): {e}", file=sys.stderr)
            return 1
        try:
            ser.write((health + "\n").encode())
            ser.flush()
        finally:
            ser.close()
    finally:
        fcntl.flock(lock, fcntl.LOCK_UN)
        lock.close()

    print(f"sent {health}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
