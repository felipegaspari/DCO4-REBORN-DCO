"""Skip picotool verify, and bind upload/monitor to this board's USB by-id name.

PlatformIO Auto port matches any Pico VID/PID (all TinyUSB Picos look the same
on ttyACM*). custom_usb_by_id in platformio.ini is the product-string symlink.
"""

import glob
import os
import sys

Import("env")

env.Replace(
    UPLOADERFLAGS=[
        f for f in env.get("UPLOADERFLAGS", []) if f not in ("-v", "--verify")
    ]
)

_BY_ID = env.GetProjectOption("custom_usb_by_id", "")


def _matches():
    return sorted(glob.glob(_BY_ID)) if _BY_ID else []


def _fail_no_port(matches):
    sys.stderr.write("ERROR: need exactly one USB device matching:\n  %s\n" % _BY_ID)
    if matches:
        sys.stderr.write("Found %d:\n" % len(matches))
        for path in matches:
            sys.stderr.write("  %s -> %s\n" % (path, os.path.realpath(path)))
        sys.stderr.write("Unplug the extra board (a wrong upload can rename another Pico).\n")
    else:
        sys.stderr.write("Connected by-id ports:\n")
        listed = sorted(glob.glob("/dev/serial/by-id/usb-*"))
        if not listed:
            sys.stderr.write("  (none)\n")
        for path in listed:
            sys.stderr.write("  %s -> %s\n" % (path, os.path.realpath(path)))
    env.Exit(1)


def bind_usb_port(source=None, target=None, env=None):
    env = env or globals()["env"]
    if not _BY_ID:
        return
    matches = _matches()
    if len(matches) != 1:
        _fail_no_port(matches)
    by_id = matches[0]
    port = os.path.realpath(by_id)
    print("USB port: %s -> %s" % (os.path.basename(by_id), port))
    env.Replace(UPLOAD_PORT=port, MONITOR_PORT=by_id)


if _BY_ID and len(_matches()) == 1:
    bind_usb_port()

env.AddPreAction("upload", bind_usb_port)
env.AddPreAction("uploadfs", bind_usb_port)
