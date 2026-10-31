import sys, json, numpy as np, soundfile as sf
from scipy.optimize import least_squares
SR = 48000; END = 41.2034; L = 60/152/2
x, _ = sf.read(sys.argv[1]); x = x if x.ndim == 1 else x.mean(1)
x = x[np.argmax(np.abs(x) > 1e-3):]
y = -x   # ref is polarity-inverted vs KickAss; work on the flipped ref (KickAss invert_phase=1 flips back)
k = np.where((y[:-1] < 0) & (y[1:] >= 0))[0]; zc = (k + (-y[k]) / (y[k+1] - y[k])) / SR
zc = zc[zc < 0.195]
tm = (zc[:-1] + zc[1:]) / 2; fr = 1 / np.diff(zc)          # measured inst. freq (period midpoints)
t = np.arange(int(0.2 * SR)) / SR
def pitch(p):
    s, m, t1, t2, c = p; f = np.full_like(t, END)
    a = t < t1; f[a] = m + (s - m) * (1 - t[a] / t1) ** c
    b = (t >= t1) & (t < t1 + t2); f[b] = END + (m - END) * (1 - (t[b] - t1) / t2) ** c
    return f
def res(q, verbose=False):
    p = [q[0], q[1], q[2]/1000, q[3]/1000, q[4]]
    f = pitch(p); ph = np.cumsum(f) / SR + q[5]
    # model's average freq over each measured period = cycles between crossings / period
    favg = (np.interp(zc[1:], t, ph) - np.interp(zc[:-1], t, ph)) / np.diff(zc)
    r_f = np.log2(favg / fr) * 12 / 2                       # semitones/2
    body = zc > 0.045
    r_ph = (np.interp(zc[body], t, ph) - np.round(np.interp(zc[body], t, ph))) * 100   # cycles, strong
    if verbose: return r_f * 2, r_ph / 100
    return np.concatenate([r_f, r_ph, [q[5] * 4]])
best = None
for s0 in [3000, 6000, 10000]:
    for m0 in [150, 300, 500]:
        for c0 in [2, 4, 6]:
            r = least_squares(res, [s0, m0, 8, 100, c0, 0.0], bounds=([100, 50, 0.1, 1, 0.1, -0.25], [15000, 1000, 50, 250, 10, 0.25]))
            if best is None or r.cost < best.cost: best = r
q = best.x; ef, ep = res(q, True)
p = [q[0], q[1], q[2]/1000, q[3]/1000, q[4]]; ph = np.cumsum(pitch(p)) / SR + q[5]
print("PITCH start=%.0f mid=%.1f sw1=%.2fms sw2=%.1fms curve=%.2f offset=%.1fdeg" % (*q[:5], q[5]*360))
print("pitch error (semitones) per period:", np.round(ef, 2))
print("body phase error (deg):", np.round(ep * 360).astype(int))
print("cycle index of body crossings:", np.round(np.interp(zc[zc > 0.045], t, ph), 2))
print("phase at L: %.1f deg" % ((np.interp(L, t, ph) % 1) * 360))
json.dump({"pitch": list(q)}, open(sys.argv[2], "w"))
