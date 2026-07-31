"""The DCO's control surface, as data. The GUI is generated entirely from this file.

Ranges are what the Input board actually transmits, because that is what the DCO's
apply_param_*() functions in DCO/params.ino expect. Only parameters the DCO actually
handles are listed: the table mirrors paramTable[] in DCO/params.ino.

Everything goes out as a 'p' (16-bit) frame. The Input board uses 'w' (8-bit) for some
of these, but 'w' sign-extends, so a value like portamento 200 arrives as -56 and only
survives because the apply function casts back through uint8_t. Sending 'p' with the
true positive value lands on the same result without depending on that round trip.
"""

from __future__ import annotations

from dataclasses import dataclass, field

import protocol

# Tab names, in display order.
GROUP_OSC = "Oscillators"
GROUP_SYNC = "Sync and PIO"
GROUP_ENV = "Envelopes"
GROUP_FILTER = "Filter"
GROUP_PWM = "PWM"
GROUP_LFO = "LFOs"
GROUP_VOICE = "Voice and Drift"
GROUP_CAL = "Calibration"

GROUP_ORDER = [
    GROUP_OSC,
    GROUP_SYNC,
    GROUP_ENV,
    GROUP_FILTER,
    GROUP_PWM,
    GROUP_LFO,
    GROUP_VOICE,
    GROUP_CAL,
]


@dataclass(frozen=True)
class Param:
    """One 'p'-frame parameter and how to present it.

    kind is one of:
      slider - continuous, lo..hi
      combo  - pick from choices, a tuple of (label, value) pairs
      check  - 0 or 1
      pulse  - a button that sends pulse_value once, for command-style params

    cc is the MIDI controller number gen_midi_map.py assigns to this parameter, or None
    for the ones deliberately left unreachable by CC. The numbers are written out rather
    than allocated on the fly so that reordering this table never moves an existing
    assignment out from under a saved panel or a DAW automation lane.
    """

    pid: int
    label: str
    group: str
    kind: str = "slider"
    lo: int = 0
    hi: int = 127
    default: int = 0
    choices: tuple = ()
    pulse_value: int = 1
    note: str = ""
    cc: int | None = None


@dataclass(frozen=True)
class BlockField:
    key: str
    label: str
    lo: int
    hi: int
    default: int
    exp: bool = False  # run through lin_to_exp() before sending
    cc: int | None = None


@dataclass(frozen=True)
class Block:
    """A grouped frame ('a'-'f'). Any field change re-sends the whole frame,
    which is exactly what the Input board does every millisecond."""

    key: str
    label: str
    group: str
    fields: tuple
    builder: object = field(repr=False, default=None)
    note: str = ""


# --- Osc sync and phase align (PARAM_OSC_SYNC_MODE = 17) --------------------
# One param, three regimes, per apply_param_osc_sync_mode() in DCO/params.ino and the
# `oscSync >= 1` gate in DCO/voices.ino:
#
#   0      the note-on block is skipped entirely, so the oscillators run straight through
#          note-on and their phase relationship is whatever it happens to be
#   1      OSC1 and OSC2 are stopped, reloaded and restarted together at note-on, with no
#          phase offset
#   2..8   the same restart, plus a fixed OSC2 offset of 45 to 315 degrees
#   >8     the same restart, with the offset in degrees being value * 2
#
# Presented as one combo so the encoding never has to be worked out by hand.
_PHASE_PRESETS = {45: 2, 90: 3, 135: 4, 180: 5, 225: 6, 270: 7, 315: 8}
_PHASE_FINE = [30, 60, 120, 150, 210, 240, 300, 330]


def _phase_choices() -> tuple:
    entries = [
        ("Off - free running (no note-on sync)", 0),
        ("Sync at note-on (0 deg)", 1),
    ]
    by_deg = dict(_PHASE_PRESETS)
    for deg in _PHASE_FINE:
        by_deg[deg] = deg // 2
    for deg in sorted(by_deg):
        entries.append((f"Sync + {deg} deg", by_deg[deg]))
    return tuple(entries)


