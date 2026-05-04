#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Compare backend filter semantics between REAPER/JSFX and Python oracle."""

from __future__ import annotations

import argparse
import math
import sys
from dataclasses import dataclass
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / "tools"))

import keller_oracle as ko  # noqa: E402
from abx_compare import read_wav_f32, write_wav_f32  # noqa: E402
from tools.jsfx_render.render_jsfx import render  # noqa: E402
from tools.jsfx_render.stage_jsfx import DEFAULT_DEST, stage  # noqa: E402

SAMPLE_RATE = 48_000
N = 4_800
PROBE_EFFECT = "nilamp_abx/filter_semantics_probe"
PROBE_SOURCE = DEFAULT_DEST / "filter_semantics_probe.jsfx"

K1 = 0.797
K2 = 0.940
HP3_HZ = 5.8
HP4_HZ = 6.4
KP1 = 1.1220184543
FP_HZ = 80.0
QP1 = 2.6685237666
KS1 = 1.4125375446
FS1_HZ = 2098.1359672


@dataclass(frozen=True)
class Mode:
    index: int
    name: str


MODES = [
    Mode(0, "hp3"),
    Mode(1, "hp4"),
    Mode(2, "peq1"),
    Mode(3, "hs1"),
    Mode(4, "peq1_hs1"),
    Mode(5, "t4_pre_chain"),
    Mode(6, "t5_pre_chain"),
]


class Smooth:
    def __init__(self, target: float, sr: int, smoothing: bool):
        self.act = 0.0
        self.new = float(target)
        if smoothing:
            self.slope = abs(self.new - self.act) / 0.01 / sr
        else:
            self.slope = 0.0

    def process(self) -> float:
        if self.slope == 0.0:
            self.act = self.new
        elif self.act < self.new:
            self.act = min(self.act + self.slope, self.new)
        elif self.act > self.new:
            self.act = max(self.act - self.slope, self.new)
        return self.act


class SmoothPeq:
    def __init__(self, kgain: float, f: float, qc: float, pwf: int, pwq: int, sr: int, smoothing: bool):
        pi_t = math.pi / sr
        k_for_q = f * pi_t
        if pwq == 0:
            kq = 1.0 / (math.sqrt(kgain) * qc)
        else:
            q_eff = qc * math.sqrt(kgain)
            aux1 = math.sqrt(1.0 + 4.0 * q_eff * q_eff)
            aux2 = (k_for_q / math.sin(2.0 * k_for_q)) * math.log((aux1 + 1.0) / (aux1 - 1.0))
            kq = math.exp(aux2) - math.exp(-aux2)
        k = f * pi_t
        if pwf == 1:
            k = math.tan(f * pi_t)
        self.k = Smooth(k, sr, smoothing)
        self.kf = Smooth(kq + k, sr, smoothing)
        self.kdiv = Smooth(1.0 / (1.0 + k * (k + kq)), sr, smoothing)
        self.b0 = Smooth(1.0, sr, smoothing)
        self.kb1 = Smooth(kq * kgain, sr, smoothing)
        self.b2 = Smooth(1.0, sr, smoothing)
        self.s1 = 0.0
        self.s2 = 0.0

    def process_sample(self, x: float) -> np.float32:
        k = self.k.process()
        kf = self.kf.process()
        kdiv = self.kdiv.process()
        b0 = self.b0.process()
        kb1 = self.kb1.process()
        b2 = self.b2.process()
        hp = (float(x) - kf * self.s1 - self.s2) * kdiv
        aux = k * hp
        bp = aux + self.s1
        self.s1 = aux + bp
        aux = k * bp
        lp = aux + self.s2
        self.s2 = aux + lp
        return np.float32(b0 * hp + kb1 * bp + b2 * lp)


class SmoothHs:
    def __init__(self, kgain: float, fs: float, pwf: int, sr: int, smoothing: bool):
        k_raw = fs * math.pi / sr
        if pwf == 1:
            k_raw = math.tan(fs * math.pi / sr)
        k = math.sqrt(kgain) * k_raw
        self.k = Smooth(k, sr, smoothing)
        self.kdiv = Smooth(1.0 / (1.0 + k), sr, smoothing)
        self.b0 = Smooth(kgain, sr, smoothing)
        self.b1 = Smooth(1.0, sr, smoothing)
        self.s1 = 0.0

    def process_sample(self, x: float) -> np.float32:
        k = self.k.process()
        kdiv = self.kdiv.process()
        b0 = self.b0.process()
        b1 = self.b1.process()
        hp = (float(x) - self.s1) * kdiv
        aux = k * hp
        lp = aux + self.s1
        self.s1 = aux + lp
        return np.float32(b0 * hp + b1 * lp)


