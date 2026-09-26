#!/bin/sh
# ZED zedlet: triggered on every ZFS event; tells the LED monitor to re-check now.
# The check is cheap, and bursts of events are merged into a single re-check.

systemctl kill -s USR1 zfs-led-monitor.service 2>/dev/null
exit 0
