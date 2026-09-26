# ZFS LED Monitor

An Arduino with three LEDs that shows the health of your ZFS pools at a glance. The LEDs update within a second of any ZFS event, thanks to a ZED zedlet, with a heartbeat check every 10 seconds as a fallback.

| LED | Meaning |
| --- | --- |
| 🟢 Green | All pools ONLINE, no errors |
| 🟡 Yellow | A pool is DEGRADED, has read/write/checksum errors, or is resilvering |
| 🔴 Red | A pool is FAULTED, UNAVAIL, SUSPENDED, REMOVED or OFFLINE, or `zpool` failed |
| 🔁 All three cycling | No update from the host for 30 seconds (host or script is down), or waiting for the first update after power-up |

When you have several pools, the LEDs show the worst state among them.

## How it works

- **`zfs_status_sender.py`** runs as a systemd service. It checks `zpool list` and `zpool status`, then sends a single letter (`G`, `Y` or `R`) to the Arduino.
- **`all-led-monitor.sh`** is a ZED zedlet. On every ZFS event it signals the service to re-check immediately.
- **The Arduino sketch** lights the matching LED. If it hears nothing for 30 seconds, it cycles all three LEDs so you know the status is stale.

## Repository layout

```
zfs-led-monitor/
├── README.md
├── arduino/zfs_monitor/zfs_monitor.ino   # Arduino sketch
├── host/zfs_status_sender.py             # Status sender (runs from a venv)
├── systemd/zfs-led-monitor.service       # systemd unit
└── zed/all-led-monitor.sh                # ZED zedlet
```

## Requirements

**Hardware**

- Arduino with USB serial (Uno, Nano or similar)
- One green, one yellow and one red LED
- Three 220 Ω resistors
- USB cable to the ZFS host

**Software**

- Linux with OpenZFS, ZED (`zfs-zed`) and systemd
- Python 3 with the `venv` module
- Arduino IDE (for flashing the board)

## Wiring

| LED | Arduino pin |
| --- | --- |
| Green | 9 |
| Yellow | 10 |
| Red | 11 |

Wire each LED the same way: Arduino pin → 220 Ω resistor → LED anode (long leg). Connect the LED cathode (short leg) to GND.

## Installation

### 1. Download the files

On the ZFS host, download the repo with either git or curl.

**With git:**

```bash
git clone https://github.com/<your-username>/zfs-led-monitor.git
cd zfs-led-monitor
```

**Without git:**

```bash
curl -L https://github.com/<your-username>/zfs-led-monitor/archive/refs/heads/main.tar.gz | tar xz
cd zfs-led-monitor-main
```

Run all the remaining host commands from inside this folder.

### 2. Flash the Arduino

On the computer with the Arduino IDE, download the repo from GitHub (**Code → Download ZIP**) and extract it. Open `arduino/zfs_monitor/zfs_monitor.ino` in the Arduino IDE and upload it to the board.

On power-up the LEDs light green, yellow, red once as a lamp test, then cycle until the host sends its first status.

To check the board on its own, open the Serial Monitor at 9600 baud and send `G`, `Y` or `R`. The matching LED lights and the board replies `ACK G` (or `Y`, `R`). Close the Serial Monitor afterwards, or it will hold the port.

Then plug the Arduino into the ZFS host.

### 3. Find the serial port

Use the stable `/dev/serial/by-id/` path rather than `/dev/ttyACM0`, which can change between reboots:

```bash
ls -l /dev/serial/by-id/
```

```
usb-Arduino__www.arduino.cc__0043_75630313536351F0B1A1-if00 -> ../../ttyACM0
```

Note the full name before `->`. You'll need it in steps 4 and 5.

> [!NOTE]
> Clone boards with a CH340 chip often show a generic name like `usb-1a86_USB2.0-Serial-if00-port0`. That's fine with one board. With two identical clones, use `/dev/serial/by-path/` instead.

### 4. Create the virtual environment and install the script

