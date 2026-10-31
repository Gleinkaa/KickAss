# Engine-in-the-loop pitch/phase fit: real KickAss renders vs the Audija reference.
import json, os, subprocess, sys, numpy as np, soundfile as sf
from scipy.optimize import minimize
S = sys.argv[1]; SR = 48000; KF = os.environ.get("KICKFIT", S + "/harness/build/KickFit_artefacts/Release/KickFit"); L = 60/152/2
ref, _ = sf.read(sys.argv[2]); ref = ref if ref.ndim == 1 else ref.mean(1)
r0 = np.argmax(np.abs(ref) > 1e-4); ref = ref[r0 - 1:]           # include the zero sample before the first step
spec = json.load(open(S + "/final_spec.json"))
def zc(z):
    k = np.where((z[:-1] < 0) & (z[1:] >= 0))[0]; return (k + (-z[k]) / (z[k+1] - z[k])) / SR
rz = zc(ref[: int(0.196 * SR)]); rmid = (rz[:-1] + rz[1:]) / 2; rf = 1 / np.diff(rz)
def quant(q):
    st, mid, sw1, sw2, pc, off = q
    return dict(start_freq=float(np.clip(round(st), 100, 15000)), mid_freq=float(np.clip(round(mid), 50, 1000)),
                sweep_time_1=float(np.clip(round(sw1, 1), 0.1, 50)), sweep_time_2=float(np.clip(round(sw2 * 2) / 2, 1, 250)),
                pitch_curve=float(np.clip(round(pc, 1), 0.1, 10)), phase_offset=float(round(off) % 360))
def render(kn, out="e.wav"):
    spec["params"].update(kn); json.dump(spec, open(S + "/espec.json", "w"))
    subprocess.run([KF, S + "/espec.json", S + "/" + out, "300"], check=True, capture_output=True)
    return sf.read(S + "/" + out)[0]
def metrics(y):
    ez = zc(y[: int(0.2 * SR)])
    emid = (ez[:-1] + ez[1:]) / 2; ef = 1 / np.diff(ez)
    semis = 12 * np.log2(np.interp(rmid, emid, ef) / rf)
    body = rz > 0.045
    cyc = np.interp(rz[body], ez, np.arange(len(ez)))          # engine cycle index at ref crossings
    ph = (cyc - np.round(cyc)) * 360
    return semis, ph
def cost(q):
    kn = quant(q); s, p = metrics(render(kn))
    off = ((kn["phase_offset"] + 180) % 360) - 180
    return np.mean(s[2:] ** 2) + 0.02 * np.mean(p ** 2) + 0.002 * off ** 2   # skip first 2 periods (<0.3 ms)
fit = json.load(open(S + "/fit2_w30.json"))["pitch"]
x0 = np.array(fit[:5] + [(fit[5] * 360) % 360])
if x0[5] > 180: x0[5] -= 360
best = None
for dsw2 in [-8, 0, 8]:
    for dmid in [-25, 0, 25]:
        x = x0 + np.array([0, dmid, 0, dsw2, 0, 0])
        r = minimize(cost, x, method="Nelder-Mead", options=dict(maxiter=600, xatol=0.05, fatol=1e-4,
                     initial_simplex=[x] + [x + d for d in np.diag([300, 20, 1.0, 6, 0.3, 8])]))
        if best is None or r.fun < best.fun: best = r
kn = quant(best.x); s, p = metrics(render(kn, "eng_best.wav"))
print("BEST", kn, "cost %.3f" % best.fun)
print("semitone err per ref period:", np.round(s, 1))
print("body/tail phase err (deg):", np.round(p).astype(int))
spec["params"].update(kn); json.dump(spec, open(S + "/final_spec.json", "w"), indent=1)
