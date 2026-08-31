#!/usr/bin/env python3
"""
Pico-to-PlotJuggler SWD Benchmark Bridge (RP2040 / RP2350)
- Sliding-window statistics (Last 1.0s window instead of whole session)
- Live terminal dashboard & real-time plottable moving stats (_avg, _min, _max)
"""

import sys
import os
import time
import socket
import json
import struct
import re
import signal
from collections import deque
from elftools.elf.elffile import ELFFile
from pyocd.core.helpers import ConnectHelper

# --- CONFIGURACIÓN ---
UDP_IP = "127.0.0.1"
UDP_PORT = 9872
POLL_RATE_HZ = 5000            # SWD sampling rate (100 Hz = 100 FPS)
STATS_WINDOW_SEC = 5.0        # Sliding time window duration (in seconds)
DASHBOARD_REFRESH_SEC = 1.0   # How often to refresh terminal dashboard (seconds)
TARGET_CHIP = "rp2350"
CLK_SYS_MHZ = 250

keep_running = True

def signal_handler(signum, frame):
    global keep_running
    keep_running = False

class SlidingWindowStat:
    """Calculates statistics over a sliding time window (FIFO ring buffer)."""
    def __init__(self, maxlen):
        self.history = deque(maxlen=maxlen)
        self.running_sum = 0.0
        self.last = 0.0

    def update(self, val):
        if val > 0:
            if len(self.history) == self.history.maxlen:
                self.running_sum -= self.history[0]
            self.history.append(val)
            self.running_sum += val
            self.last = val

    @property
    def mean(self):
        return (self.running_sum / len(self.history)) if self.history else 0.0

    @property
    def min(self):
        return min(self.history) if self.history else 0.0

    @property
    def max(self):
        return max(self.history) if self.history else 0.0

def get_symbol_info(elf_path, symbol_name):
    """Encuentra la dirección en RAM de bench_telemetry."""
    with open(elf_path, 'rb') as f:
        elf = ELFFile(f)
        symtab = elf.get_section_by_name('.symtab')
        if not symtab:
            raise RuntimeError("No se encontró la tabla de símbolos (.symtab).")
        symbols = symtab.get_symbol_by_name(symbol_name)
        if not symbols:
            raise RuntimeError(f"No se encontró el símbolo '{symbol_name}' en el ELF.")
        sym = symbols[0]
        return sym['st_value'], sym['st_size']

def get_bench_meta_from_elf(elf_path):
    """Extrae el JSON de metadatos directamente del binario ELF."""
    try:
        with open(elf_path, 'rb') as f:
            raw_bytes = f.read()
        match = re.search(b'@@BENCH_META_BEGIN@@(.*?)@@BENCH_META_END@@', raw_bytes, re.DOTALL)
        if match:
            json_str = match.group(1).decode('utf-8', errors='ignore')
            data = json.loads(json_str)
            return [p for p in data if "id" in p]
    except Exception:
        pass
    return None

def get_bench_meta_from_header(header_path="bench.h"):
    """Fallback: extrae las tuplas de BENCH_PROBES expandiendo submacros."""
    search_paths = [header_path, os.path.join("src", header_path), os.path.join("include", header_path)]
    target = next((p for p in search_paths if os.path.exists(p)), None)
    if not target:
        return None

    with open(target, 'r', encoding='utf-8') as f:
        content = f.read()

    probes_match = re.search(r'#define BENCH_PROBES\(X\)(.*?)(?:\n\s*#|\Z)', content, re.DOTALL)
    if not probes_match:
        return None
    probes_body = probes_match.group(1)

    submacros = re.findall(r'([A-Z0-9_]+)\(X\)', probes_body)
    for sub in submacros:
        sub_def = re.search(fr'#define {sub}\(X\)(.*?)(?:\n\s*#|\Z)', content, re.DOTALL)
        if sub_def:
            probes_body = probes_body.replace(f"{sub}(X)", sub_def.group(1))
        else:
            probes_body = probes_body.replace(f"{sub}(X)", "")

    pattern = r'X\s*\(\s*([a-zA-Z0-9_]+)\s*,\s*([0-9]+)\s*,\s*[^,]+\s*,\s*[^,]+\s*,\s*([a-zA-Z0-9_]+)\s*,\s*"([^"]+)"\s*\)'
    matches = re.findall(pattern, probes_body)

    probes = []
    for m in matches:
        if m[0] == 'id':
            continue
        probes.append({
            "id": m[0],
            "core": int(m[1]),
            "parent": m[2],
            "label": m[3]
        })
    return probes