The script and its venv live in `/opt/zfs-led-monitor/`, owned by root, because the service runs as root to query `zpool`.

Install the venv module. This is only needed on Debian, Ubuntu and Proxmox; most other distros include it already:

```bash
sudo apt install python3-venv
```

Create the venv, install pyserial into it, and copy the script into place:

```bash
sudo mkdir -p /opt/zfs-led-monitor
sudo python3 -m venv /opt/zfs-led-monitor/venv
sudo /opt/zfs-led-monitor/venv/bin/pip install pyserial
sudo install -m 755 host/zfs_status_sender.py /opt/zfs-led-monitor/
```

You never need to activate the venv. The script's shebang and the service both call `/opt/zfs-led-monitor/venv/bin/python` directly.

Do a quick manual run with your port from step 3:

```bash
sudo /opt/zfs-led-monitor/venv/bin/python /opt/zfs-led-monitor/zfs_status_sender.py \
    --port /dev/serial/by-id/<your-arduino-id>
```

After about 2 seconds it prints `Connected to ...` and a status line, and the matching LED lights. Press Ctrl-C to stop. The LEDs start cycling 30 seconds later, which is expected.

### 5. Install the systemd service

Copy the unit file into place and open it:

```bash
sudo cp systemd/zfs-led-monitor.service /etc/systemd/system/
sudo nano /etc/systemd/system/zfs-led-monitor.service
```

