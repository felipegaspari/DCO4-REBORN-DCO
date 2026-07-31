# DCO bench controller

Control the DCO board from Linux over one USB cable, with no Input board and no Screen
attached.

Parameters go out over the board's USB serial port. **Notes do not go through this tool** —
the DCO already enumerates as a USB MIDI device called `DCO3-MONO`, so play it from a MIDI
keyboard, VMPK, or anything else that can reach an ALSA MIDI port.

---

## 1. Firmware requirement

The board must be built with `ENABLE_USB_CONTROL`, which is on by default in
[`DCO.ino`](../../DCO.ino). It makes the USB serial port accept the same frames
the Input board sends on its UART, using the same handler table, so nothing about the
board's behaviour changes — it just gains a second place those frames can arrive from.

The sketch also needs the TinyUSB stack, which it already required for USB MIDI. From the
DCO sketch folder (the parent of `tools/`):

```bash
arduino-cli compile --fqbn rp2040:rp2040:rpipico2:usbstack=tinyusb \
  --libraries _build_libs .
```

Without `usbstack=tinyusb` the build fails with
`#error TinyUSB is not selected, please select it in "Tools->Menu->USB Stack"`.

Leave `ENABLE_USB_CONTROL` commented out for production. While it is on, any stray
`a`-`f`, `p`, `w` or `q` byte typed into a serial terminal is read as a frame header.

## 2. Install

`tkinter` ships with Python. Only `pyserial` is extra:

```bash
sudo pacman -S python-pyserial      # Arch / CachyOS
# or, in a virtualenv:
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
```

## 3. Run

```bash
python3 app.py                      # auto-detects the DCO3-MONO port
python3 app.py --port /dev/ttyACM0  # or name it explicitly
python3 app.py --theme light        # dark is the default
```

Pick the port, press **Connect**, then press **Send all**. That last step matters: the
board boots with its own defaults and has no idea what the window is showing, so nothing
is in sync until you push it once.

### If the port will not open

- **Permissions.** You need to be in the group that owns `/dev/ttyACM*` — `uucp` on Arch
  and CachyOS, `dialout` on Debian and Ubuntu. Check with `ls -l /dev/ttyACM0`, then
  `sudo usermod -aG uucp $USER` and log out and back in.
- **ModemManager.** It probes new ACM devices on connect and can hold the port for a few
  seconds or grab it outright. If it is the culprit, `systemctl stop ModemManager`, or
  add a udev rule with `ID_MM_DEVICE_IGNORE="1"` for the board's USB IDs.
- **No `/dev/ttyACM*` at all.** The board is not running a TinyUSB build, or it is in
  BOOTSEL mode. Re-flash with the `usbstack=tinyusb` FQBN above.

## 4. Layout

Eight tabs, all generated from the tables in [`params.py`](params.py):

| Tab | Contents |
|-----|----------|
| Oscillators | Intervals, detune, SQR and sub levels, wave enables |
| Sync and PIO | Sync mode, soft sync, sub-osc divide, phase align, diagnostics |
| Envelopes | The three envelope time blocks, curves, ADSR3 routing |
| Filter | Cutoff, resonance, envelope and LFO amounts, keytrack |
| PWM | Pulse width, LFO2 and envelope to PW |
| LFOs | Waveforms, speeds, and the LFO routing depths |
| Voice and Drift | Voice mode, unison, portamento, analog drift, VCA |
| Calibration | Autotune trigger, manual calibration stage and offsets |

Anything the board prints — the topology report, `DCO_DEBUG_REPORT` output, autotune
progress — lands in the **Board output** pane at the bottom. Lines this tool writes itself
are prefixed `[link]`, `[send]` or `[ui]` and tinted, so they are easy to tell from the
board's own output.

### Appearance

Dark by default. The **Light** / **Dark** button in the toolbar switches in place, and
`--theme light` starts that way; the choice is not remembered between runs, so use a shell
alias if you always want light.

The colours are painted onto ttk's built-in `clam` theme, which means **no extra package is
needed**. If [`sv-ttk`](https://pypi.org/project/sv-ttk/) happens to be installed it is used
instead for a more modern look and real toggle switches on the boolean parameters:

```bash
pip install sv-ttk        # entirely optional, detected automatically
```

Everything lives in [`theme.py`](theme.py). Note that ttk styling only reaches ttk widgets,
so that module also recolours the plain-Tk parts by hand — each tab's scroll canvas, the log
pane, the tooltips and the combobox dropdown — reading the colours back out of the live
style so the same code works under either backend.

## 5. Testing the PIO sync work

The **Sync and PIO** tab is the reason this tool exists. `PARAM_SOFT_SYNC` (36) and
`PARAM_SUBOSC_DIVIDE` (37) have no Input-board UI at all, so the panel cannot reach them;
this is the only way to exercise them. See
[`DCO/docs/PIO_OSCILLATORS.md`](../../docs/PIO_OSCILLATORS.md) for what they do.

The three **Diagnostics** buttons call the bench helpers documented in section 12 of that
file, which nothing else in the firmware invokes:

- **PIO topology report** verifies every oscillator reset pin still reads back as PIO0.
  Expect the summary line `reset pin ownership: OK (all PIO0)`. Anything else means a pin
  has been stolen and hard sync cannot work, whatever it sounds like. This also doubles as
  an end-to-end link test: press it, see text come back.
- **Period probe** parks OSC1 at a fixed divider and prints the predicted period, to
  compare against a frequency counter on the reset pin. Run both probes and feed the two
  readings to `pio_solve_period_model()` to confirm the weight and overhead constants.
  **Only works with no note playing** — `voice_task()` pushes a fresh divider every frame
  for a held note, which immediately overwrites what the probe set.

For the hard-sync listening check: set **Sync mode** to 1 or 2, leave **Soft sync** off,
hold a note, and sweep the slave's detune. A timbral formant sweep means sync is working.
If the pitch simply tracks with no change in character, the slave is being cloned rather
than synced.

## 6. Files

| File | Role |
|------|------|
| [`protocol.py`](protocol.py) | Frame builders, the envelope lin-to-exp curve, port detection |
| [`params.py`](params.py) | The whole control surface as data; edit this to add a control |
| [`app.py`](app.py) | tkinter UI generated from `params.py` |
| [`theme.py`](theme.py) | Dark and light palettes, ttk styling, plain-Tk recolouring |

### Adding a parameter

Add the ID to [`DCO/params_def.h`](../../params_def.h) and an `apply_param_*` entry to
`paramTable[]` in [`DCO/params.ino`](../../params.ino) as usual, then add one `Param`
row to `params.py`. The UI picks it up with no changes to `app.py`.

## 7. Protocol notes

Frames are exactly what the Input board sends, per
[`DCO/serial_input_protocol.h`](../../serial_input_protocol.h): one command byte, then
a fixed-length big-endian payload. `'p'` and `'w'` end in a finish byte of 1; the `'a'`-`'f'`
block frames do not.

Two details that are easy to get wrong:

- **Everything is sent as `'p'` (16-bit), even parameters the Input board sends as `'w'`.**
  `'w'` carries a signed 8-bit value, so portamento 200 arrives as -56 and only comes out
  right because the apply function casts back through `uint8_t`. A `'p'` frame with the
  true positive value lands on the same result without relying on that round trip.
- **Envelope attack, decay and release are exp-mapped on the wire** (0..25000), while
  sustain is linear (0..4095). `protocol.lin_to_exp()` replicates
  `linearToExponential(v, 50, 25000)` from the Input board so a slider here feels like the
  physical fader.
