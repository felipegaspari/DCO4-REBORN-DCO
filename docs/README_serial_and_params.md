## Serial & Parameter Protocol – Usage Guide

Shared **inner** serial + parameter infrastructure for DCO4-family MCUs. On this board
the only peer UART is Serial2 ↔ **STM32 Mainboard**, which relays panel traffic to and
from the Input board; USB CDC (`ENABLE_USB_CONTROL`) speaks the same inner frames for
[`tools/dco_control`](../tools/dco_control/README.md). Topology:
[`SYSTEM_OVERVIEW.md`](SYSTEM_OVERVIEW.md). Preset protocol:
[`PRESET_STORE.md`](PRESET_STORE.md).

---

## 1. Headers

| Header | Role |
|--------|------|
| `params_def.h` | Canonical `enum ParamId : uint16_t` (wire id is still `uint8`); one superset shared byte-for-byte by every board |
| `param_router.h` | `ParamDescriptorT` table + O(1) jump dispatch |
| `serial_input_protocol.h` | Command bytes + payload sizes (`'a'`–`'d'`, `'p'`, `'q'`, `'x'`, `'B'`, `'C'`, `'N'`, `'O'`, `'L'`). Values are shared across boards; each copy is trimmed to what that board parses or sends (DCO's copy = Mainboard's, byte for byte) |
| `serial_param_protocol.h` | LE encode/decode for `'p'` / `'w'` / `'x'` |
| `serial_frame.h` | Buffer COBS encode/decode + `serial_frame_stuff` / `unstuff` / `write()`. Default RAW; `#define SERIAL_FRAMING_COBS` wraps `COBS(inner)+0x00` |
| `serial_parser.h` | Non-blocking parser: 256-entry cmd LUT, idle timeout (`SERIAL_FRAME_TIMEOUT_US` = 500 µs), drain budget 64. RAW fixed-length or COBS accumulate-until-`0x00` |
| `serial_protocol.h` | Mainboard ↔ DCO command set on top of `serial_input_protocol.h`: `'n'`/`'o'`/`'e'` DCO→MB, `'m'`/`'t'` MB→DCO |

**Two LUTs:** `mainboardSerialCommands[]` serves Serial2 (Mainboard-origin frames plus the panel frames the Mainboard relays, including `'N'`), `inputSerialCommands[]` serves the USB CDC bench link (including `'B'`/`'C'`). They overlap but are not the same table — a command registered in only one is unreachable from the other transport.

**USB bench link:** `serial_usb_task()` uses a second `SerialParserContext`. Only host → DCO is framed; DCO → host is plain debug text. Serial2 and USB CDC drain on Core 0 `timer1msFlag` (~1 ms). USB/DIN MIDI still runs every `loop()`. CDC drain is skipped when the host has not opened `Serial`.

**MIDI CC:** `midi_cc_apply()` writes ADSR/filter block globals directly (`CC_LOCAL_*`). Everything else, including `PARAM_PW_VALUE` and `PARAM_ADSR1_TO_VCA`, goes through `update_parameters()`. Persistable ParamId CCs also `serial_echo_persistable_param16()` so the panel display — and the next DCO preset save — match what you hear. `'a'`–`'d'` domains are mirrored by their own block helpers instead.

---

## 2. Inner frames (handlers always see this)

Little-endian multi-byte fields. No finish byte. `0x00` is reserved (COBS delimiter) and is never a command.

| Cmd | Payload | Meaning |
|-----|---------|---------|
| `'a'`/`'b'`/`'c'` | 8 | ADSR A,D,S,R as `uint16`. A/D/R exp-mapped 0..25000; S linear. Direct globals + dirty flags — **not** `update_parameters` |
| `'d'` | 8 | `CUTOFF`, `RESONANCE`, `ADSR2toVCF`, `LFO2toVCF` + scale bake |
| `'p'` | 3 | `[id:u8][value:i16]` → `update_parameters`. Also the outbound persistable mirror (USB/MIDI only; never Serial2 ingress) |
| `'q'` | 16 | Preset name, 16 ASCII chars; staged for the next preset save |
| `'x'` | 5 | DCO→MB `[id:u8][value:u32 LE]` (gap 154 / cal 155); `serial_frame_write` |
| `'B'` | 36 | Host bulk chunk `[target][slot][offset:u16][32 data]` ([`PRESET_STORE.md`](PRESET_STORE.md)) |
| `'C'` | 8 | Host bulk commit `[target][slot][size:u16][crc32]` |
| `'N'` | 1 | Input→DCO preset directory request; the single byte is padding (see below) |
| `'O'` | 17 | DCO→Input `[slot:u8][name:16]`, one directory entry, 256 per `'N'` |
| `'L'` | 1 | DCO→Input `[slot:u8]`, "this slot just finished loading" |