PARAMS: list[Param] = [
    # --- Oscillators ---
    Param(13, "OSC1 interval (semitones)", GROUP_OSC, "combo",
          choices=tuple((f"+{s}", s) for s in range(0, 73, 12)), default=0, cc=2),
    Param(14, "OSC2 interval (semitones)", GROUP_OSC, "slider", 0, 60, 0, cc=3),
    Param(33, "OSC3 interval (semitones)", GROUP_OSC, "slider", 0, 60, 0, cc=4),
    Param(15, "OSC2 detune", GROUP_OSC, "slider", 0, 512, 0, cc=5),
    Param(34, "OSC3 detune", GROUP_OSC, "slider", 0, 512, 0, cc=8),
    Param(22, "SQR1 level", GROUP_OSC, "slider", 0, 127, 127, cc=9),
    Param(23, "SQR2 level", GROUP_OSC, "slider", 0, 127, 0, cc=12),
    Param(24, "Sub level", GROUP_OSC, "slider", 0, 127, 0, cc=13),
    Param(5, "SQR1 enable", GROUP_OSC, "check", default=1, cc=14),
    Param(6, "SQR2 enable", GROUP_OSC, "check", default=0, cc=15),
    Param(1, "Saw enable", GROUP_OSC, "check", default=0, note="wave mux, needs ENABLE_WAVE_MUX", cc=16),
    Param(2, "Saw2 enable", GROUP_OSC, "check", default=0, note="wave mux", cc=17),
    Param(3, "Tri enable", GROUP_OSC, "check", default=0, note="wave mux", cc=18),
    Param(4, "Sine enable", GROUP_OSC, "check", default=0, note="wave mux", cc=19),

    # --- Sync and PIO ---
    Param(31, "Hard sync topology", GROUP_SYNC, "combo", default=0,
          choices=(("0 - all free running", 0), ("1 - OSC2 masters OSC1", 1), ("2 - OSC1 masters OSC2", 2)),
          note="which oscillator's sideset drives which reset pin; not the note-on phase "
               "reset, which is 'Osc sync / phase align OSC2' below",
          cc=20),
    Param(36, "Soft sync", GROUP_SYNC, "check", default=0,
          note="off = hard sync (cap discharge only), on = slave polls master", cc=21),
    Param(37, "Sub-oscillator divide", GROUP_SYNC, "combo", default=0,
          choices=(("Off", 0), ("Divide by 2", 2), ("Divide by 4", 4)),
          note="output on GP8, needs a mixer input on the carrier to be audible", cc=22),
    Param(17, "Osc sync / phase align OSC2", GROUP_SYNC, "combo", default=0,
          choices=_phase_choices(),
          note="Off leaves the oscillators running through note-on; every other setting "
               "restarts OSC1 and OSC2 together there, the degree entries adding an OSC2 "
               "offset on top. Changing this retriggers all notes.",
          cc=23),

    # --- Envelopes (curves and routing; times live in the a/b/c blocks) ---
    Param(126, "EnvDCO (ADSR3) enabled", GROUP_ENV, "check", default=1, cc=24),
    Param(10, "ADSR3 to osc select", GROUP_ENV, "combo", default=0,
          choices=(("0 - OSC1", 0), ("1 - OSC2", 1), ("2 - OSC1+2", 2), ("3 - OSC3", 3), ("4 - all", 4)),
          cc=25),
    Param(47, "ADSR3 to OSC1 detune", GROUP_ENV, "slider", -511, 511, 0, cc=26),
    Param(48, "ADSR1 attack curve", GROUP_ENV, "slider", 0, 7, 0, cc=27),
    Param(49, "ADSR1 decay curve", GROUP_ENV, "slider", 0, 7, 0, cc=28),
    Param(50, "ADSR2 attack curve", GROUP_ENV, "slider", 0, 7, 0, cc=29),
    Param(51, "ADSR2 decay curve", GROUP_ENV, "slider", 0, 7, 0, cc=30),
    Param(8, "VCA ADSR restart", GROUP_ENV, "check", default=0, cc=31),
    Param(9, "VCF ADSR restart", GROUP_ENV, "check", default=0, cc=33),

    # --- Filter ---
    Param(19, "VCF keytrack", GROUP_FILTER, "slider", -256, 255, 0, cc=49),
    Param(20, "Velocity to VCF", GROUP_FILTER, "slider", 0, 20, 0, cc=50),
    Param(7, "Resonance amp compensation", GROUP_FILTER, "check", default=0, cc=51),

    # --- PWM ---
    Param(45, "LFO2 to PW", GROUP_PWM, "slider", 0, 511, 0, cc=56),
    Param(46, "ADSR3 to PWM", GROUP_PWM, "slider", 0, 1023, 512,
          note="512 is centre; the DCO subtracts 512 internally", cc=57),
    Param(124, "PWM pots manual", GROUP_PWM, "check", default=1, cc=58),

    # --- LFOs ---
    Param(11, "LFO1 waveform", GROUP_LFO, "combo", default=1,
          choices=(("0 - off", 0), ("1 - saw", 1), ("2 - triangle", 2), ("3 - sine", 3), ("4 - square", 4)),
          cc=60),
    Param(12, "LFO2 waveform", GROUP_LFO, "combo", default=1,
          choices=(("0 - off", 0), ("1 - saw", 1), ("2 - triangle", 2), ("3 - sine", 3), ("4 - square", 4)),
          cc=61),
    Param(41, "LFO1 speed", GROUP_LFO, "slider", 0, 4095, 0, cc=62),
    Param(42, "LFO2 speed", GROUP_LFO, "slider", 0, 4095, 0, cc=63),
    Param(40, "LFO1 to DCO", GROUP_LFO, "slider", 0, 511, 0, cc=65),
    Param(44, "LFO1 to VCA", GROUP_LFO, "slider", 0, 1023, 0, cc=66),
    Param(16, "LFO2 to OSC2 detune", GROUP_LFO, "slider", 0, 255, 0, cc=67),
    Param(35, "LFO2 to OSC3 detune", GROUP_LFO, "slider", 0, 255, 0, cc=68),

    # --- Voice and Drift ---
    Param(26, "Voice mode", GROUP_VOICE, "combo", default=0,
          choices=(("0 - mono", 0), ("1 - poly", 1), ("2 - stack", 2)), cc=69),
    Param(27, "Unison detune", GROUP_VOICE, "slider", 0, 127, 0, cc=70),
    Param(18, "Portamento time", GROUP_VOICE, "slider", 0, 255, 0, cc=71),
    Param(32, "Portamento mode", GROUP_VOICE, "combo", default=0,
          choices=(("0 - fixed time", 0), ("1 - slew rate", 1)), cc=72),
    Param(28, "Analog drift amount", GROUP_VOICE, "slider", 0, 127, 0, cc=73),
    Param(29, "Analog drift speed", GROUP_VOICE, "slider", 1, 255, 1, cc=74),
    Param(30, "Analog drift spread", GROUP_VOICE, "slider", 1, 127, 1, cc=75),
    Param(43, "VCA level", GROUP_VOICE, "slider", 0, 128, 128, cc=76),
    Param(21, "Velocity to VCA", GROUP_VOICE, "slider", 0, 20, 0, cc=77),

    # --- Calibration ---
    # The two pulses keep cc=None on purpose: autotune takes over the board for a minute
    # and the store writes the filesystem, neither of which should be one stray CC away.
    Param(150, "Run autotune", GROUP_CAL, "pulse", pulse_value=1),
    Param(151, "Manual calibration mode", GROUP_CAL, "check", default=0, cc=78),
    Param(152, "Manual cal stage (osc)", GROUP_CAL, "slider", 0, 2, 0, cc=79),
    Param(153, "Manual cal offset", GROUP_CAL, "slider", -20, 20, 0, cc=80),
    Param(156, "Store manual cal offsets", GROUP_CAL, "pulse", pulse_value=1),
]


