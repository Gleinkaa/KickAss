import json, os, subprocess, sys, numpy as np, soundfile as sf
S = sys.argv[1]; REF = sys.argv[2]; SR = 48000; KF = os.environ.get("KICKFIT", S + "/harness/build/KickFit_artefacts/Release/KickFit")
fit = json.load(open(S + "/fit2_w30.json")); st, mid, sw1, sw2, pc, off = fit["pitch"]
st, mid, sw1, sw2, pc = round(st), round(mid), round(sw1, 1), round(sw2 * 2) / 2, round(pc, 1)
PHOFF = float(sys.argv[4]) if len(sys.argv) > 4 else round((off * 360) % 360)
kn = json.load(open(S + "/final_spec.json"))["params"]
st, mid, sw1, sw2, pc, PHOFF = (kn[k] for k in ["start_freq", "mid_freq", "sweep_time_1", "sweep_time_2", "pitch_curve", "phase_offset"])
print("knobs:", st, mid, sw1, sw2, pc, "phase_offset", PHOFF)
ref, _ = sf.read(REF); ref = ref if ref.ndim == 1 else ref.mean(1)
def onset(y): return int(np.argmax(np.abs(y) > 1e-3 * np.abs(y).max()))
def hpeaks(y):
    s = np.signbit(y); e = np.where(s[:-1] != s[1:])[0] + 1
    k = np.array([a + np.argmax(np.abs(y[a:b])) for a, b in zip(e[:-1], e[1:])])
    return k / SR, np.abs(y[k])
ref = ref[onset(ref):]
rt, rv = hpeaks(ref[: int(0.2 * SR)])
L = 60 / 152 / 2 * 1000                                # 197.368 ms, 1/8 at 152 BPM
SCALE = 0.55
T = [0, 0.4, 1.2, 2.5, 4.5, 7.5, 11.5, 16, 22, 30, 40, 52, 100, 150, 180, L]
target = lambda tms: SCALE * np.interp(np.array(tms) / 1000, rt, rv, left=rv[0])
V = list(target(T)); V[-1] = 0.0
params = dict(start_freq=st, mid_freq=mid, end_freq=41.20, sweep_time_1=sw1, sweep_time_2=sw2,
              pitch_curve=pc, drive=1.0, tail_drive=0.0, sat_type=2, scoop_depth=0, click_vol=0,
              invert_phase=1, safety_limit=1, phase_offset=PHOFF, pitch_track=0, envelope_mode=1, output_gain=0.0)
def render(V, gain, out="kf.wav", preset=None):
    params["output_gain"] = gain
    json.dump({"params": params, "curve": [[t, float(v), 0.0] for t, v in zip(T, V)]}, open(S + "/spec.json", "w"))
    cmd = [KF, S + "/spec.json", S + "/" + out, "400"] + ([preset] if preset else [])
    subprocess.run(cmd, check=True, capture_output=True)
    y, _ = sf.read(S + "/" + out); return y[onset(y):]
for it in range(10):
    y = render(V, 0.0)
    kt, kv = hpeaks(y[: int(0.2 * SR)])
    meas = np.interp(np.array(T[:-1]) / 1000, kt, kv, left=kv[0])
    err = 20 * np.log10(meas / target(T[:-1]))
    print(f"iter {it}: env err dB max={np.abs(err).max():.2f} rms={np.sqrt((err**2).mean()):.2f}")
    if np.abs(err).max() < 0.15: break
    V[:-1] = list(np.clip(np.array(V[:-1]) * (target(T[:-1]) / meas) ** 0.8, 0.001, 1.0))
peak = np.abs(y).max(); gain = round(float(np.clip(20 * np.log10(10 ** (-0.5 / 20) / peak), -24, 6)), 1)
y = render(V, gain, "kickass_final.wav", sys.argv[3] if len(sys.argv) > 3 else None)
print("output_gain", gain, "peak dBFS %.2f" % (20 * np.log10(np.abs(y).max())))
json.dump({"params": params, "curve": [[t, float(v), 0.0] for t, v in zip(T, V)]}, open(S + "/final_spec.json", "w"), indent=1)
# ---- compare
n = min(len(y), len(ref), int(0.25 * SR)); a, b = ref[:n] / np.abs(ref[:n]).max(), y[:n] / np.abs(y[:n]).max()
print("waveform corr: 0-20ms %.3f  20-100ms %.3f  100-197ms %.3f  all %.3f" % tuple(
    np.corrcoef(a[i:j], b[i:j])[0, 1] for i, j in [(0, 960), (960, 4800), (4800, 9473), (0, 9473)]))
print("residual energy vs ref: %.1f dB" % (10 * np.log10(np.sum((a - b) ** 2) / np.sum(a ** 2))))
kt, kv = hpeaks(y[: int(0.2 * SR)]); kv = kv / kv.max(); rvn = rv / rv.max()
for ms in [1, 5, 10, 20, 30, 50, 80, 120, 160, 185, 195]:
    print(f"  {ms:4d}ms ref {np.interp(ms/1000, rt, rvn):.3f}  kickass {np.interp(ms/1000, kt, kv):.3f}")
zc = lambda z: (lambda i: (i + (-z[i]) / (z[i+1] - z[i])) / SR)(np.where((z[:-1] < 0) & (z[1:] >= 0))[0])
for name, z in [("ref", ref), ("kickass", y)]:
    c = zc(z[: int(0.21 * SR)]); last = c[c < L / 1000 + 0.005][-3:]
    print(f"{name:8s} last upward crossings (ms): {np.round(last * 1000, 2)}  tail Hz {1/np.diff(last).mean():.2f}")
