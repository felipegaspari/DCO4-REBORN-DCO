#!/usr/bin/env python3
"""Generate a fully wired distortion.kicad_sch for the post-LP Drive+Mix stage."""

from __future__ import annotations

import json
import math
import re
import uuid
from pathlib import Path

OUT = Path(__file__).resolve().parent
LIBS = Path("/usr/share/kicad/symbols")
SCH_UUID = "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
PROJECT = "distortion"

# Pin offsets relative to symbol origin (unrotated).
PIN_R = {"1": (0.0, 3.81), "2": (0.0, -3.81)}
PIN_C = PIN_R
PIN_D = {"1": (-3.81, 0.0), "2": (3.81, 0.0)}  # 1=K, 2=A
PIN_LED = PIN_D
PIN_CONN = {"1": (-5.08, 0.0)}
# KiCad power symbols: zero-length pin at the origin.
PIN_PWR = {"1": (0.0, 0.0)}
# TL074 / LM2902 unit geometry (all amp units share the same local pin coords)
PIN_OA = {
    "1": (7.62, 0.0),
    "2": (-7.62, -2.54),
    "3": (-7.62, 2.54),
    "5": (-7.62, 2.54),
    "6": (-7.62, -2.54),
    "7": (7.62, 0.0),
    "8": (7.62, 0.0),
    "9": (-7.62, -2.54),
    "10": (-7.62, 2.54),
    "12": (-7.62, 2.54),
    "13": (-7.62, -2.54),
    "14": (7.62, 0.0),
    "4": (-2.54, 7.62),
    "11": (-2.54, -7.62),
}
PIN_AS = {
    "1": (-12.7, -15.24),  # MODE
    "2": (-12.7, 15.24),  # I_IN1
    "3": (-12.7, 12.7),  # VC1
    "4": (12.7, 15.24),  # I_OUT1
    "5": (12.7, 7.62),  # I_OUT2
    "6": (-12.7, 5.08),  # VC2
    "7": (-12.7, 7.62),  # I_IN2
    "8": (2.54, -20.32),  # GND
    "9": (0.0, -20.32),  # V-
    "10": (-12.7, 0.0),  # I_IN3
    "11": (-12.7, -2.54),  # VC3
    "12": (12.7, 0.0),  # I_OUT3
    "13": (12.7, -7.62),  # I_OUT4
    "14": (-12.7, -10.16),  # VC4
    "15": (-12.7, -7.62),  # I_IN4
    "16": (0.0, 20.32),  # V+
}

# Use DCO: lib prefix so KiCad does not replace embeds with mismatched system libs.
LIB_R = "DCO:R"
LIB_C = "DCO:C"
LIB_D = "DCO:D"
LIB_LED = "DCO:LED"
LIB_CONN = "DCO:Conn_01x01"
LIB_OA = "DCO:TL074"
LIB_AS = "DCO:AS2164"

LIB_PINS: dict[str, dict[str, tuple[float, float]]] = {
    LIB_R: PIN_R,
    LIB_C: PIN_C,
    LIB_D: PIN_D,
    LIB_LED: PIN_LED,
    LIB_CONN: PIN_CONN,
    "power:+12V": PIN_PWR,
    "power:-12V": PIN_PWR,
    "power:GND": PIN_PWR,
    "power:PWR_FLAG": PIN_PWR,
    LIB_OA: PIN_OA,
    LIB_AS: PIN_AS,
}


def uid() -> str:
    return str(uuid.uuid4())


def extract_symbol(lib_path: Path, name: str) -> str:
    text = lib_path.read_text(encoding="utf-8")
    start = text.find(f'\t(symbol "{name}"')
    if start < 0:
        raise SystemExit(f"symbol {name} not in {lib_path}")
    m = re.search(r"\n\t\(symbol \"", text[start + 10 :])
    if not m:
        raise SystemExit(f"end of {name} not found")
    return text[start : start + 10 + m.start()].rstrip() + "\n"


def embed_symbol(
    block: str,
    lib_id: str,
    src_name: str,
    value: str | None = None,
    nested_name: str | None = None,
) -> str:
    nest = nested_name or src_name
    block = re.sub(
        rf'\(symbol "{re.escape(src_name)}"',
        f'(symbol "{lib_id}"',
        block,
        count=1,
    )
    if nest != src_name:
        block = block.replace(f'(symbol "{src_name}_', f'(symbol "{nest}_')
    if value is not None:
        block = re.sub(
            r'\(property "Value" "[^"]*"',
            f'(property "Value" "{value}"',
            block,
            count=1,
        )
    block = re.sub(r'\n\t\t\(extends "[^"]*"\)', "", block)
    return block


def unit_pins(embedded_block: str, lib_id: str, unit: int) -> list[str]:
    short = lib_id.split(":", 1)[1]
    for base in (short, lib_id):
        pat = rf'\(symbol "{re.escape(base)}_{unit}_\d+"'
        m = re.search(pat, embedded_block)
        if m:
            start = m.start()
            rest = embedded_block[start + 1 :]
            m2 = re.search(r'\n\t\t\(symbol "', rest)
            chunk = rest[: m2.start()] if m2 else rest
            return list(dict.fromkeys(re.findall(r'\(number "(\d+)"', chunk)))
    return list(dict.fromkeys(re.findall(r'\(number "(\d+)"', embedded_block)))


