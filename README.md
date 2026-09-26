# ZFS Towerlight

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

## Files

| File | Purpose | Where it goes |
| --- | --- | --- |
| `zfs_status_sender.py` | Status sender | Stays in `~/zfs-towerlight`, next to its venv |
| `zfs-led-monitor.service` | systemd unit | `/etc/systemd/system/` |
| `all-led-monitor.sh` | ZED zedlet | `/etc/zfs/zed.d/` |
| `zfs_monitor.ino` | Arduino sketch | Flashed to the Arduino |

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

Cloning the repo puts all the files in `~/zfs-towerlight`. The Python script stays there, and its virtual environment is created alongside it in `~/zfs-towerlight/venv`. The service file and the zedlet are copied to their system locations in steps 5 and 6.

They're copied rather than moved so the clone stays complete, which keeps `git pull` working when you update. Run the commands below as your normal user; they use `sudo` only where root is needed.

### 1. Clone the repo

On the ZFS host, clone the repo into your home directory:

```bash
git clone https://github.com/Millzie21/zfs-towerlight.git ~/zfs-towerlight
cd ~/zfs-towerlight
```

If `git` isn't installed, install it first with `sudo apt install git` (Debian, Ubuntu and Proxmox).

> [!NOTE]
> If the repo is private, git asks for a username and password. GitHub no longer accepts account passwords here, so use a [personal access token](https://github.com/settings/tokens) as the password.

Run all the remaining host commands from inside `~/zfs-towerlight`.

### 2. Flash the Arduino

On the computer with the Arduino IDE, download the repo from GitHub (**Code → Download ZIP**) and extract it, or copy `zfs_monitor.ino` over from the clone. Open `zfs_monitor.ino` in the Arduino IDE.

The IDE requires each sketch to be in a folder with the same name, so it will offer to create a `zfs_monitor` folder and move the file into it. Click **OK**, then upload the sketch to the board.

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

### 4. Create the virtual environment

Install the venv module. This is only needed on Debian, Ubuntu and Proxmox; most other distros include it already:

```bash
sudo apt install python3-venv
```

Create the venv inside the repo folder and install pyserial into it. No `sudo` is needed here:

```bash
python3 -m venv ~/zfs-towerlight/venv
~/zfs-towerlight/venv/bin/pip install pyserial
```

You never need to activate the venv. The service calls `~/zfs-towerlight/venv/bin/python` directly.

Do a quick manual run with your port from step 3. This one needs `sudo`, because querying `zpool` and opening the serial port usually require root:

```bash
sudo ~/zfs-towerlight/venv/bin/python ~/zfs-towerlight/zfs_status_sender.py \
    --port /dev/serial/by-id/<your-arduino-id>
```

After about 2 seconds it prints `Connected to ...` and a status line, and the matching LED lights. Press Ctrl-C to stop. The LEDs start cycling 30 seconds later, which is expected.

### 5. Install the systemd service

Copy the service file from the clone to `/etc/systemd/system/` and open it:

```bash
sudo cp zfs-led-monitor.service /etc/systemd/system/
sudo nano /etc/systemd/system/zfs-led-monitor.service
```

The `ExecStart` line has two placeholders to replace:

- **`YOUR-USERNAME`** appears twice. Replace both with your username, so the paths point at your home directory. If you're not sure of the path, run `echo $HOME`.
- **`YOUR-ARDUINO-ID`** is the serial port name from step 3.

For example, for the user `alice`, the finished line looks like this:

```ini
ExecStart=/home/alice/zfs-towerlight/venv/bin/python /home/alice/zfs-towerlight/zfs_status_sender.py --port /dev/serial/by-id/usb-Arduino__www.arduino.cc__0043_75630313536351F0B1A1-if00 --interval 10
```

Save with Ctrl-O and Enter, then exit with Ctrl-X.

Enable and start the service:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now zfs-led-monitor
systemctl status zfs-led-monitor
```

You should see `active (running)`, and the green LED should light if your pools are healthy.

> [!NOTE]
> The service runs as root but executes files in your home directory. Anything that can write to your home folder could change the script and have it run as root. On a single-user home server that's usually an acceptable trade-off.

### 6. Install the ZED zedlet

```bash
sudo install -o root -g root -m 755 all-led-monitor.sh /etc/zfs/zed.d/
sudo systemctl restart zfs-zed
```

> [!IMPORTANT]
> ZED silently skips zedlets that aren't owned by root or are writable by other users, so the zedlet is copied to `/etc/zfs/zed.d/` rather than run from your home directory. The `install` command above sets the ownership and permissions correctly.

Setup is complete. Run through [Testing](#testing) to confirm everything works.

## Configuration

Options are set on the `ExecStart` line in `/etc/systemd/system/zfs-led-monitor.service`:

| Option | Default | Description |
| --- | --- | --- |
| `--port` | `/dev/ttyACM0` | Serial port of the Arduino |
| `--pool` | all pools | Monitor only this pool |
| `--interval` | `10` | Seconds between heartbeat checks. Keep this below 30, or the Arduino starts cycling between checks |
| `--baud` | `9600` | Must match `BAUD_RATE` in the sketch |

Edit it with `sudo nano /etc/systemd/system/zfs-led-monitor.service`, then reload and restart:

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
    echo R | sudo tee /dev/serial/by-id/<your-arduino-id>
    ```

6. **Clean up.**

    ```bash
    sudo zpool destroy ledtest
    sudo rm /tmp/zt1 /tmp/zt2
    ```

> [!TIP]
> If you set `--pool` in the service, the test pool isn't monitored. Remove the option while testing, or temporarily set it to `--pool ledtest`.

## Updating

**Update the script.** The service runs the script straight from `~/zfs-towerlight`, so updating is just downloading the new files and restarting. The venv and your service settings are kept.

```bash
cd ~/zfs-towerlight
git pull
sudo systemctl restart zfs-led-monitor
```

**Update the service file.** Only needed if `zfs-led-monitor.service` changed, because systemd uses its own copy in `/etc/systemd/system/`. Copy it over again and redo your edits from step 5. Copying overwrites the installed file, so note your `ExecStart` line first. Then:

```bash
sudo systemctl daemon-reload
sudo systemctl restart zfs-led-monitor
```

**Update the zedlet.** Only needed if `all-led-monitor.sh` changed, because ZED runs its own copy in `/etc/zfs/zed.d/`:

```bash
sudo install -o root -g root -m 755 ~/zfs-towerlight/all-led-monitor.sh /etc/zfs/zed.d/
sudo systemctl restart zfs-zed
```

**Update pyserial.**

```bash
~/zfs-towerlight/venv/bin/pip install --upgrade pyserial
sudo systemctl restart zfs-led-monitor
```

**Rebuild the venv after a major Python upgrade.** A distro upgrade that changes the Python version (for example 3.11 to 3.12) can break the venv, and the service will fail to start. Recreate it:

```bash
rm -rf ~/zfs-towerlight/venv
python3 -m venv ~/zfs-towerlight/venv
~/zfs-towerlight/venv/bin/pip install pyserial
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
| Service fails with `No such file or directory` | A placeholder is still in the service file, or the venv is missing | Check the `ExecStart` line has your real home path and port; recreate the venv if `~/zfs-towerlight/venv` is missing |
| Service fails with `No module named 'serial'` | pyserial isn't installed in the venv | Rerun `~/zfs-towerlight/venv/bin/pip install pyserial` |
| Service fails after a distro upgrade | Python version changed and broke the venv | Rebuild the venv (see [Updating](#updating)) |
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
rm -rf ~/zfs-towerlight
```