def generate_xml_layout(paths, filename="dco_layout.xml"):
    """Genera layout XML con curvas instantáneas y de promedio para PlotJuggler."""
    xml = "<?xml version='1.0' encoding='UTF-8'?>\n<root>\n <tabbed_widget name=\"Main Window\">\n"

    for core in [0, 1]:
        core_paths = [p for p in paths if f"Core {core}" in p]
        if not core_paths:
            continue

        xml += f'  <Tab tab_name="Core {core} Profiling" containers="1">\n'
        xml += f'   <Container>\n    <DockSplitter>\n     <DockArea name="Core {core} Traces">\n      <plot>\n'
        for p in core_paths:
            xml += f'       <curve name="{p}"/>\n'
            xml += f'       <curve name="{p}/_avg"/>\n'
        xml += '      </plot>\n     </DockArea>\n    </DockSplitter>\n   </Container>\n  </Tab>\n'

    xml += " </tabbed_widget>\n</root>\n"

    with open(filename, "w") as f:
        f.write(xml)

def main():
    global keep_running

    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)

    elf_path = sys.argv[1] if len(sys.argv) > 1 else ".pio/build/rpipico2/firmware.elf"

    telemetry_addr, struct_size = get_symbol_info(elf_path, "bench_telemetry")
    num_fields = struct_size // 4

    probes = get_bench_meta_from_elf(elf_path)
    if not probes:
        probes = get_bench_meta_from_header()
        if not probes:
            probes = [{"id": f"probe_{i}", "core": 0, "parent": "BENCH_NONE", "label": f"probe_{i}"} for i in range(num_fields)]

    if len(probes) != num_fields:
        probes = probes[:num_fields]

    probe_dict = { "BENCH_" + p["id"]: p for p in probes }
    for p in probes:
        p["has_children"] = any(other.get("parent") == "BENCH_" + p["id"] for other in probes)

    paths = []
    for p in probes:
        path_parts = []
        curr = p
        while curr:
            path_parts.insert(0, curr["label"])
            parent_id = curr.get("parent")
            if parent_id in probe_dict and parent_id != "BENCH_NONE":
                curr = probe_dict[parent_id]
            else:
                break

        path_str = f"Core {p['core']}/" + "/".join(path_parts)
        if p["has_children"]:
            path_str += "/_Total"

        paths.append(path_str)

    generate_xml_layout(paths)

    # Configurar el tamaño de la ventana deslizante
    window_samples = max(1, int(POLL_RATE_HZ * STATS_WINDOW_SEC))
    stats = {p: SlidingWindowStat(maxlen=window_samples) for p in paths}

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    session = None

    try:
        session = ConnectHelper.session_with_chosen_probe(
            target_override=TARGET_CHIP,
            options={
                "connect_mode": "attach",
                "resume_on_connect": True,
                "auto_unlock": False,
            }
        )
        session.open()
        target = session.board.target

        for core in target.cores.values():
            try:
                if core.is_halted():
                    core.resume()
            except Exception:
                pass

        interval = 1.0 / POLL_RATE_HZ
        last_dashboard_time = 0.0

        while keep_running:
            t_start = time.time()

            try:
                raw_cycles = target.read_memory_block32(telemetry_addr, num_fields)
                payload = {}

                for path, cyc in zip(paths, raw_cycles):
                    us = round(cyc / CLK_SYS_MHZ, 4)
                    stats[path].update(us)

                    # Transmitir instantáneo, promedio móvil y pico de la ventana
                    payload[path] = us
                    payload[f"{path}/_avg"] = round(stats[path].mean, 2)
                    payload[f"{path}/_min"] = round(stats[path].min, 2)
                    payload[f"{path}/_max"] = round(stats[path].max, 2)

                sock.sendto(json.dumps(payload).encode('utf-8'), (UDP_IP, UDP_PORT))
            except Exception:
                if not keep_running:
                    break

            # Actualizar el Dashboard en consola cada segundo mostrando la ventana actual
            now = time.time()
            if now - last_dashboard_time >= DASHBOARD_REFRESH_SEC:
                last_dashboard_time = now
                sys.stdout.write("\033[H\033[J")
                sys.stdout.write(f"================ DCO LIVE TELEMETRY (Last {STATS_WINDOW_SEC:.1f}s Window) ================\n")
                sys.stdout.write(f"{'PROBE':<42} {'NOW (us)':>9} {'AVG':>8} {'MIN':>7} {'MAX':>7}\n")
                sys.stdout.write("----------------------------------------------------------------------\n")
                for path in paths:
                    st = stats[path]
                    if st.history:
                        sys.stdout.write(f"{path:<42} {st.last:>9.2f} {st.mean:>8.2f} {st.min:>7.2f} {st.max:>7.2f}\n")
                sys.stdout.write("======================================================================\n")
                sys.stdout.write(f"[*] Streaming SWD -> PlotJuggler ({UDP_IP}:{UDP_PORT}) | Ctrl+C para salir\n")
                sys.stdout.flush()

            elapsed = time.time() - t_start
            sleep_time = interval - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)

    finally:
        print("\n[*] Desconectando sesión SWD y liberando sockets...")
        if session is not None:
            try:
                session.close()
            except Exception:
                pass
        try:
            sock.close()
        except Exception:
            pass
        print("[+] Listo.")

if __name__ == "__main__":
    main()