def rot_pt(px: float, py: float, angle_deg: float) -> tuple[float, float]:
    a = math.radians(angle_deg)
    c, s = math.cos(a), math.sin(a)
    return px * c - py * s, px * s + py * c


G = 1.27  # KiCad connection grid


def u(n: int | float) -> float:
    """Millimetres from integer (or half) grid steps."""
    return round(float(n) * G, 2)


def r2(v: float) -> float:
    return round(v, 2)


def snap(v: float) -> float:
    return round(round(v / G) * G, 2)


class Sheet:
    def __init__(self, symbols: dict[str, str]) -> None:
        self.symbols = symbols
        self.items: list[str] = []
        self.parts: dict[tuple[str, int], dict] = {}
        self._pwr = 0

    def text(self, txt: str, x: float, y: float, size: float = 1.27, bold: bool = False) -> None:
        b = "\n\t\t\t\t(bold yes)" if bold else ""
        txt = txt.replace("\\", "\\\\").replace('"', '\\"')
        self.items.append(
            f"""\t(text "{txt}"
\t\t(exclude_from_sim no)
\t\t(at {r2(x)} {r2(y)} 0)
\t\t(effects
\t\t\t(font
\t\t\t\t(size {size} {size}){b}
\t\t\t)
\t\t\t(justify left bottom)
\t\t)
\t\t(uuid "{uid()}")
\t)"""
        )

    def label(self, name: str, x: float, y: float, shape: str = "input", rot: int = 0) -> None:
        just = "right" if rot == 180 else "left"
        if shape == "passive":
            shape = "bidirectional"
        self.items.append(
            f"""\t(global_label "{name}"
\t\t(shape {shape})
\t\t(at {r2(x)} {r2(y)} {rot})
\t\t(effects
\t\t\t(font
\t\t\t\t(size 1.27 1.27)
\t\t\t)
\t\t\t(justify {just})
\t\t)
\t\t(uuid "{uid()}")
\t)"""
        )

    def wire(self, *pts: tuple[float, float]) -> None:
        """Emit orthogonal wire segments (KiCad allows only 2 points per wire)."""
        if len(pts) < 2:
            return
        for a, b in zip(pts, pts[1:]):
            if a == b:
                continue
            self.items.append(
                f"""\t(wire
\t\t(pts
\t\t\t(xy {r2(a[0])} {r2(a[1])}) (xy {r2(b[0])} {r2(b[1])})
\t\t)
\t\t(stroke
\t\t\t(width 0)
\t\t\t(type default)
\t\t)
\t\t(uuid "{uid()}")
\t)"""
            )

    def junction(self, x: float, y: float) -> None:
        self.items.append(
            f"""\t(junction
\t\t(at {r2(x)} {r2(y)})
\t\t(diameter 0)
\t\t(color 0 0 0 0)
\t\t(uuid "{uid()}")
\t)"""
        )

    def no_connect(self, x: float, y: float) -> None:
        self.items.append(
            f"""\t(no_connect
\t\t(at {r2(x)} {r2(y)})
\t\t(uuid "{uid()}")
\t)"""
        )

    def place(
        self,
        lib_id: str,
        ref: str,
        value: str,
        x: float,
        y: float,
        unit: int = 1,
        rot: float = 0,
    ) -> None:
        pins = unit_pins(self.symbols[lib_id], lib_id, unit)
        pin_block = "\n".join(
            f'\t\t(pin "{n}"\n\t\t\t(uuid "{uid()}")\n\t\t)' for n in pins
        )
        # Property offsets roughly above/below body
        self.items.append(
            f"""\t(symbol
\t\t(lib_id "{lib_id}")
\t\t(at {r2(x)} {r2(y)} {int(rot)})
\t\t(unit {unit})
\t\t(exclude_from_sim no)
\t\t(in_bom yes)
\t\t(on_board yes)
\t\t(dnp no)
\t\t(uuid "{uid()}")
\t\t(property "Reference" "{ref}"
\t\t\t(at {r2(x)} {r2(y + 5.08)} 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t)
\t\t)
\t\t(property "Value" "{value}"
\t\t\t(at {r2(x)} {r2(y - 5.08)} 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t)
\t\t)
\t\t(property "Footprint" ""
\t\t\t(at {r2(x)} {r2(y)} 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t\t(hide yes)
\t\t\t)
\t\t)
\t\t(property "Datasheet" "~"
\t\t\t(at {r2(x)} {r2(y)} 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t\t(hide yes)
\t\t\t)
\t\t)
\t\t(property "Description" ""
\t\t\t(at {r2(x)} {r2(y)} 0)
\t\t\t(effects
\t\t\t\t(font
\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t)
\t\t\t\t(hide yes)
\t\t\t)
\t\t)
{pin_block}
\t\t(instances
\t\t\t(project "{PROJECT}"
\t\t\t\t(path "/{SCH_UUID}"
\t\t\t\t\t(reference "{ref}")
\t\t\t\t\t(unit {unit})
\t\t\t\t)
\t\t\t)
\t\t)
\t)"""
        )
        self.parts[(ref, unit)] = {
            "lib": lib_id,
            "x": x,
            "y": y,
            "rot": rot,
            "value": value,
        }

    def pin(self, ref: str, pin: str, unit: int = 1) -> tuple[float, float]:
        """Absolute pin connection point (KiCad flips Y after symbol rotation)."""
        p = self.parts[(ref, unit)]
        ox, oy = LIB_PINS[p["lib"]][str(pin)]
        rx, ry = rot_pt(ox, oy, p["rot"])
        return p["x"] + rx, p["y"] - ry

    def pwr_at(self, kind: str, pin_xy: tuple[float, float]) -> str:
        """Place a power symbol with its origin on pin_xy (zero-length pin)."""
        px, py = snap(pin_xy[0]), snap(pin_xy[1])
        self._pwr += 1
        ref = f"#PWR{self._pwr:02d}"
        lib = {
            "GND": "power:GND",
            "+12V": "power:+12V",
            "-12V": "power:-12V",
            "PWR_FLAG": "power:PWR_FLAG",
        }[kind]
        value = "PWR_FLAG" if kind == "PWR_FLAG" else kind
        self.place(lib, ref, value, px, py)
        self.junction(px, py)
        return ref

    def gnd_at(self, pin_xy: tuple[float, float]) -> str:
        """GND via a short stub to the side (avoids shorting vertical signal runs)."""
        px, py = snap(pin_xy[0]), snap(pin_xy[1])
        gx = snap(px - 2 * G)
        self.pwr_at("GND", (gx, py))
        self.wire((px, py), (gx, py))
        return f"#PWR{self._pwr:02d}"


