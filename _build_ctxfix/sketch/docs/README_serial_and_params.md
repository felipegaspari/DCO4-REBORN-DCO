#line 1 "/home/felipe/Documentos/DCO4-REBORN/DCO/docs/README_serial_and_params.md"
## Serial & Parameter Protocol – DCO (DCO4-REBORN)

The wire format, the command table, the parser and the `ParamId` enum are shared
by every board and documented once in
[`DCO-PROTOCOL/README.md`](../../DCO-PROTOCOL/README.md). The headers come from
that library through `_build_libs/DCO-PROTOCOL`; there is no copy in this sketch
folder any more.

This page covers only what is specific to the DCO4 DCO. Topology:
[`SYSTEM_OVERVIEW.md`](SYSTEM_OVERVIEW.md). Preset protocol:
[`PRESET_STORE.md`](PRESET_STORE.md).

---

## Links

The only peer UART is Serial2 ↔ **STM32 Mainboard**, which relays panel traffic
to and from the Input board. USB CDC (`ENABLE_USB_CONTROL`) speaks the same inner
frames for [`DCO-CONTROL-PANEL`](../../DCO-CONTROL-PANEL/README.md).

`serial_protocol.h` (still board-local, not part of the shared library) adds the
Mainboard ↔ DCO command set on top: `'n'`/`'o'`/`'e'` DCO→MB, `'m'`/`'t'` MB→DCO.

**Two LUTs.** `mainboardSerialCommands[]` serves Serial2 — Mainboard-origin
frames plus the panel frames the Mainboard relays, including `'N'` — while
`inputSerialCommands[]` serves the USB CDC bench link, including `'B'`/`'C'`.
They overlap but are not the same table: a command registered in only one is
unreachable from the other transport.

`serial_usb_task()` uses a second `SerialParserContext`. Only host → DCO is
framed; DCO → host is plain debug text. Serial2 and USB CDC drain on Core 0
`timer1msFlag` (~1 ms), while USB/DIN MIDI runs every `loop()`. The CDC drain is
skipped when the host has not opened `Serial`.

`SERIAL_INNER_MAX_PAYLOAD` is **36** here (set in `Serial.h`) for the `'B'` bulk
chunk; Input and Screen use 17. Screen-only commands (`'w'`, `'y'`, `'s'`, defined
in the Screen's own `screen_mode.h`) never reach this board. The former `'e'`/`'f'`
commands are now `'p'` ids 222 / 210.

## Board-specific frame handling

- `'a'`/`'b'`/`'c'`/`'d'` write the ADSR and filter block globals directly and set
  dirty flags — they do **not** go through `update_parameters()`. A USB-origin
  block is re-sent to the Mainboard (`serial_forward_input_block_to_mb`, gated on
  `g_param_ingress`), which applies the analog ones and relays all of them to
  Input so the panel and the Screen follow a host edit.
- `'p'` is both ingress and the outbound persistable mirror (the mirror covers
  USB/MIDI edits only, never Serial2 ingress).
- `'q'` stages the name for the next preset save.
- `'O'`/`'L'` are TX-only on this board.

## MIDI CC

`midi_cc_apply()` writes the ADSR/filter block globals directly (`CC_LOCAL_*`).
Everything else, including `PARAM_PW_VALUE` and `PARAM_ADSR1_TO_VCA`, goes
through `update_parameters()`. Persistable ParamId CCs also call
`serial_echo_persistable_param16()` so the panel display, and the next preset
save, match what you hear. The `'a'`–`'d'` domains have no ParamId to echo, so
they are mirrored by their own block helpers instead — including `'c'`, whose
engine is DCO-local but whose faders live on the panel.

## Adding a parameter here

The generic steps are in the [shared
guide](../../DCO-PROTOCOL/README.md#adding-a-parameter). Two extra steps apply on
this instrument:

1. Implement `apply_param_*`, add a row to `paramTable[]` in `params.ino`, and
   rely on `init_param_router()` already being called from `setup()`.
2. **For the id to be reachable from the panel, add an applier row to the
   Mainboard's `paramTable[]` that calls `forward_dco()`.** Panel `'p'` is applied
   and re-emitted per ParamId there, not relayed as raw bytes.

## Adding a serial command here

Beyond the [shared checklist](../../DCO-PROTOCOL/README.md#adding-a-serial-command-rare):

- Add the `SerialCommandDef` row to the right table in `Serial.ino`
  (`mainboardSerialCommands[]` for Serial2, `inputSerialCommands[]` for USB, or
  both).
- **Register it in the Mainboard relay too** — `inputSerial8Commands[]` for
  Input→DCO and `mainSerial2Commands[]` for DCO→Input, in
  [`../../MAINBOARD-CONTROLLER/Serial.ino`](../../MAINBOARD-CONTROLLER/Serial.ino).
  Nothing crosses that board implicitly; an unregistered byte is dropped in
  transit with no error at either end.
