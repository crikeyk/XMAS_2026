#!/usr/bin/env python3
"""Compare the C content/tracker with the archived v8 Python."""
import math
import pathlib
import subprocess
import sys
import types

root = pathlib.Path(__file__).resolve().parents[1]
repo = root.parents[1]
py_src = (repo / "Firmware/archive/circuitpython/device/code.py").read_text()
py_src = py_src.replace("\nwhile True:\n", "\nif False:\n", 1)
seed = root / "seed"

for name in ("digitalio", "microcontroller", "alarm", "leds", "accel"):
    sys.modules[name] = types.ModuleType(name)

class _Pin:
    def __init__(self):
        self.value = 0

class _Spi:
    def write(self, *a):
        pass
    def readinto(self, buf):
        for i in range(len(buf)):
            buf[i] = 0

class _Cs:
    value = True

class Leds:
    def __init__(self, *a, **k):
        self._cath = [_Pin() for _ in range(16)]

class Accel:
    def __init__(self):
        self._spi = _Spi()
        self._cs = _Cs()
    def _write(self, *a):
        pass

sys.modules["leds"].Leds = Leds
sys.modules["accel"].Accel = Accel
sys.modules["microcontroller"].pin = types.SimpleNamespace(GPIO23=23)
sys.modules["alarm"].pin = types.SimpleNamespace(PinAlarm=lambda **k: None)
sys.modules["alarm"].wake_alarm = None

real_open = open

def fake_open(path, *a, **k):
    name = str(path)
    if name == "/settings.txt":
        return real_open(seed / "settings.txt", *a, **k)
    if name == "/message.txt":
        return real_open(seed / "message.txt", *a, **k)
    if name == "/image.bmp":
        return real_open(seed / "image.bmp", *a, **k)
    return real_open(path, *a, **k)

import builtins
builtins.open = fake_open
ns = {}
exec(compile(py_src, "code.py", "exec"), ns)
builtins.open = real_open

anchor = {"auto": 0, "zero": 1, "peaks": 2}[ns["ANCHOR"]]
show = {"text": 0, "image": 1, "both": 2}[ns["SHOW"]]
passes = 1 if ns["PASSES"] == "right" else (-1 if ns["PASSES"] == "left" else 0)
expect = {
    "SHOW": show,
    "SECONDS": ns["SECONDS"],
    "STRETCH": ns["STRETCH"],
    "LIT_DARK": 1 if ns["LIT"] == "dark" else 0,
    "PASSES": passes,
    "FLIP": 1 if ns["FLIP_VERTICAL"] else 0,
    "SIGN": ns["SIGN"],
    "SHIFT": ns["SHIFT_MS"],
    "CAL": 1 if ns["CALIBRATE"] else 0,
    "ANCHOR": anchor,
    "WIDTH": float(ns["WIDTH"]),
    "DEBUG": 1 if ns["DEBUG"] else 0,
    "SLEEP": 1 if ns["SLEEP"] else 0,
    "SLEEP_S": ns["SLEEP_S"],
    "WAKE": ns["WAKE_G"],
    "INT_TEST": 1 if ns["INT_TEST"] else 0,
}

text_masks, _pitch = ns["build_text_frame"]((seed / "message.txt").read_text())
ns["LIT"] = "dark"
bmp_masks = ns["load_bmp"](str(seed / "image.bmp"))

def half_of(n, pitch):
    half = n * pitch * ns["STRETCH"] / 2
    limit = ns["WIDTH"] / 100.0
    return limit if half > limit else half

# Replay the same peak events the C program feeds, on a fresh copy of the oscillator.
ns["phi"] = 0.0
ns["w"] = math.pi / 0.15
ns["t_phi"] = 0.0
ns["t_c"] = 0.0
ns["t_last"] = 0.0
ns["ev_last"] = 0
ns["locked"] = False
ns["sess_active"] = False
ns["d"] = 0
ns["out_run"] = 0
ns["pk"] = 0.0
ns["avg_pk"] = 0.0
tc = 0.0
for ev in (2, -2, 2, -2, 2, -2, 2, -2, 2, -2, 2, -2):
    tc += 0.11
    ns["handle_event"](tc, ev)

bin_path = root / "host" / "host_check"
out = subprocess.check_output(
    [str(bin_path), str(seed / "settings.txt"), str(seed / "message.txt"), str(seed / "image.bmp")],
    text=True,
)
got = {}
masks = {}
for line in out.splitlines():
    if "_MASKS" in line:
        tag, rest = line.split("_MASKS", 1)
        masks[tag] = [int(x, 16) for x in rest.split(",") if x.strip()]
        continue
    key, val = line.split(" ", 1)
    got[key] = val

bad = 0

def fail(msg):
    global bad
    bad += 1
    print("MISMATCH", msg)

for key, exp in expect.items():
    g = float(got[key]) if isinstance(exp, float) else int(float(got[key]))
    if isinstance(exp, float):
        if abs(g - exp) > 1e-6:
            fail(f"{key}: c={g} python={exp}")
    elif g != exp:
        fail(f"{key}: c={g} python={exp}")

def check_masks(tag, py_masks, pitch):
    c_masks = masks[tag]
    if c_masks != py_masks:
        fail(f"{tag} masks differ (c {len(c_masks)} python {len(py_masks)})")
        for i, (a, b) in enumerate(zip(c_masks, py_masks)):
            if a != b:
                print(f"  col {i}: c={a:04X} python={b:04X}")
                break
        if len(c_masks) != len(py_masks):
            print(f"  lengths {len(c_masks)} vs {len(py_masks)}")
    half = float(got[f"{tag}_HALF"])
    want = half_of(len(py_masks), pitch)
    if abs(half - want) > 1e-6:
        fail(f"{tag} half c={half} python={want}")

check_masks("text", text_masks, ns["PITCH_TEXT"])
check_masks("image", bmp_masks, ns["PITCH_IMG"])

phi = float(got["PHI"])
w = float(got["W"])
if abs(phi - ns["phi"]) > 1e-6 or abs(w - ns["w"]) > 1e-6 or int(got["LOCKED"]) != int(bool(ns["locked"])):
    fail(f"tracker phi c={phi} py={ns['phi']} w c={w} py={ns['w']} locked c={got['LOCKED']} py={ns['locked']}")

if int(got["SWEEP_LOCKED"]) < 1 or int(got["SWEEP_COLS"]) < 5:
    fail(f"sine sweep did not lock (locks={got['SWEEP_LOCKED']} cols={got['SWEEP_COLS']})")

if bad:
    print(f"{bad} mismatches")
    sys.exit(1)
print("host check ok")
print(f"  text {len(text_masks)} columns, image {len(bmp_masks)} columns")
print(f"  phi {phi:.6f}  w {w:.6f}")
print(f"  sine sweep locked {got['SWEEP_LOCKED']} times, {got['SWEEP_COLS']} columns")