def build_circuit(s: Sheet) -> None:
    """Place parts left→right and emit wires for DISTORTION.md topology."""

    s.text("Post-LP distortion (Drive + Mix) — fully wired", u(16), u(310), 3.0, True)
    s.text(
        "AS2164 (SSI2164 symbol). Rails ±12V. Values = bench-tune starts. See DISTORTION.md",
        u(16),
        u(304),
    )
    s.text(
        "LP_OUT → dry buf + Drive VCA → presence → asym clip → fold → LPF(=WET) → Mix → DIST_OUT",
        u(16),
        u(299),
    )

    def p(ref: str, pin: str, unit: int = 1) -> tuple[float, float]:
        x, y = s.pin(ref, pin, unit)
        return snap(x), snap(y)

    def w(*pts: tuple[float, float]) -> None:
        s.wire(*[(snap(x), snap(y)) for x, y in pts])

    def j(*xy: float) -> None:
        s.junction(snap(xy[0]), snap(xy[1]))

    # --- Chip origins (grid units) ---
    # Signal rail Y = 200 (= 254 mm)
    Y = 200
    s.place(LIB_AS, "U1", "AS2164", u(120), u(170))
    s.text("U1 AS2164  Ch1 Drive / Ch2 Wet Mix / Ch3 Dry Mix / Ch4 spare", u(95), u(200), 1.5, True)

    # U2: A dry, B drive I-V, C clip, D wet buf
    s.place(LIB_OA, "U2", "TL074", u(50), u(Y), unit=1)
    s.place(LIB_OA, "U2", "TL074", u(175), u(Y), unit=2)
    s.place(LIB_OA, "U2", "TL074", u(240), u(Y), unit=3)
    s.place(LIB_OA, "U2", "TL074", u(360), u(Y), unit=4)
    s.place(LIB_OA, "U2", "TL074", u(50), u(40), unit=5)
    s.text("U2 TL074  A dry  B drive I-V  C clip  D wet buf", u(45), u(220), 1.5, True)

    # U3: A wet I-V, B dry I-V, C summer, D MIX inv
    s.place(LIB_OA, "U3", "TL074", u(175), u(110), unit=1)
    s.place(LIB_OA, "U3", "TL074", u(175), u(70), unit=2)
    s.place(LIB_OA, "U3", "TL074", u(260), u(90), unit=3)
    s.place(LIB_OA, "U3", "TL074", u(55), u(85), unit=4)  # MIX invert pocket (clear of R2/C2)
    s.place(LIB_OA, "U3", "TL074", u(75), u(40), unit=5)
    s.text("U3 TL074  A wet I-V  B dry I-V  C summer  D MIX invert", u(150), u(130), 1.5, True)

    # Op-amp + is below origin after KiCad Y-flip (file +y → screen -y)
    s.place(LIB_CONN, "J1", "LP_OUT", u(20), u(Y) - u(2))
    s.place(LIB_CONN, "J2", "DRIVE_CV", u(20), u(160))
    s.place(LIB_CONN, "J3", "MIX_CV", u(20), u(110) - u(2))
    s.place(LIB_CONN, "J4", "DIST_OUT", u(320), u(90))

    # CV RC: R on the CV Y; C hanging below with top pin on the filter node (Y-flip: pin2=top)
    s.place(LIB_R, "R1", "10k", u(35), u(160), rot=90)
    s.place(LIB_C, "C1", "100n", u(45), u(160 - 3))  # top pin at drive CV Y
    s.place(LIB_R, "R2", "10k", u(35), u(112), rot=90)
    s.place(LIB_C, "C2", "100n", u(45), u(112 - 3))  # top pin at mix CV Y
    # I_IN1 screen Y = u1y - 15.24
    s.place(LIB_R, "R3", "30k", u(95), u(158), rot=90)
    s.place(LIB_R, "R4", "30k", u(175), u(Y + 12))
    s.place(LIB_R, "R5", "10k", u(195), u(Y), rot=90)
    s.place(LIB_R, "R6", "22k", u(205), u(Y - 12))
    s.place(LIB_R, "R7", "4k7", u(215), u(Y), rot=90)
    s.place(LIB_C, "C3", "3n3", u(225), u(Y - 12))
    # U2C clip — R8 in; R9 fb immediately right of amp; D1 then R10–D2 above
    s.place(LIB_R, "R8", "100k", u(228), u(Y + 2), rot=90)  # into U2C − (upper input)
    s.place(LIB_R, "R9", "10k", u(250), u(Y + 3))  # vertical fb: bottom=out, top=− row
    s.place(LIB_D, "D1", "1N4148", u(240), u(Y + 11))  # K←−  A→out
    s.place(LIB_LED, "D2", "LED", u(236), u(Y + 17))  # K←−  A→R10
    s.place(LIB_R, "R10", "1k", u(244), u(Y + 17), rot=90)  # out→R10→D2→−
    s.place(LIB_R, "R11", "22k", u(280), u(Y), rot=90)
    # Fold clamps (vertical): D3 A→fold / K→GND; D4 K→fold / A→GND (antiparallel)
    s.place(LIB_D, "D3", "1N4148", u(292), u(Y - 8), rot=90)
    s.place(LIB_D, "D4", "1N4148", u(298), u(Y - 8), rot=270)
    s.place(LIB_R, "R12", "100k", u(305), u(Y - 20))
    s.place(LIB_C, "C4", "1u", u(315), u(Y - 2), rot=90)
    s.place(LIB_R, "R13", "10k", u(330), u(Y - 2), rot=90)
    s.place(LIB_C, "C5", "1n", u(340), u(Y - 14))
    # I_IN2 screen Y = u1y - 7.62; I_IN3 at u1y
    s.place(LIB_R, "R14", "30k", u(100), u(164), rot=90)
    s.place(LIB_R, "R15", "30k", u(175), u(110 + 12))
    s.place(LIB_R, "R16", "30k", u(100), u(170), rot=90)
    s.place(LIB_R, "R17", "30k", u(175), u(70 + 12))
    s.place(LIB_R, "R19", "10k", u(210), u(110), rot=90)
    s.place(LIB_R, "R18", "10k", u(210), u(70), rot=90)
    s.place(LIB_R, "R20", "10k", u(260), u(90 + 12))
    # U3D inverter pocket: R21 in | amp | R22 fb; R23 from + to GND below
    s.place(LIB_R, "R21", "100k", u(42), u(87), rot=90)  # into U3D − (upper)
    s.place(LIB_R, "R22", "100k", u(65), u(88))  # fb: pin1 at out Y, pin2 above − Y
    s.place(LIB_R, "R23", "100k", u(48), u(78))  # + down to GND
    s.place(LIB_R, "R24", "0R", u(105), u(150))

    # Power on IC pins (origin = pin; zero-length power pin)
    s.pwr_at("+12V", p("U1", "16"))
    s.pwr_at("-12V", p("U1", "9"))
    s.pwr_at("GND", p("U1", "8"))
    s.pwr_at("+12V", p("U2", "4", 5))
    s.pwr_at("-12V", p("U2", "11", 5))
    s.pwr_at("+12V", p("U3", "4", 5))
    s.pwr_at("-12V", p("U3", "11", 5))
    # ERC: power symbols are power_in — place flags on the rail pins (no long wires)
    s.pwr_at("PWR_FLAG", p("U2", "4", 5))
    s.pwr_at("PWR_FLAG", p("U2", "11", 5))
    s.pwr_at("PWR_FLAG", p("U1", "8"))

    # Labels
    s.label("LP_OUT", u(10), p("J1", "1")[1], "input")
    s.label("DRIVE_CV", u(10), u(160), "input")
    s.label("MIX_CV", u(10), p("J3", "1")[1], "input")
    s.label("DIST_OUT", u(340), u(90), "output", 180)
    s.label("DRY", u(70), u(Y), "bidirectional")
    s.label("WET", u(385), u(Y), "bidirectional")

    # ===== WIRING =====
    # LP_OUT → J1 → tee → U2A+ and down to R3
    j1 = p("J1", "1")
    u2a_p, u2a_n, u2a_o = p("U2", "3", 1), p("U2", "2", 1), p("U2", "1", 1)
    w((u(10), j1[1]), j1)
    tee_lp = (u(40), j1[1])
    w(j1, tee_lp)
    j(*tee_lp)
    w(tee_lp, (u2a_p[0] - u(4), u2a_p[1]), u2a_p)
    # follower −↔out
    w(u2a_n, (u2a_n[0] - u(2), u2a_n[1]), (u2a_n[0] - u(2), u2a_o[1]), u2a_o)
    dry = (u(70), u(Y))
    w(u2a_o, dry)
    j(*dry)

    # tee → R3 → I_IN1
    r3a, r3b = p("R3", "1"), p("R3", "2")
    w(tee_lp, (tee_lp[0], r3a[1]), r3a)
    w(r3b, p("U1", "2"))

    # DRY → R16 → I_IN3
    r16a, r16b = p("R16", "1"), p("R16", "2")
    w(dry, (dry[0], r16a[1]), r16a)
    w(r16b, p("U1", "10"))

    # DRIVE_CV → R1 → tee → C1(top) / VC1; C1 bottom → GND
    j2 = p("J2", "1")
    r1a, r1b = p("R1", "1"), p("R1", "2")
    c1_bot, c1_top = p("C1", "1"), p("C1", "2")  # Y-flip: pin1 below, pin2 above
    w((u(10), u(160)), j2)
    w(j2, r1a)
    drive_tee = (c1_top[0], r1b[1])
    w(r1b, drive_tee)
    j(*drive_tee)
    w(drive_tee, c1_top)
    s.gnd_at(c1_bot)
    vc1 = p("U1", "3")
    # Route DRIVE CV on its own column (left of AS2164 left pins)
    w(drive_tee, (u(100), drive_tee[1]), (u(100), vc1[1]), (vc1[0] - u(2), vc1[1]), vc1)

    # MIX_CV → R2 → tee → C2(top) / MIX_FILT; C2 bottom → GND
    j3 = p("J3", "1")
    r2a, r2b = p("R2", "1"), p("R2", "2")
    c2_bot, c2_top = p("C2", "1"), p("C2", "2")
    w((u(10), j3[1]), j3)
    w(j3, r2a)
    mix_tee = (c2_top[0], r2b[1])
    w(r2b, mix_tee)
    j(*mix_tee)
    w(mix_tee, c2_top)
    s.gnd_at(c2_bot)
    vc2 = p("U1", "6")
    # MIX filter node → VC2; also a global label for the inverter Rin (avoids long bus shorts)
    mix_bus_y = u(148)
    w(mix_tee, (mix_tee[0], mix_bus_y), (u(100), mix_bus_y), (u(100), vc2[1]), (vc2[0] - u(2), vc2[1]), vc2)
    s.label("MIX_FILT", mix_tee[0] - u(2), mix_tee[1], "bidirectional")
    w(mix_tee, (mix_tee[0] - u(2), mix_tee[1]))

    # U3D inverter — local textbook wiring; MIX_FILT / MIX_CV_INV via labels
    r21a, r21b = p("R21", "1"), p("R21", "2")
    s.label("MIX_FILT", r21a[0] - u(4), r21a[1], "bidirectional")
    w((r21a[0] - u(4), r21a[1]), r21a)
    u3d_n, u3d_p, u3d_o = p("U3", "13", 4), p("U3", "12", 4), p("U3", "14", 4)
    w(r21b, u3d_n)
    r22a, r22b = p("R22", "1"), p("R22", "2")  # below / above after Y-flip
    # out → R22 bottom; R22 top drops to − row then left — never land on the other R22 pin
    r22_tee_o = (r22a[0], u3d_o[1])
    r22_tee_n = (r22b[0], u3d_n[1])
    w(u3d_o, r22_tee_o)
    j(*u3d_o)
    j(*r22_tee_o)
    w(r22_tee_o, r22a)
    w(r22b, r22_tee_n)
    j(*r22_tee_n)
    w(r22_tee_n, u3d_n)
    j(*u3d_n)
    r23a, r23b = p("R23", "1"), p("R23", "2")  # below / above
    # + (lower amp pin) → R23 top → R23 bottom → GND
    w(u3d_p, (r23b[0], u3d_p[1]), r23b)
    s.gnd_at(r23a)
    mix_inv = (r22_tee_o[0] + u(4), u3d_o[1])
    s.label("MIX_CV_INV", mix_inv[0], mix_inv[1], "bidirectional")
    w(r22_tee_o, mix_inv)
    vc3 = p("U1", "11")
    s.label("MIX_CV_INV", vc3[0] - u(4), vc3[1], "bidirectional")
    w((vc3[0] - u(4), vc3[1]), vc3)

    # Drive I-V U2B
    iout1 = p("U1", "4")
    u2b_n, u2b_p, u2b_o = p("U2", "6", 2), p("U2", "5", 2), p("U2", "7", 2)
    w(iout1, (u2b_n[0] - u(4), iout1[1]), (u2b_n[0] - u(4), u2b_n[1]), u2b_n)
    r4a, r4b = p("R4", "1"), p("R4", "2")
    w(u2b_o, (u2b_o[0] + u(2), u2b_o[1]), (u2b_o[0] + u(2), r4a[1]), r4a)
    w(r4b, (r4b[0], u2b_n[1]), u2b_n)
    j(*u2b_n)
    g_pt = (u2b_p[0] - u(4), u2b_p[1])
    w(u2b_p, g_pt)
    s.gnd_at(g_pt)

    # U2B → R5 → gain tee → R6 GND + R7
    r5a, r5b = p("R5", "1"), p("R5", "2")
    w(u2b_o, (r5a[0], u2b_o[1]), r5a)
    j(*u2b_o)
    gain = r5b
    j(*gain)
    r6a, r6b = p("R6", "1"), p("R6", "2")
    w(gain, (r6a[0], gain[1]), r6a)
    s.gnd_at(r6b)
    r7a, r7b = p("R7", "1"), p("R7", "2")
    w(gain, r7a)

    # Presence tee
    c3a, c3b = p("C3", "1"), p("C3", "2")
    r8a, r8b = p("R8", "1"), p("R8", "2")
    pres = (c3a[0], r7b[1])
    w(r7b, pres)
    j(*pres)
    w(pres, c3a)
    s.gnd_at(c3b)
    w(pres, r8a)

    # Clip U2C — explicit tees at R9 (out and −) so fb is obvious
    u2c_n, u2c_p, u2c_o = p("U2", "9", 3), p("U2", "10", 3), p("U2", "8", 3)
    w(r8b, u2c_n)
    j(*u2c_n)
    r9a, r9b = p("R9", "1"), p("R9", "2")  # pin1 below, pin2 above
    r9_tee_o = (r9a[0], u2c_o[1])
    r9_tee_n = (r9b[0], u2c_n[1])
    w(u2c_o, r9_tee_o)
    j(*u2c_o)
    j(*r9_tee_o)
    w(r9_tee_o, r9a)
    w(r9b, r9_tee_n)
    j(*r9_tee_n)
    w(r9_tee_n, u2c_n)
    d1k, d1a = p("D1", "1"), p("D1", "2")
    w(u2c_o, (u2c_o[0], d1a[1]), d1a)
    w(d1k, (u2c_n[0], d1k[1]), u2c_n)
    r10a, r10b = p("R10", "1"), p("R10", "2")  # left, right
    d2k, d2a = p("D2", "1"), p("D2", "2")
    # climb from R9 out-tee (not a long bus through R9)
    w(r9_tee_o, (r10b[0], u2c_o[1]), (r10b[0], r10b[1]), r10b)
    w(r10a, d2a)
    w(d2k, (u2c_n[0], d2k[1]), u2c_n)
    g2 = (u2c_p[0] - u(4), u2c_p[1])
    w(u2c_p, g2)
    s.gnd_at(g2)

    # Fold — chained bus with a junction at every tap (D3/D4/R12/C4)
    r11a, r11b = p("R11", "1"), p("R11", "2")
    w(r9_tee_o, (r11a[0], u2c_o[1]), r11a)
    d3k, d3a = p("D3", "1"), p("D3", "2")
    d4k, d4a = p("D4", "1"), p("D4", "2")
    fold = r11b
    j(*fold)
    d3_tee = (d3a[0], fold[1])
    d4_tee = (d4k[0], fold[1])
    w(fold, d3_tee)
    j(*d3_tee)
    w(d3_tee, d3a)
    w(d3_tee, d4_tee)
    j(*d4_tee)
    w(d4_tee, d4k)
    gfold = (u(295), u(Y - 28))
    s.gnd_at(gfold)
    d3_g = (d3k[0], gfold[1])
    d4_g = (d4a[0], gfold[1])
    w(d3k, d3_g)
    j(*d3_g)
    w(d3_g, gfold)
    w(d4a, d4_g)
    j(*d4_g)
    w(d4_g, gfold)
    j(*gfold)
    r12a, r12b = p("R12", "1"), p("R12", "2")
    r12_tee = (r12a[0], fold[1])
    r12_g = (r12b[0], gfold[1])
    w(d4_tee, r12_tee)
    j(*r12_tee)
    w(r12_tee, r12a)
    w(r12b, r12_g)
    j(*r12_g)
    w(r12_g, gfold)

    c4a, c4b = p("C4", "1"), p("C4", "2")
    r13a, r13b = p("R13", "1"), p("R13", "2")
    c5a, c5b = p("C5", "1"), p("C5", "2")
    w(r12_tee, (c4a[0], fold[1]), c4a)
    w(c4b, r13a)
    lpf = (c5a[0], r13b[1])
    w(r13b, lpf)
    j(*lpf)
    w(lpf, c5a)
    s.gnd_at(c5b)

    # U2D follower → WET
    u2d_p, u2d_n, u2d_o = p("U2", "12", 4), p("U2", "13", 4), p("U2", "14", 4)
    w(lpf, (u2d_p[0] - u(4), lpf[1]), (u2d_p[0] - u(4), u2d_p[1]), u2d_p)
    w(u2d_n, (u2d_n[0] - u(2), u2d_n[1]), (u2d_n[0] - u(2), u2d_o[1]), u2d_o)
    wet = (u(385), u(Y))
    w(u2d_o, wet)
    j(*wet)

    # WET → R14 → I_IN2
    r14a, r14b = p("R14", "1"), p("R14", "2")
    w(wet, (wet[0], u(235)), (r14a[0], u(235)), (r14a[0], r14a[1]), r14a)
    w(r14b, p("U1", "7"))

    # Mix I-V wet U3A
    iout2 = p("U1", "5")
    u3a_n, u3a_p, u3a_o = p("U3", "2", 1), p("U3", "3", 1), p("U3", "1", 1)
    w(iout2, (iout2[0] + u(8), iout2[1]), (iout2[0] + u(8), u3a_n[1]), u3a_n)
    r15a, r15b = p("R15", "1"), p("R15", "2")
    w(u3a_o, (u3a_o[0] + u(2), u3a_o[1]), (u3a_o[0] + u(2), r15a[1]), r15a)
    w(r15b, (r15b[0], u3a_n[1]), u3a_n)
    j(*u3a_n)
    g3a = (u3a_p[0] - u(4), u3a_p[1])
    w(u3a_p, g3a)
    s.gnd_at(g3a)

    # Mix I-V dry U3B
    iout3 = p("U1", "12")
    u3b_n, u3b_p, u3b_o = p("U3", "6", 2), p("U3", "5", 2), p("U3", "7", 2)
    w(iout3, (iout3[0] + u(8), iout3[1]), (iout3[0] + u(8), u3b_n[1]), u3b_n)
    r17a, r17b = p("R17", "1"), p("R17", "2")
    w(u3b_o, (u3b_o[0] + u(2), u3b_o[1]), (u3b_o[0] + u(2), r17a[1]), r17a)
    w(r17b, (r17b[0], u3b_n[1]), u3b_n)
    j(*u3b_n)
    g3b = (u3b_p[0] - u(4), u3b_p[1])
    w(u3b_p, g3b)
    s.gnd_at(g3b)

    # Summer U3C
    u3c_n, u3c_p, u3c_o = p("U3", "9", 3), p("U3", "10", 3), p("U3", "8", 3)
    r19a, r19b = p("R19", "1"), p("R19", "2")
    r18a, r18b = p("R18", "1"), p("R18", "2")
    r20a, r20b = p("R20", "1"), p("R20", "2")
    w(u3a_o, (r19a[0], u3a_o[1]), r19a)
    j(*u3a_o)
    sum_in = (u3c_n[0] - u(4), u3c_n[1])
    w(r19b, (sum_in[0], r19b[1]), sum_in, u3c_n)
    w(u3b_o, (r18a[0], u3b_o[1]), r18a)
    j(*u3b_o)
    w(r18b, (sum_in[0], r18b[1]), sum_in)
    j(*sum_in)
    w(u3c_o, (u3c_o[0] + u(2), u3c_o[1]), (u3c_o[0] + u(2), r20a[1]), r20a)
    w(r20b, (r20b[0], u3c_n[1]), u3c_n)
    j(*u3c_n)
    g3c = (u3c_p[0] - u(4), u3c_p[1])
    w(u3c_p, g3c)
    s.gnd_at(g3c)
    j4 = p("J4", "1")
    w(u3c_o, (j4[0] - u(4), u3c_o[1]), (j4[0] - u(4), j4[1]), j4)
    j(*u3c_o)
    w(j4, (u(340), u(90)))

    # MODE via R24 → GND; VC4 → GND; Ch4 NC
    r24a, r24b = p("R24", "1"), p("R24", "2")
    mode = p("U1", "1")
    w(mode, (r24a[0], mode[1]), (r24a[0], r24a[1]), r24a)
    s.gnd_at(r24b)
    s.gnd_at(p("U1", "14"))
    s.no_connect(*p("U1", "15"))
    s.no_connect(*p("U1", "13"))

    s.text("Ch4 spare: I_IN4 / I_OUT4 no-connect; VC4→GND; MODE→GND via R24 (Class A)", u(16), u(20))


