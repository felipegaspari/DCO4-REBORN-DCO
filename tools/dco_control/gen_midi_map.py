#!/usr/bin/env python3
"""Emit the MIDI CC map, its chart and an Open Stage Control panel from params.py.

params.py is the one description of the DCO's control surface, so the firmware table,
the implementation chart and the panel session are all generated from it and cannot
drift apart. Three outputs:

  ../../midi_cc_map.h              the MidiCcEntry table, included by DCO/midi_cc.h
  ../../docs/MIDI_CC_MAP.md        the implementation chart
  ../panels/dco3_panel.json        the Open Stage Control session

Usage:
  python3 gen_midi_map.py           write the three files
  python3 gen_midi_map.py --check   validate and diff only, exit 1 on drift

The checks are the interesting part: they catch a CC collision, a reserved controller,
a parameter that params.py claims but the firmware does not route, a block value whose
CC_LOCAL_* target is not handled in midi.ino, and a combo entry that 7-bit CC cannot
express exactly.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path

import params
import protocol

# Controllers left alone: 0/32 bank select, 1 mod wheel, 6/38 data entry, 7 volume,
# 10 pan, 11 expression, 42 pitch-bend range (the DCO's own historical use), 64 sustain,
# 98-101 NRPN/RPN, and 120-127 channel mode. Keeping 98-101 free also leaves room for a
# later NRPN upgrade without moving any assignment made here.
RESERVED_CC = {0, 1, 6, 7, 10, 11, 32, 38, 42, 64, 98, 99, 100, 101,
               120, 121, 122, 123, 124, 125, 126, 127}

MIDI_CHANNEL = 1
MIDI_TARGET = "midi:dco3"

CURVE_LINEAR = "MIDI_CC_LINEAR"
CURVE_EXP_TIME = "MIDI_CC_EXP_TIME"

HERE = Path(__file__).resolve().parent
DCO_DIR = HERE.parents[1]

MAP_HEADER = DCO_DIR / "midi_cc_map.h"
CHART = DCO_DIR / "docs" / "MIDI_CC_MAP.md"
PANEL = DCO_DIR / "tools" / "panels" / "dco3_panel.json"

GENERATED_BY = "tools/dco_control/gen_midi_map.py from tools/dco_control/params.py"


@dataclass
class Entry:
    """One row of the CC map, in both firmware and panel terms."""

    cc: int
    target: str  # C expression: a PARAM_* name or a CC_LOCAL_* code
    lo: int
    hi: int
    curve: str
    label: str
    group: str
    kind: str  # fader, menu or switch
    note: str = ""
    choices: tuple = ()  # (label, native value) pairs, menus only
    unreachable: tuple = ()  # choices 7-bit CC cannot express
    is_local: bool = False


def cc_to_native(cc: int, lo: int, hi: int, curve: str) -> int:
    """Mirror of the scaling in midi_cc_handle(), so the chart cannot lie."""
    value = lo + ((hi - lo) * cc + 63) // 127
    if curve == CURVE_EXP_TIME:
        value = protocol.lin_to_exp(value)
    return value


def local_target(block: params.Block, field_: params.BlockField) -> str:
    return f"CC_LOCAL_{block.key}_{field_.key}".upper()


def param_target(pid: int, enum_by_id: dict[int, str]) -> str:
    return enum_by_id.get(pid, str(pid))


def param_range(p: params.Param) -> tuple[int, int, str]:
    """lo, hi and widget kind for a parameter.

    Combos map a CC straight onto the native value (lo 0, hi 127 makes the scaling an
    identity), which is what lets a menu entry pick an exact value such as sub-osc
    divide 2. Checks scale to 0..1 so a CC below 64 is off and 64 or above is on.
    """
    if p.kind == "combo":
        return 0, 127, "menu"
    if p.kind == "check":
        return 0, 1, "switch"
    return p.lo, p.hi, "fader"


def build_entries(enum_by_id: dict[int, str]) -> list[Entry]:
    entries: list[Entry] = []
    for group in params.GROUP_ORDER:
        for p in params.PARAMS:
            if p.group != group or p.cc is None:
                continue
            lo, hi, kind = param_range(p)
            entry = Entry(cc=p.cc, target=param_target(p.pid, enum_by_id), lo=lo, hi=hi,
                          curve=CURVE_LINEAR, label=p.label, group=group, kind=kind,
                          note=p.note)
            if p.kind == "combo":
                reachable = []
                unreachable = []
                for choice_label, value in p.choices:
                    if 0 <= value <= 127 and cc_to_native(value, lo, hi, entry.curve) == value:
                        reachable.append((choice_label, value))
                    else:
                        unreachable.append((choice_label, value))
                entry.choices = tuple(reachable)
                entry.unreachable = tuple(unreachable)
            entries.append(entry)

        for block in params.BLOCKS:
            if block.group != group:
                continue
            for field_ in block.fields:
                if field_.cc is None:
                    continue
                entries.append(Entry(
                    cc=field_.cc,
                    target=local_target(block, field_),
                    lo=field_.lo,
                    hi=field_.hi,
                    curve=CURVE_EXP_TIME if field_.exp else CURVE_LINEAR,
                    label=f"{block.label}: {field_.label}",
                    group=group,
                    kind="fader",
                    note=block.note if field_ is block.fields[0] else "",
                    is_local=True,
                ))
    return entries


# --- firmware cross-checks --------------------------------------------------------


def read_param_ids() -> tuple[dict[int, str], set[str]]:
    """(id -> PARAM_* name) from params_def.h, and the names routed by paramTable[]."""
    header = (DCO_DIR / "params_def.h").read_text()
    enum_by_id: dict[int, str] = {}
    for name, value in re.findall(r"^\s*(PARAM_\w+)\s*=\s*(\d+)", header, re.M):
        enum_by_id[int(value)] = name

    table = (DCO_DIR / "params.ino").read_text()
    body = table.split("static const ParamDescriptorT<int16_t> paramTable[]", 1)
    routed = set(re.findall(r"\{\s*(PARAM_\w+),", body[-1]))
    return enum_by_id, routed


def read_local_targets() -> tuple[set[str], set[str]]:
    """CC_LOCAL_* names declared in midi_cc.h, and those handled in midi.ino."""
    declared = set(re.findall(r"(CC_LOCAL_\w+)", (DCO_DIR / "midi_cc.h").read_text()))
    declared.discard("CC_LOCAL_FIRST")
    handled = set(re.findall(r"case\s+(CC_LOCAL_\w+)\s*:", (DCO_DIR / "midi.ino").read_text()))
    return declared, handled


def validate(entries: list[Entry], enum_by_id: dict[int, str], routed: set[str]) -> list[str]:
    problems: list[str] = []

    seen: dict[int, str] = {}
    for e in entries:
        if not 0 <= e.cc <= 127:
            problems.append(f"CC {e.cc} out of range ({e.label})")
        if e.cc in RESERVED_CC:
            problems.append(f"CC {e.cc} is reserved ({e.label})")
        if e.cc in seen:
            problems.append(f"CC {e.cc} used twice: {seen[e.cc]} and {e.label}")
        seen[e.cc] = e.label
        if e.hi <= e.lo:
            problems.append(f"CC {e.cc} has an empty range {e.lo}..{e.hi} ({e.label})")

    for p in params.PARAMS:
        if p.kind == "pulse" and p.cc is not None:
            problems.append(f"parameter {p.pid} ({p.label}) is a command and must not have a CC")
        if p.cc is None:
            continue
        name = enum_by_id.get(p.pid)
        if name is None:
            problems.append(f"parameter {p.pid} ({p.label}) is not in the params_def.h enum")
        elif name not in routed:
            problems.append(f"parameter {p.pid} ({name}) is not routed by paramTable[]")

    declared, handled = read_local_targets()
    for e in entries:
        if not e.is_local:
            continue
        if e.target not in declared:
            problems.append(f"{e.target} is not declared in midi_cc.h ({e.label})")
        if e.target not in handled:
            problems.append(f"{e.target} has no case in midi_cc_apply() ({e.label})")
    for name in sorted(declared - {e.target for e in entries if e.is_local}):
        problems.append(f"{name} is declared in midi_cc.h but no CC maps to it")

    return problems


# --- emitters ---------------------------------------------------------------------


def emit_map_header(entries: list[Entry]) -> str:
    width = max(len(e.target) for e in entries) + 1  # room for the comma
    lines = [
        "#ifndef __MIDI_CC_MAP_H__",
        "#define __MIDI_CC_MAP_H__",
        "",
        "// GENERATED FILE - do not edit.",
        f"// Emitted by {GENERATED_BY}.",
        "// Re-run that script after changing params.py; see docs/MIDI_CC_MAP.md for the chart.",
        "//",
        "// cc, target, lo, hi, curve. CC 0 lands on lo and CC 127 on hi; targets at or above",
        "// CC_LOCAL_FIRST are block values that midi_cc_apply() writes directly.",
        "",
        '#include <stddef.h>',
        '#include "params_def.h"',
        '#include "midi_cc.h"',
        "",
        "static const MidiCcEntry midiCcMap[] = {",
    ]
    current_group = None
    for e in entries:
        if e.group != current_group:
            lines.append(f"  // --- {e.group} ---")
            current_group = e.group
        lines.append(
            f"  {{ {e.cc:3d}, {e.target + ',':<{width}} {e.lo:5d}, {e.hi:5d}, {e.curve} }},"
        )
    lines += [
        "};",
        "",
        "static const size_t midiCcMapSize = sizeof(midiCcMap) / sizeof(midiCcMap[0]);",
        "",
        "#endif",
        "",
    ]
    return "\n".join(lines)


def emit_chart(entries: list[Entry]) -> str:
    out: list[str] = [
        "# MIDI CC implementation chart",
        "",
        "Generated from `tools/dco_control/params.py` by `tools/dco_control/gen_midi_map.py`. "
        "Do not edit by hand.",
        "",
        "Every control the bench app exposes is reachable from a 7-bit CC on any channel "
        "(the DCO listens omni), over USB MIDI or the DIN input. The board does not send "
        "anything back, so a panel should push its state after connecting.",
        "",
        "A controller value scales into the parameter's native range as",
        "",
        "```",
        "value = lo + ((hi - lo) * cc + 63) / 127",
        "```",
        "",
        "so CC 0 lands on `CC 0` below and CC 127 on `CC 127`. Envelope attack, decay and "
        "release then go through `linearToExponential(value, 50, 25000)`, the same curve the "
        "Input board applies to its faders, because the `'a'`-`'c'` block frames carry those "
        "values already exp-mapped.",
        "",
        "Menu-style parameters use a 0..127 range so the scaling is an identity and a menu "
        "entry can pick an exact native value. Switches scale to 0..1, so under 64 is off "
        "and 64 or over is on.",
        "",
        "## Map",
        "",
        "| CC | Control | Group | Target | CC 0 | CC 127 | Curve |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ]
    for e in entries:
        curve = "exp" if e.curve == CURVE_EXP_TIME else "linear"
        out.append(
            f"| {e.cc} | {e.label} | {e.group} | `{e.target}` | "
            f"{cc_to_native(0, e.lo, e.hi, e.curve)} | "
            f"{cc_to_native(127, e.lo, e.hi, e.curve)} | {curve} |"
        )

    menus = [e for e in entries if e.kind == "menu"]
    if menus:
        out += ["", "## Menu values", "",
                "These parameters take discrete values; the CC number to send is the value "
                "itself.", ""]
        for e in menus:
            values = ", ".join(f"{label} = {value}" for label, value in e.choices)
            out.append(f"- **CC {e.cc}, {e.label}**: {values}")
            if e.unreachable:
                missing = ", ".join(f"{label} ({value})" for label, value in e.unreachable)
                out.append(f"  - out of 7-bit reach, use the serial bench app instead: {missing}")

    skipped = [p for p in params.PARAMS if p.cc is None]
    out += ["", "## Deliberately not mapped", ""]
    for p in skipped:
        out.append(f"- **{p.label}** (parameter {p.pid})")
    out += [
        "",
        "Autotune takes the board over for about a minute and the store writes the "
        "filesystem, so neither should be one stray controller away. Both are still "
        "available from the serial bench app in `tools/dco_control`.",
        "",
        "Reserved controllers left untouched: "
        + ", ".join(str(c) for c in sorted(RESERVED_CC))
        + ". CC 42 keeps its historical meaning here, pitch-bend range in semitones. "
        "98-101 stay free so a later NRPN upgrade needs no reshuffling.",
        "",
    ]
    return "\n".join(out)


def emit_panel(entries: list[Entry]) -> str:
    tabs = []
    for group in params.GROUP_ORDER:
        widgets = [panel_widget(e) for e in entries if e.group == group]
        if not widgets:
            continue
        tabs.append({
            "type": "tab",
            "id": "tab_" + slug(group),
            "label": group,
            "layout": "grid",
            "gridTemplate": 4,
            "widgets": widgets,
        })

    root = {
        "type": "root",
        "id": "root",
        "label": "DCO3 monosynth",
        "width": 1280,
        "height": 800,
        "tabs": tabs,
    }
    return json.dumps(root, indent=2) + "\n"


def slug(text: str) -> str:
    return re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")


def panel_widget(e: Entry) -> dict:
    common = {
        "id": f"cc{e.cc}_{slug(e.label)}",
        "address": "/control",
        "preArgs": [MIDI_CHANNEL, e.cc],
        "target": MIDI_TARGET,
        "label": f"{e.label}  (CC {e.cc})",
    }
    if e.kind == "menu":
        return {
            "type": "menu",
            **common,
            "values": {f"{label} ": value for label, value in e.choices},
        }
    if e.kind == "switch":
        return {"type": "switch", **common, "values": {"Off ": 0, "On ": 127}}
    # decimals 0 keeps the widget on whole controller numbers; the range stays the MIDI
    # 0..127 because that is what /control carries, with the native range in the chart.
    return {
        "type": "fader",
        **common,
        "design": "compact",
        "range": {"min": 0, "max": 127},
        "decimals": 0,
    }


# --- driver -----------------------------------------------------------------------


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="validate and report drift without writing anything")
    args = parser.parse_args(argv)

    enum_by_id, routed = read_param_ids()
    entries = build_entries(enum_by_id)
    problems = validate(entries, enum_by_id, routed)
    if problems:
        for problem in problems:
            print(f"error: {problem}", file=sys.stderr)
        return 1

    outputs = {
        MAP_HEADER: emit_map_header(entries),
        CHART: emit_chart(entries),
        PANEL: emit_panel(entries),
    }

    stale = []
    for path, text in outputs.items():
        current = path.read_text() if path.exists() else None
        if current == text:
            continue
        stale.append(path)
        if not args.check:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text)

    for e in entries:
        for label, value in e.unreachable:
            print(f"note: CC cannot reach '{label}' ({value}) on CC {e.cc}, {e.label}")

    if args.check:
        for path in stale:
            print(f"error: {path.relative_to(DCO_DIR)} is out of date", file=sys.stderr)
        if stale:
            return 1
        print(f"up to date: {len(entries)} controllers")
        return 0

    for path in stale:
        print(f"wrote {path.relative_to(DCO_DIR)}")
    print(f"{len(entries)} controllers mapped, "
          f"{len(RESERVED_CC)} reserved, "
          f"{128 - len(RESERVED_CC) - len(entries)} free")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
