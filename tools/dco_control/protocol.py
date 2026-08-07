"""Frame builders for the DCO board's Input-panel serial protocol.

The DCO accepts these frames on its USB CDC port when the firmware is built with
ENABLE_USB_CONTROL (see DCO/Serial.ino). They are byte-identical to what the Input
board sends over its 2.5 Mbaud UART, so the board's handlers do not know or care
which link a frame arrived on.

Frame layout is one command byte followed by a fixed-length payload, per
DCO/serial_input_protocol.h. All multi-byte fields are big-endian. The 'p'/'w'
parameter frames end in a "finish" byte of 1; the 'a'-'f' block frames do not.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

# Command bytes, from DCO/serial_input_protocol.h.
CMD_ADSR1_BLOCK = b"a"  # EnvVCA times
CMD_ADSR2_BLOCK = b"b"  # EnvVCF times
CMD_ADSR3_BLOCK = b"c"  # EnvDCO times (pitch / PW)
CMD_FILTER_BLOCK = b"d"
CMD_ADSR1_TO_VCA = b"e"
CMD_PW_VALUE = b"f"
CMD_PARAM_16 = b"p"
CMD_PARAM_8 = b"w"
CMD_PRESET_NAME = b"q"

FINISH = 1

# The USB product string set by USBDevice.setProductDescriptor() in DCO/DCO.ino.
# The descriptor is space-padded, so match on a prefix.
USB_PRODUCT_PREFIX = "DCO3-MONO"

# USB CDC ignores the line rate, but pyserial requires one. Mirror the firmware's
# Serial.begin(2000000) for symmetry.
BAUD = 2000000

# Envelope lin->exp mapping, from INPUT-CONTROLLER/auxiliary.h.
ADSR_LIN_MAX = 4095
ADSR_EXP_MAX = 25000
ADSR_EXP_BASE = 50.0


def lin_to_exp(linear: int) -> int:
    """Replicate linearToExponential(linear, 50, 25000) from the Input board.

    Envelope attack/decay/release travel the wire already exp-mapped, so applying
    the same curve here makes a host slider behave like the physical fader.
    Sustain is sent linear and must not go through this.
    """
    linear = max(0, min(ADSR_LIN_MAX, int(linear)))
    normalized = linear / float(ADSR_LIN_MAX)
    exp_value = ADSR_EXP_BASE**normalized - 1.0
    max_exp_value = ADSR_EXP_BASE - 1.0
    return int(exp_value * (ADSR_EXP_MAX / max_exp_value))


def param16(param_id: int, value: int) -> bytes:
    """'p' frame: 16-bit parameter. Negative values go out as two's complement."""
    return CMD_PARAM_16 + bytes([param_id & 0xFF]) + struct.pack(">h", _clamp16(value)) + bytes([FINISH])


def param16u(param_id: int, value: int) -> bytes:
    """'p' frame with unsigned 16-bit value (0..65535). Firmware reinterprets as uint16_t."""
    return CMD_PARAM_16 + bytes([param_id & 0xFF]) + struct.pack(">H", _clampu16(value)) + bytes([FINISH])


def param8(param_id: int, value: int) -> bytes:
    """'w' frame: 8-bit parameter. The DCO sign-extends, so -1 arrives as -1."""
    return CMD_PARAM_8 + bytes([param_id & 0xFF]) + struct.pack(">b", _clamp8(value)) + bytes([FINISH])


def adsr_block(cmd: bytes, attack: int, decay: int, sustain: int, release: int) -> bytes:
    """'a'/'b'/'c' frame. attack/decay/release are exp-domain (0..25000), sustain linear.

    Callers should pass values already run through lin_to_exp() for the three times.
    """
    return cmd + struct.pack(">HHHH", _clampu16(attack), _clampu16(decay), _clampu16(sustain), _clampu16(release))


def filter_block(cutoff: int, resonance: int, adsr2_to_vcf: int, lfo2_to_vcf: int) -> bytes:
    """'d' frame. cutoff/resonance are 0..4095; the two mod amounts are 0..512.

    The Input board inverts its cutoff and resonance pots when reading the ADC, but
    that is a pot-wiring detail: on the wire, larger simply means more.
    """
    return CMD_FILTER_BLOCK + struct.pack(
        ">HHHH", _clampu16(cutoff), _clampu16(resonance), _clampu16(adsr2_to_vcf), _clampu16(lfo2_to_vcf)
    )


def adsr1_to_vca(value: int) -> bytes:
    """'e' frame: ADSR1 -> VCA amount, 0..512."""
    return CMD_ADSR1_TO_VCA + struct.pack(">H", _clampu16(value))


def pw(value: int) -> bytes:
    """'f' frame: pulse width, 0..4095. The DCO stores it as value / 4."""
    return CMD_PW_VALUE + struct.pack(">H", _clampu16(value))


def preset_name(name: str) -> bytes:
    """'q' frame: 8 ASCII chars plus a finish byte."""
    padded = name.encode("ascii", errors="replace")[:8].ljust(8, b" ")
    return CMD_PRESET_NAME + padded + bytes([FINISH])


def _clamp16(v: int) -> int:
    return max(-32768, min(32767, int(v)))


def _clampu16(v: int) -> int:
    return max(0, min(0xFFFF, int(v)))


def _clamp8(v: int) -> int:
    return max(-128, min(127, int(v)))


@dataclass
class PortInfo:
    device: str
    description: str


def find_dco_ports() -> list[PortInfo]:
    """Return candidate serial ports, DCO-looking ones first.

    Matches the USB product descriptor the firmware advertises. Falls back to
    listing everything so a board with a stale descriptor is still reachable.
    """
    from serial.tools import list_ports

    matches: list[PortInfo] = []
    others: list[PortInfo] = []
    for p in list_ports.comports():
        info = PortInfo(p.device, p.description or "")
        product = (getattr(p, "product", None) or "") + " " + (p.description or "")
        if USB_PRODUCT_PREFIX.lower() in product.lower():
            matches.append(info)
        else:
            others.append(info)
    return matches + others