`'N'` carries a padding byte rather than an empty payload because `serial_parser_dispatch()` / `serial_parser_process_byte()` treat `payload_len == 0` as an unregistered command, so a zero-length frame would never dispatch. `'O'`/`'L'` are TX-only on this board and therefore have no `serial_input_payload_len()` row here.

On-wire **default** = inner (RAW). **`#define SERIAL_FRAMING_COBS`:** `COBS(cmd+payload) + 0x00`. Handlers still see inner only. Host A/B: `dco_control --cobs` or `DCO_SERIAL_COBS=1` (must match firmware or controls look ignored). Codec is buffer-in/buffer-out so a later SPI link can reuse `serial_frame_unstuff` after reading until `0x00`.

**Every board speaks the same slim inner protocol.** Flash them together. `SERIAL_INNER_MAX_PAYLOAD` defaults to 8; `Serial.h` raises it to **36** on the DCO for `'B'`, Input/Screen use 17 for Screen `'q'` scroll. Screen-only cmds (`'w'`/`'y'`/`'s'`, defined in the Screen's own `screen_mode.h`) never reach the DCO. Former `'e'`/`'f'` are `'p'` ids 222 / 210. `SERIAL_FRAMING_COBS` must match on all boards (commented = RAW).

---

## 3. Adding a parameter (`ParamId`)

1. Append a new id in the shared `params_def.h` master (`DCO3-MONOSYNTH/DCO/params_def.h`) and copy it out to every board (do not renumber; stay ≤ 255 on the wire).
2. Implement `apply_param_*` and add one row to `paramTable[]` in `params.ino`.
3. Call `init_param_router()` at boot (already in `setup()`).
4. Send `'p'` + id + i16 LE. Host: one `Param` row in `tools/dco_control/params.py`.
5. For the id to be reachable from the panel, add an applier row to the Mainboard's `paramTable[]` that calls `forward_dco()`. Panel `'p'` is applied and re-emitted per ParamId there, not relayed as raw bytes.

No new serial command unless it is a new **1 ms analog block**.

---

## 4. Adding a serial command (rare)

1. Add the cmd + payload length to `serial_input_protocol.h` (`serial_input_payload_len`), in the shared master copy, then copy the header out to every board.
2. Implement `on_frame` and add a `SerialCommandDef` row to the right table in `Serial.ino` (`mainboardSerialCommands[]` for Serial2, `inputSerialCommands[]` for USB, or both).
3. **Register it in the Mainboard relay too** — `inputSerial8Commands[]` for Input→DCO, `mainSerial2Commands[]` for DCO→Input, in [`../../MAINBOARD-CONTROLLER/Serial.ino`](../../MAINBOARD-CONTROLLER/Serial.ino). Nothing crosses that board implicitly; an unregistered byte is dropped in transit with no error at either end.
4. Payload length must be ≥ 1: a zero-length payload is indistinguishable from an unregistered command in the parser. Use one padding byte, as `'N'` does.
5. Keep `0x00` unused. Prefer LE. Send via `serial_frame_write()`, not ad-hoc UART bytes.

---

## 5. Parser notes

- O(1) lookup: `serial_command_table_init()` fills `payload_len[256]` / `on_frame[256]`.
- Timeout (`SERIAL_FRAME_TIMEOUT_US` = 500 µs) runs only when mid-frame **and** the stream is idle — no `micros()` per byte. COBS: mid-frame means stuffed bytes received, delimiter not yet seen.
- RAW: cmd LUT + fixed payload. COBS: accumulate until `0x00`, decode, unpack, then LUT length check.
- Drain snapshots `available()` once, then `read()`s up to `SERIAL_DRAIN_BYTE_BUDGET` (64) so one 1 ms Input burst (incl. COBS ~11 B/block) finishes in a single tick.

---

## 6. New MCU checklist

1. Copy `params_def.h`, `param_router.h`, `serial_input_protocol.h`, `serial_param_protocol.h`, `serial_frame.h`, `serial_parser.h`.
2. `paramTable[]` + `init_param_router()` + `update_parameters(uint16_t, int16_t)`.
3. Per UART: `SerialCommandDef[]` → LUT, `serial_parser_drain()`, `serial_frame_write()` for TX.
4. Keep ParamIds and inner layouts identical across MCUs.