def main() -> None:
    symbols: dict[str, str] = {}
    lm = extract_symbol(LIBS / "Amplifier_Operational.kicad_sym", "LM2902")
    symbols[LIB_OA] = embed_symbol(
        lm,
        LIB_OA,
        "LM2902",
        "TL074",
        nested_name="TL074",
    )
    specs = [
        (LIB_AS, "Audio.kicad_sym", "SSI2164", "AS2164", "AS2164"),
        (LIB_R, "Device.kicad_sym", "R", "R", None),
        (LIB_C, "Device.kicad_sym", "C", "C", None),
        (LIB_D, "Device.kicad_sym", "D", "1N4148", None),
        (LIB_LED, "Device.kicad_sym", "LED", "LED", None),
        ("power:+12V", "power.kicad_sym", "+12V", "+12V", None),
        ("power:-12V", "power.kicad_sym", "-12V", "-12V", None),
        ("power:GND", "power.kicad_sym", "GND", "GND", None),
        ("power:PWR_FLAG", "power.kicad_sym", "PWR_FLAG", "PWR_FLAG", None),
        (LIB_CONN, "Connector_Generic.kicad_sym", "Conn_01x01", "PAD", "Conn_01x01"),
    ]
    for lib_id, fname, src, value, nest in specs:
        block = extract_symbol(LIBS / fname, src)
        symbols[lib_id] = embed_symbol(
            block, lib_id, src, value, nested_name=nest or src
        )

    s = Sheet(symbols)
    build_circuit(s)

    def indent_lib(block: str) -> str:
        return "\n".join(("\t" + line) if line.strip() else line for line in block.splitlines())

    lib_symbols = "\n".join(indent_lib(b) for b in symbols.values())
    sch = f"""(kicad_sch
\t(version 20250114)
\t(generator "generate_sch.py")
\t(generator_version "10.0")
\t(uuid "{SCH_UUID}")
\t(paper "A2")
\t(title_block
\t\t(title "DCO post-LP distortion")
\t\t(date "2026-07-31")
\t\t(rev "0.2")
\t\t(company "DCO3-MONOSYNTH")
\t\t(comment 1 "AS2164 Drive+Mix, asymmetric clip, fold — fully wired")
\t\t(comment 2 "Bench-tune values — DCO/docs/DISTORTION.md")
\t)
\t(lib_symbols
{lib_symbols}
\t)
{chr(10).join(s.items)}
\t(sheet_instances
\t\t(path "/"
\t\t\t(page "1")
\t\t)
\t)
\t(embedded_fonts no)
)
"""
    (OUT / "distortion.kicad_sch").write_text(sch, encoding="utf-8")

    pro = {
        "board": {"design_settings": {"meta": {"version": 2}}},
        "boards": [],
        "cvpcb": {"equivalence_files": []},
        "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
        "meta": {"filename": "distortion.kicad_pro", "version": 3},
        "net_settings": {
            "classes": [
                {
                    "bus_width": 12,
                    "clearance": 0.2,
                    "diff_pair_gap": 0.25,
                    "diff_pair_via_gap": 0.25,
                    "diff_pair_width": 0.2,
                    "line_width": 0.25,
                    "microvia_diameter": 0.3,
                    "microvia_drill": 0.1,
                    "name": "Default",
                    "pcb_color": "rgba(0, 0, 0, 0.000)",
                    "schematic_color": "rgba(0, 0, 0, 0.000)",
                    "track_width": 0.25,
                    "via_diameter": 0.8,
                    "via_drill": 0.4,
                    "wire_width": 6,
                }
            ],
            "meta": {"version": 4},
        },
        "pcbnew": {
            "last_paths": {
                "gencam": "",
                "idf": "",
                "netlist": "",
                "specctra_dsn": "",
                "step": "",
                "vrml": "",
            }
        },
        "schematic": {
            "annotate_start_num": 0,
            "drawing": {
                "dashed_lines_dash_length_ratio": 12.0,
                "dashed_lines_gap_length_ratio": 3.0,
                "default_line_thickness": 6.0,
                "default_text_size": 50.0,
                "field_names": [],
                "intersheets_ref_own_page": False,
                "intersheets_ref_prefix": "",
                "intersheets_ref_short": False,
                "intersheets_ref_show": False,
                "intersheets_ref_suffix": "",
                "junction_size_choice": 3,
                "label_size_ratio": 0.375,
                "operating_point_overlay_i_precision": 3,
                "operating_point_overlay_i_range": "~A",
                "operating_point_overlay_v_precision": 3,
                "operating_point_overlay_v_range": "~V",
                "overbar_offset_ratio": 1.23,
                "pin_symbol_size": 25.0,
                "text_offset_ratio": 0.15,
            },
            "legacy_lib_dir": "",
            "legacy_lib_list": [],
            "meta": {"version": 1},
            "net_format_name": "",
            "page_layout_descr_file": "",
            "plot_directory": "",
            "spice_current_sheet_as_root": False,
            "spice_external_command": "spice \"%I\"",
            "spice_model_current_sheet_as_root": True,
            "spice_save_all_currents": False,
            "spice_save_all_dissipations": False,
            "spice_save_all_voltages": False,
            "subpart_first_id": 65,
            "subpart_id_separator": 0,
        },
        "sheets": [["a1b2c3d4-e5f6-7890-abcd-ef1234567890", "Root"]],
        "text_variables": {},
    }
    (OUT / "distortion.kicad_pro").write_text(json.dumps(pro, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {OUT / 'distortion.kicad_sch'} ({(OUT / 'distortion.kicad_sch').stat().st_size} bytes)")
    print(f"wrote {OUT / 'distortion.kicad_pro'}")
    print(f"symbols={len(symbols)} items={len(s.items)} parts={len(s.parts)}")


if __name__ == "__main__":
    main()
