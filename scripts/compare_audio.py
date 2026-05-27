#!/usr/bin/env python
"""
KickAss DSP parity test.

Renders the same preset in:
  (a) the original BazzismRebuild.py engine
  (b) the KickAss VST3/Standalone (via subprocess + WAV capture, or by directly loading
      a rendered WAV produced by the standalone's --export flag if implemented)

Compares them via FFT band-energy diff. Expected differences:
  - <8 Hz: removed by DC blocker (acceptable, will read as -inf dB in Python)
  - >Nyquist*0.45 due to anti-aliasing of the oversampler
  - Peak slightly under -0.3 dBFS in KickAss (soft-clip ceiling)

Usage:
    python scripts/compare_audio.py

For now (Phase 2), this script:
  1. Renders the Psytrance default preset via the Python engine to reference/python_psytrance.wav
  2. Prints the expected output for visual diff.

Phase 6 extension: drive KickAss standalone via OSC or command-line to render same preset,
then auto-diff the FFTs.
"""

import os
import sys
import numpy as np
from scipy.io import wavfile

# Allow `python scripts/compare_audio.py` from project root
PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(PROJECT_ROOT, "reference"))

# Import the Python engine's DSP. The Tk GUI would normally fire on import, so we work
# around it by importing only the DSP-relevant constants.
SAMPLE_RATE = 44100

# --- Replicate the Python DSP inline so we don't depend on the Tk GUI starting ---

PRESET_PSYTRANCE = {
    "start_freq": 8000.0, "mid_freq": 150.0, "end_freq": 49.0,
    "sweep_time_1": 10.0, "sweep_time_2": 80.0, "pitch_curve": 3.0,
    "vol_attack": 2.0, "vol_hold": 10.0, "vol_decay_1": 50.0,
    "vol_sustain": 40.0, "vol_decay_2": 150.0, "vol_curve": 3.0,
    "scoop_start": 10.0, "scoop_length": 30.0, "scoop_depth": 0.0,
    "drive": 1.5, "click_vol": 0.0, "tail_drive": 0.0, "invert_phase": False
}

PRESET_PROJEKTOR_PUNCH = {
    "start_freq": 12000.0, "mid_freq": 350.0, "end_freq": 49.0,
    "sweep_time_1": 8.0, "sweep_time_2": 80.0, "pitch_curve": 4.0,
    "vol_attack": 0.5, "vol_hold": 15.0, "vol_decay_1": 60.0,
    "vol_sustain": 30.0, "vol_decay_2": 250.0, "vol_curve": 3.5,
    "scoop_start": 15.0, "scoop_length": 40.0, "scoop_depth": 10.0,
    "drive": 2.5, "click_vol": 0.15, "tail_drive": 1.0, "invert_phase": False
}


def render_python(p, sample_rate=SAMPLE_RATE):
    """Port of BazzismRebuild.py::generate_audio_data() — pure numpy, no Tk."""
    a_ms = p["vol_attack"]; h_ms = p["vol_hold"]; d1_ms = p["vol_decay_1"]
    sus_pct = p["vol_sustain"] / 100.0; d2_ms = p["vol_decay_2"]; v_curve = p["vol_curve"]

    total_ms = a_ms + h_ms + d1_ms + d2_ms
    if total_ms <= 0: total_ms = 1
    duration = total_ms / 1000.0
    t = np.linspace(0, duration, int(sample_rate * duration), endpoint=False)

    # Pitch envelope
    f_start = p["start_freq"]; f_mid = p["mid_freq"]; f_end = p["end_freq"]
    t1 = p["sweep_time_1"] / 1000.0; t2 = p["sweep_time_2"] / 1000.0
    p_curve = p["pitch_curve"]
    freqs = np.zeros_like(t)
    m1 = t < t1
    if np.any(m1):
        x1 = np.clip(t[m1] / t1, 0.0, 1.0) if t1 > 0 else 1.0
        freqs[m1] = f_mid + (f_start - f_mid) * ((1.0 - x1) ** p_curve)
    m2 = (t >= t1) & (t < t1 + t2)
    if np.any(m2):
        x2 = np.clip((t[m2] - t1) / t2, 0.0, 1.0) if t2 > 0 else 1.0
        freqs[m2] = f_end + (f_mid - f_end) * ((1.0 - x2) ** p_curve)
    m3 = t >= t1 + t2
    if np.any(m3):
        freqs[m3] = f_end

    phase = np.cumsum(freqs) / sample_rate
    audio = np.sin(2 * np.pi * phase)

    # AHDSR
    amp_env = np.zeros_like(t)
    p1e = a_ms / 1000.0
    p2e = p1e + h_ms / 1000.0
    p3e = p2e + d1_ms / 1000.0
    m_a = t < p1e
    if np.any(m_a):
        x = np.clip(t[m_a] / (a_ms / 1000.0), 0.0, 1.0) if a_ms > 0 else 1.0
        amp_env[m_a] = x ** (1.0 / v_curve)
    m_h = (t >= p1e) & (t < p2e)
    if np.any(m_h): amp_env[m_h] = 1.0
    m_d1 = (t >= p2e) & (t < p3e)
    if np.any(m_d1):
        x = np.clip((t[m_d1] - p2e) / (d1_ms / 1000.0), 0.0, 1.0) if d1_ms > 0 else 1.0
        amp_env[m_d1] = sus_pct + (1.0 - sus_pct) * ((1.0 - x) ** v_curve)
    m_d2 = t >= p3e
    if np.any(m_d2):
        x = np.clip((t[m_d2] - p3e) / (d2_ms / 1000.0), 0.0, 1.0) if d2_ms > 0 else 1.0
        amp_env[m_d2] = sus_pct * ((1.0 - x) ** v_curve)

    # Scoop
    s_start = p["scoop_start"] / 1000.0
    s_len = p["scoop_length"] / 1000.0
    s_depth = p["scoop_depth"] / 100.0
    if s_depth > 0 and s_len > 0:
        m_s = (t >= s_start) & (t < s_start + s_len)
        if np.any(m_s):
            sp = (t[m_s] - s_start) / s_len
            amp_env[m_s] *= 1.0 - (np.sin(sp * np.pi) * s_depth)

    audio = audio * amp_env

    # Click (sine chirp)
    if p["click_vol"] > 0:
        click_len = int(0.005 * sample_rate)
        click_len = min(click_len, len(t))
        c_freqs = np.linspace(10000, 2000, click_len)
        c_phase = np.cumsum(c_freqs) / sample_rate
        click_snd = np.sin(2 * np.pi * c_phase) * np.linspace(1, 0, click_len) ** 2
        click_track = np.zeros_like(t)
        click_track[:click_len] = click_snd * p["click_vol"]
        audio = audio + click_track

    # Drive
    base = p["drive"]; tail = p["tail_drive"]
    if tail > 0:
        ramp = np.linspace(0, 1, len(audio)) ** 2
        drive_arr = base + ramp * tail * 5.0
        audio = np.tanh(audio * drive_arr) / np.tanh(np.max(drive_arr))
    else:
        audio = np.tanh(audio * base) / np.tanh(base)

    if p["invert_phase"]:
        audio = -audio

    # 100-sample tail fade
    fade = min(100, len(audio))
    if fade > 0:
        audio[-fade:] *= np.linspace(1, 0, fade)

    return audio.astype(np.float32)