def _adsr_builder(cmd: bytes):
    def build(values: dict) -> bytes:
        return protocol.adsr_block(
            cmd,
            protocol.lin_to_exp(values["attack"]),
            protocol.lin_to_exp(values["decay"]),
            values["sustain"],
            protocol.lin_to_exp(values["release"]),
        )

    return build


def _adsr_fields(ccs: tuple, sustain_default: int = 4095) -> tuple:
    return (
        BlockField("attack", "Attack", 0, 4095, 0, exp=True, cc=ccs[0]),
        BlockField("decay", "Decay", 0, 4095, 1200, exp=True, cc=ccs[1]),
        BlockField("sustain", "Sustain", 0, 4095, sustain_default, cc=ccs[2]),
        BlockField("release", "Release", 0, 4095, 600, exp=True, cc=ccs[3]),
    )


BLOCKS: list[Block] = [
    Block("adsr_vca", "EnvVCA times", GROUP_ENV, _adsr_fields((34, 35, 36, 37)),
          _adsr_builder(protocol.CMD_ADSR1_BLOCK),
          note="attack, decay and release are exp-mapped on the wire; sustain is linear"),
    Block("adsr_vcf", "EnvVCF times", GROUP_ENV, _adsr_fields((39, 40, 41, 43)),
          _adsr_builder(protocol.CMD_ADSR2_BLOCK)),
    Block("adsr_dco", "EnvDCO times (pitch and PW)", GROUP_ENV, _adsr_fields((44, 45, 46, 47)),
          _adsr_builder(protocol.CMD_ADSR3_BLOCK)),
    Block("adsr1_to_vca", "EnvVCA to VCA amount", GROUP_ENV,
          (BlockField("amount", "ADSR1 to VCA", 0, 512, 512, cc=48),),
          lambda v: protocol.adsr1_to_vca(v["amount"])),
    Block("filter", "Filter block", GROUP_FILTER,
          (
              BlockField("cutoff", "Cutoff", 0, 4095, 4095, cc=52),
              BlockField("resonance", "Resonance", 0, 4095, 0, cc=53),
              BlockField("adsr2_to_vcf", "EnvVCF to cutoff", 0, 512, 0, cc=54),
              BlockField("lfo2_to_vcf", "LFO2 to cutoff", 0, 512, 0, cc=55),
          ),
          lambda v: protocol.filter_block(v["cutoff"], v["resonance"], v["adsr2_to_vcf"], v["lfo2_to_vcf"])),
    Block("pw", "Pulse width", GROUP_PWM,
          (BlockField("pw", "PW", 0, 4095, 2048, cc=59),),
          lambda v: protocol.pw(v["pw"]),
          note="the DCO stores this as value / 4"),
]


# Diagnostic buttons, all PARAM_DEBUG_COMMAND (160). See DCO/docs/PIO_OSCILLATORS.md
# section 12. The period probes only hold while no note is playing, because voice_task()
# pushes a fresh divider every frame for a held note.
DEBUG_COMMANDS = (
    ("PIO topology report", 1),
    ("Period probe, clk_div 2000", 2),
    ("Period probe, clk_div 20000", 3),
)
DEBUG_PARAM_ID = 160