Replace `YOUR-ARDUINO-ID` in the `ExecStart` line with the name from step 3, then save. Enable and start the service:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now zfs-led-monitor
systemctl status zfs-led-monitor
```

You should see `active (running)`, and the green LED should light if your pools are healthy.

### 6. Install the ZED zedlet

```bash
sudo install -o root -g root -m 755 zed/all-led-monitor.sh /etc/zfs/zed.d/
sudo systemctl restart zfs-zed
```

> [!IMPORTANT]
> ZED silently skips zedlets that aren't owned by root or are writable by other users. The `install` command above sets both correctly.

Setup is complete. Run through [Testing](#testing) to confirm everything works.

## Configuration

Options are set on the `ExecStart` line in `/etc/systemd/system/zfs-led-monitor.service`:

| Option | Default | Description |
| --- | --- | --- |
| `--port` | `/dev/ttyACM0` | Serial port of the Arduino |
| `--pool` | all pools | Monitor only this pool |
| `--interval` | `10` | Seconds between heartbeat checks. Keep this below 30, or the Arduino starts cycling between checks |
| `--baud` | `9600` | Must match `BAUD_RATE` in the sketch |

After editing, reload and restart:

```bash
sudo systemctl daemon-reload
sudo systemctl restart zfs-led-monitor
```

## Testing

These tests use a throwaway pool built from two files, so your real pools are never touched. Keep the log open in a second terminal:

```bash
journalctl -u zfs-led-monitor -f
```

The log prints a line only when the colour changes, along with what triggered the check: `startup`, `heartbeat` or `zed event`.

1. **Host-died behaviour.** Stop the service and wait about 30 seconds. The LEDs should start cycling. Start it again and green returns within a couple of seconds.

    ```bash
    sudo systemctl stop zfs-led-monitor
    sudo systemctl start zfs-led-monitor
    ```

2. **Create a test pool.**

    ```bash
    sudo truncate -s 256M /tmp/zt1 /tmp/zt2
    sudo zpool create ledtest mirror /tmp/zt1 /tmp/zt2
    ```

3. **Degraded state and the ZED trigger.** Yellow should light within about a second, and the log should show `Y: ledtest is DEGRADED (trigger: zed event)`. Bringing the disk back online returns it to green.

    ```bash
    sudo zpool offline ledtest /tmp/zt2
    sudo zpool online ledtest /tmp/zt2
    ```

4. **Checksum errors on an ONLINE pool.** Once the scrub finds the damage, yellow lights and the log says `errors reported by 'zpool status -x'`. Clearing the errors turns it green again.

    ```bash
    sudo dd if=/dev/urandom of=/ledtest/junk bs=1M count=100
    sudo dd if=/dev/urandom of=/tmp/zt2 bs=1M seek=10 count=50 conv=notrunc
    sudo zpool scrub ledtest
    sudo zpool clear ledtest
    ```

5. **Red LED.** There's no safe way to force a real FAULTED pool, so send `R` directly. The service replaces it with the real state within 10 seconds.

    ```bash
    echo R > /dev/serial/by-id/<your-arduino-id>
    ```

6. **Clean up.**

    ```bash
    sudo zpool destroy ledtest
    sudo rm /tmp/zt1 /tmp/zt2
    ```

> [!TIP]
> If you set `--pool` in the service, the test pool isn't monitored. Remove the option while testing, or temporarily set it to `--pool ledtest`.

## Updating

**Update the script.** Get the latest files, copy the script into place and restart the service. Your service settings are kept, because the service file isn't touched.

```bash
git pull    # or re-download with curl as in step 1
sudo install -m 755 host/zfs_status_sender.py /opt/zfs-led-monitor/
sudo systemctl restart zfs-led-monitor
```

**Update the zedlet.** Only needed if `zed/all-led-monitor.sh` changed:

```bash
sudo install -o root -g root -m 755 zed/all-led-monitor.sh /etc/zfs/zed.d/
sudo systemctl restart zfs-zed
```

**Update pyserial.**

```bash
sudo /opt/zfs-led-monitor/venv/bin/pip install --upgrade pyserial
sudo systemctl restart zfs-led-monitor
```

**Rebuild the venv after a major Python upgrade.** A distro upgrade that changes the Python version (for example 3.11 to 3.12) can break the venv, and the service will fail to start. Recreate it:

```bash
sudo rm -rf /opt/zfs-led-monitor/venv
sudo python3 -m venv /opt/zfs-led-monitor/venv
sudo /opt/zfs-led-monitor/venv/bin/pip install pyserial
sudo systemctl restart zfs-led-monitor
```

**Re-flash the Arduino.** Stop the service first, because it holds the serial port and the upload will fail:

```bash
sudo systemctl stop zfs-led-monitor
# upload from the Arduino IDE
sudo systemctl start zfs-led-monitor
```

## Troubleshooting

Start with `journalctl -u zfs-led-monitor -n 50`; most problems show up there.

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| An LED never lights, even during the lamp test | LED in backwards or loose wiring | Long leg to the resistor side, short leg to GND |
| LEDs keep cycling while the service is active | Wrong `--port`, or the Arduino IDE's Serial Monitor is holding the port | Look for `Serial error` in the log, check `ls -l /dev/serial/by-id/`, close the Serial Monitor |
| Service fails with `No module named 'serial'` | `ExecStart` uses the system Python, or pyserial isn't in the venv | Check `ExecStart` starts with `/opt/zfs-led-monitor/venv/bin/python`; rerun the pip install from step 4 |
| Service fails with `No such file or directory` for the venv Python | The venv is missing or broken, often after a Python upgrade | Rebuild the venv (see [Updating](#updating)) |
| Red LED with `zpool list failed` in the log | `zpool` can't run or isn't permitted | Run the manual command from step 4 with `sudo` to see the full error |
| Changes take up to 10 seconds and the log says `heartbeat` | The zedlet isn't firing | Check it's owned by root with mode 755, restart `zfs-zed`, and watch `journalctl -u zfs-zed -f` while repeating test 3 |
| Arduino IDE upload fails with the port busy | The service holds the serial port | Stop the service, upload, then start it again |

## Uninstall

```bash
sudo systemctl disable --now zfs-led-monitor
sudo rm /etc/systemd/system/zfs-led-monitor.service
sudo systemctl daemon-reload
sudo rm /etc/zfs/zed.d/all-led-monitor.sh
sudo systemctl restart zfs-zed
sudo rm -rf /opt/zfs-led-monitor
```