def probe_input() -> np.ndarray:
    t = np.arange(N, dtype=np.float64) / SAMPLE_RATE
    x = (
        0.35 * np.sin(2.0 * np.pi * 73.0 * t)
        + 0.18 * np.sin(2.0 * np.pi * 997.0 * t)
        + 0.08 * np.sin(2.0 * np.pi * 6100.0 * t)
    )
    x[:32] += np.linspace(0.25, -0.25, 32)
    x[N // 3:] += 0.05
    return x.astype(np.float32)


def process_peq(x: np.ndarray, smoothing: bool) -> np.ndarray:
    flt = SmoothPeq(KP1, FP_HZ, QP1, 1, 1, SAMPLE_RATE, smoothing)
    out = np.empty_like(x, dtype=np.float32)
    for i, sample in enumerate(x):
        out[i] = flt.process_sample(sample)
    return out


def process_hs(x: np.ndarray, smoothing: bool) -> np.ndarray:
    flt = SmoothHs(KS1, FS1_HZ, 1, SAMPLE_RATE, smoothing)
    out = np.empty_like(x, dtype=np.float32)
    for i, sample in enumerate(x):
        out[i] = flt.process_sample(sample)
    return out


def oracle(mode: Mode, x: np.ndarray, smoothing: bool) -> np.ndarray:
    if mode.name == "hp3":
        return ko.flt_ii1_hp_block(HP3_HZ, SAMPLE_RATE, x)
    if mode.name == "hp4":
        return ko.flt_ii1_hp_block(HP4_HZ, SAMPLE_RATE, x)
    if mode.name == "peq1":
        return process_peq(x, smoothing)
    if mode.name == "hs1":
        return process_hs(x, smoothing)
    if mode.name == "peq1_hs1":
        return process_hs(process_peq(x, smoothing), smoothing)
    if mode.name == "t4_pre_chain":
        hp = ko.flt_ii1_hp_block(HP3_HZ, SAMPLE_RATE, (x * K1).astype(np.float32))
        return process_hs(process_peq(hp, smoothing), smoothing)
    if mode.name == "t5_pre_chain":
        hp = ko.flt_ii1_hp_block(HP4_HZ, SAMPLE_RATE, (x * K2).astype(np.float32))
        return process_hs(process_peq(hp, smoothing), smoothing)
    raise ValueError(f"unknown mode: {mode}")


def compare(actual: np.ndarray, expected: np.ndarray) -> tuple[float, float]:
    n = min(len(actual), len(expected))
    diff = actual[:n] - expected[:n]
    return float(np.max(np.abs(diff))), float(np.sqrt(np.mean(diff.astype(np.float64) ** 2)))


def selected_modes(spec: str) -> list[Mode]:
    if spec == "all":
        return MODES
    wanted = {s.strip() for s in spec.split(",") if s.strip()}
    modes = [m for m in MODES if m.name in wanted or str(m.index) in wanted]
    missing = wanted - {m.name for m in modes} - {str(m.index) for m in modes}
    if missing:
        raise SystemExit(f"unknown mode(s): {sorted(missing)}")
    return modes


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--mode", default="all", help="Mode name/index, comma list, or 'all'.")
    ap.add_argument("--smoothing", type=int, choices=(0, 1), default=0)
    ap.add_argument("--out-dir", type=Path, default=Path("/tmp/filter_semantics_probe"))
    ap.add_argument("--timeout", type=float, default=60.0)
    ap.add_argument("--max-abs", type=float, default=1e-4)
    ap.add_argument("--max-rms", type=float, default=1e-5)
    ap.add_argument("--no-stage", action="store_true", help="Do not restage JSFX files before rendering.")
    args = ap.parse_args()

    if not args.no_stage:
        stage(DEFAULT_DEST)

    args.out_dir.mkdir(parents=True, exist_ok=True)
    x = probe_input()
    input_wav = args.out_dir / "probe_input.wav"
    write_wav_f32(input_wav, x, SAMPLE_RATE)

    ok = True
    smoothing = bool(args.smoothing)
    for mode in selected_modes(args.mode):
        jsfx_wav = args.out_dir / f"{mode.name}_jsfx_s{args.smoothing}.wav"
        render(
            input_wav=input_wav,
            output_wav=jsfx_wav,
            sliders={"mode": mode.index, "smoothing": args.smoothing},
            sample_rate=SAMPLE_RATE,
            timeout_s=args.timeout,
            effect_name=PROBE_EFFECT,
            jsfx_source=PROBE_SOURCE,
        )
        actual, sr = read_wav_f32(jsfx_wav)
        if sr != SAMPLE_RATE:
            raise RuntimeError(f"{mode.name}: sample-rate mismatch: {sr}")
        expected = oracle(mode, x, smoothing)
        max_abs, rms = compare(np.asarray(actual, dtype=np.float32), expected)
        passed = max_abs <= args.max_abs and rms <= args.max_rms
        ok = ok and passed
        verdict = "OK" if passed else "FAIL"
        print(f"{verdict} {mode.name:<16} smoothing={args.smoothing} max_abs={max_abs:.3e} rms={rms:.3e}")

    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