def fft_db(audio, sample_rate):
    """Return (freqs_hz, magnitude_db) for half-spectrum FFT."""
    n = len(audio)
    spec = np.abs(np.fft.rfft(audio * np.hanning(n))) / n
    freqs = np.fft.rfftfreq(n, 1.0 / sample_rate)
    mag_db = 20.0 * np.log10(np.maximum(spec, 1e-10))
    return freqs, mag_db


def band_summary(audio, sample_rate, label):
    freqs, mag_db = fft_db(audio, sample_rate)
    bands = [
        ("Sub      20-60 Hz",  20,    60),
        ("Low body 60-150 Hz", 60,    150),
        ("Mid body 150-500 Hz", 150,  500),
        ("UpperMid 500Hz-2kHz", 500,  2000),
        ("Click    2kHz-10kHz", 2000, 10000),
        ("Air      10k-20kHz",  10000, 20000),
    ]
    print(f"\n=== {label} ===")
    print(f"  Peak:     {20*np.log10(np.max(np.abs(audio)) + 1e-10):+6.2f} dBFS")
    print(f"  RMS:      {20*np.log10(np.sqrt(np.mean(audio*audio)) + 1e-10):+6.2f} dBFS")
    print(f"  DC mean:  {np.mean(audio):+.6f}")
    print(f"  Duration: {len(audio) / sample_rate * 1000:.1f} ms")
    for name, lo, hi in bands:
        mask = (freqs >= lo) & (freqs < hi)
        if np.any(mask):
            band_db = 20 * np.log10(np.sqrt(np.mean(10**(mag_db[mask]/10))) + 1e-10)
            print(f"  {name:20s}: {band_db:+6.2f} dB")


def main():
    out_dir = os.path.join(PROJECT_ROOT, "reference", "renders")
    os.makedirs(out_dir, exist_ok=True)

    for name, preset in [
        ("psytrance_default",  PRESET_PSYTRANCE),
        ("projektor_punch",    PRESET_PROJEKTOR_PUNCH),
    ]:
        py_audio = render_python(preset, SAMPLE_RATE)
        wav_path = os.path.join(out_dir, f"python_{name}.wav")
        wavfile.write(wav_path, SAMPLE_RATE, (py_audio * 32767).astype(np.int16))
        print(f"\nWrote {wav_path}")
        band_summary(py_audio, SAMPLE_RATE, f"PYTHON · {name}")

    print("\n---")
    print("Phase 2 parity step:")
    print("  1. Run KickAss standalone, load each preset, render to WAV via DAW")
    print("     (or wire up a '--render-preset NAME OUT.wav' CLI mode in Phase 6).")
    print("  2. Run band_summary on the C++ output and compare.")
    print("  3. Expected deltas:")
    print("     - Sub band: ~0 dB (DC blocker only removes <8 Hz)")
    print("     - Low/Mid body bands: ~0 dB (no signal-path change)")
    print("     - Click/Air bands: KickAss should be quieter due to 4x anti-aliasing")
    print("     - Peak: KickAss should be <= -0.3 dBFS (soft-clip ceiling)")


if __name__ == "__main__":
    main()
