import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import json

try:
    import numpy as np
    import sounddevice as sd
    from scipy.io import wavfile
except ImportError as e:
    import sys
    root = tk.Tk()
    root.withdraw()
    messagebox.showerror("Missing Dependencies", f"Failed to start: {e}\n\nPlease install the required packages by running this in your terminal:\npip install numpy sounddevice scipy")
    sys.exit(1)

# Notes mapping for the fundamental frequency
NOTES = {
    "C1": 32.70, "C#1": 34.65, "D1": 36.71, "D#1": 38.89,
    "E1": 41.20, "F1": 43.65, "F#1": 46.25, "G1": 49.00,
    "G#1": 51.91, "A1": 55.00, "A#1": 58.27, "B1": 61.74,
    "C2": 65.41
}

# Built-in Example Presets
BUILTIN_PRESETS = {
    "Psytrance (Default)": {
        "start_freq": 8000.0, "mid_freq": 150.0, "end_freq": 49.0, 
        "sweep_time_1": 10.0, "sweep_time_2": 80.0, "pitch_curve": 3.0,
        "vol_attack": 2.0, "vol_hold": 10.0, "vol_decay_1": 50.0, 
        "vol_sustain": 40.0, "vol_decay_2": 150.0, "vol_curve": 3.0, 
        "scoop_start": 10.0, "scoop_length": 30.0, "scoop_depth": 0.0,
        "drive": 1.5, "click_vol": 0.0, "tail_drive": 0.0, "invert_phase": False
    },
    "Techno Deep": {
        "start_freq": 2000.0, "mid_freq": 200.0, "end_freq": 43.0, 
        "sweep_time_1": 20.0, "sweep_time_2": 150.0, "pitch_curve": 2.0,
        "vol_attack": 5.0, "vol_hold": 30.0, "vol_decay_1": 100.0, 
        "vol_sustain": 60.0, "vol_decay_2": 300.0, "vol_curve": 1.5, 
        "scoop_start": 20.0, "scoop_length": 50.0, "scoop_depth": 0.0,
        "drive": 3.0, "click_vol": 0.05, "tail_drive": 2.0, "invert_phase": False
    },
    "Hardstyle Zap": {
        "start_freq": 15000.0, "mid_freq": 500.0, "end_freq": 55.0, 
        "sweep_time_1": 5.0, "sweep_time_2": 100.0, "pitch_curve": 5.0,
        "vol_attack": 0.0, "vol_hold": 20.0, "vol_decay_1": 80.0, 
        "vol_sustain": 20.0, "vol_decay_2": 100.0, "vol_curve": 5.0, 
        "scoop_start": 5.0, "scoop_length": 20.0, "scoop_depth": 0.0,
        "drive": 8.0, "click_vol": 0.1, "tail_drive": 5.0, "invert_phase": False
    },
    "Projektor Punch": {
        "start_freq": 12000.0, "mid_freq": 350.0, "end_freq": 49.0, 
        "sweep_time_1": 8.0, "sweep_time_2": 80.0, "pitch_curve": 4.0,
        "vol_attack": 0.5, "vol_hold": 15.0, "vol_decay_1": 60.0, 
        "vol_sustain": 30.0, "vol_decay_2": 250.0, "vol_curve": 3.5, 
        "scoop_start": 15.0, "scoop_length": 40.0, "scoop_depth": 10.0,
        "drive": 2.5, "click_vol": 0.15, "tail_drive": 1.0, "invert_phase": False
    },
    "Projektor Deep Sub": {
        "start_freq": 8000.0, "mid_freq": 200.0, "end_freq": 43.65, 
        "sweep_time_1": 15.0, "sweep_time_2": 150.0, "pitch_curve": 2.5,
        "vol_attack": 2.0, "vol_hold": 30.0, "vol_decay_1": 100.0, 
        "vol_sustain": 70.0, "vol_decay_2": 400.0, "vol_curve": 2.0, 
        "scoop_start": 20.0, "scoop_length": 60.0, "scoop_depth": 5.0,
        "drive": 1.2, "click_vol": 0.05, "tail_drive": 3.0, "invert_phase": True
    },
    "Projektor Hard F#": {
        "start_freq": 14000.0, "mid_freq": 450.0, "end_freq": 46.25, 
        "sweep_time_1": 5.0, "sweep_time_2": 110.0, "pitch_curve": 4.5,
        "vol_attack": 0.0, "vol_hold": 10.0, "vol_decay_1": 70.0, 
        "vol_sustain": 40.0, "vol_decay_2": 200.0, "vol_curve": 4.0, 
        "scoop_start": 10.0, "scoop_length": 35.0, "scoop_depth": 15.0,
        "drive": 4.0, "click_vol": 0.2, "tail_drive": 4.0, "invert_phase": False
    }
}

# Modern Dark Theme Colors
BG_COLOR = "#1a1a1a"
FG_COLOR = "#e0e0e0"
ACCENT_COLOR = "#ff0055"
CANVAS_BG = "#0f0f0f"

class KickSynthApp:
    def __init__(self, root):
        self.root = root
        self.root.title("PyKick - Bazzism Clone")
        self.root.geometry("800x1050")  # Made taller to fit new UI elements
        self.root.configure(bg=BG_COLOR, padx=20, pady=20)
        
        self.sample_rate = 44100
        self.auto_playing = False
        self.auto_task = None
        
        # --- UI Styling ---
        style = ttk.Style()
        style.theme_use('default')
        style.configure('TFrame', background=BG_COLOR)
        style.configure('TLabel', background=BG_COLOR, foreground=FG_COLOR, font=("Segoe UI", 10))
        style.configure('Title.TLabel', background=BG_COLOR, foreground=ACCENT_COLOR, font=("Segoe UI", 20, "bold"))
        style.configure('Subtitle.TLabel', background=BG_COLOR, foreground="#888888", font=("Segoe UI", 10, "italic"))
        style.configure('Header.TLabel', background=BG_COLOR, foreground=ACCENT_COLOR, font=("Segoe UI", 12, "bold"))
        style.configure('TButton', background="#333333", foreground=FG_COLOR, font=("Segoe UI", 10, "bold"), padding=6)
        style.map('TButton', background=[('active', ACCENT_COLOR)], foreground=[('active', '#ffffff')])
        style.configure('TScale', background=BG_COLOR, troughcolor="#333333", sliderthickness=15)
        style.configure('TCheckbutton', background=BG_COLOR, foreground=FG_COLOR, font=("Segoe UI", 10))
        style.map('TCheckbutton', background=[('active', BG_COLOR)])
        
        # --- Header ---
        ttk.Label(root, text="SYNTHESIZE YOUR KICK", style='Title.TLabel').pack(pady=(0, 0))
        ttk.Label(root, text="by Gleinkaa", style='Subtitle.TLabel').pack(pady=(0, 10))
        
        # --- Presets Frame ---
        preset_frame = ttk.Frame(root)
        preset_frame.pack(fill="x", pady=5)
        
        ttk.Label(preset_frame, text="Example Presets:").pack(side="left")
        self.example_cb = ttk.Combobox(preset_frame, values=["---"] + list(BUILTIN_PRESETS.keys()), state="readonly", width=15)
        self.example_cb.current(0)
        self.example_cb.pack(side="left", padx=5)
        self.example_cb.bind("<<ComboboxSelected>>", self.on_example_selected)
        
        ttk.Button(preset_frame, text="Load .json", command=self.load_preset).pack(side="right", padx=5)
        ttk.Button(preset_frame, text="Save .json", command=self.save_preset).pack(side="right")

        # --- Transport / Global ---
        transport_frame = ttk.Frame(root)
        transport_frame.pack(fill="x", pady=10)
        
        self.bpm_var = tk.IntVar(value=140)
        ttk.Label(transport_frame, text="BPM:").pack(side="left")
        bpm_entry = tk.Entry(transport_frame, textvariable=self.bpm_var, width=5, bg="#333333", fg=FG_COLOR, insertbackground=FG_COLOR, font=("Segoe UI", 10))
        bpm_entry.pack(side="left", padx=5)
        
        self.auto_var = tk.BooleanVar(value=False)
        auto_check = ttk.Checkbutton(transport_frame, text="Auto Play (4/4)", variable=self.auto_var, command=self.toggle_auto)
        auto_check.pack(side="right")
        
        ttk.Label(transport_frame, text="Snap End Freq to Note:").pack(side="right", padx=(20, 5))
        self.note_cb = ttk.Combobox(transport_frame, values=["Off"] + list(NOTES.keys()), state="readonly", width=6)
        self.note_cb.current(0)
        self.note_cb.pack(side="right")
        self.note_cb.bind("<<ComboboxSelected>>", self.on_note_selected)
        
        # --- Parameter Columns ---
        param_container = ttk.Frame(root)
        param_container.pack(fill="both", expand=True, pady=10)
        
        pitch_frame = ttk.Frame(param_container)
        pitch_frame.pack(side="left", fill="both", expand=True, padx=(0, 15))
        
        vol_frame = ttk.Frame(param_container)
        vol_frame.pack(side="right", fill="both", expand=True, padx=(15, 0))
        
        # Initialize default values using Psytrance preset
        defaults = BUILTIN_PRESETS["Psytrance (Default)"]

        # --- Pitch Envelope ---
        ttk.Label(pitch_frame, text="PITCH ENVELOPE", style='Header.TLabel').pack(anchor="w", pady=(0, 10))
        self.start_freq = self.create_slider(pitch_frame, "Start Freq (Hz)", 100, 15000, defaults["start_freq"])
        self.mid_freq = self.create_slider(pitch_frame, "Mid Freq (Hz)", 50, 1000, defaults["mid_freq"])
        self.end_freq = self.create_slider(pitch_frame, "End Freq (Hz)", 20, 100, defaults["end_freq"])
        self.sweep_time_1 = self.create_slider(pitch_frame, "Sweep Time 1 (ms)", 0.1, 50.0, defaults["sweep_time_1"], resolution=0.1)
        self.sweep_time_2 = self.create_slider(pitch_frame, "Sweep Time 2 (ms)", 1.0, 250.0, defaults["sweep_time_2"], resolution=0.5)
        self.pitch_curve = self.create_slider(pitch_frame, "Pitch Curve", 0.1, 10.0, defaults["pitch_curve"], resolution=0.1)
        
        # --- Volume Envelope ---
        ttk.Label(vol_frame, text="VOLUME ENVELOPE", style='Header.TLabel').pack(anchor="w", pady=(0, 10))
        self.vol_attack = self.create_slider(vol_frame, "Attack (ms)", 0.0, 30.0, defaults["vol_attack"], resolution=0.1)
        self.vol_hold = self.create_slider(vol_frame, "Hold (ms)", 0.0, 50.0, defaults["vol_hold"], resolution=0.1)
        self.vol_decay_1 = self.create_slider(vol_frame, "Decay 1 (ms)", 0.0, 150.0, defaults["vol_decay_1"], resolution=0.5)
        self.vol_sustain = self.create_slider(vol_frame, "Sustain (%)", 0.0, 100.0, defaults["vol_sustain"], resolution=1.0)
        self.vol_decay_2 = self.create_slider(vol_frame, "Decay 2 (ms)", 0.0, 700.0, defaults["vol_decay_2"], resolution=1.0)
        self.vol_curve = self.create_slider(vol_frame, "Volume Curve", 0.1, 10.0, defaults["vol_curve"], resolution=0.1)

        # --- Volume Scoop ---
        ttk.Label(vol_frame, text="VOLUME SCOOP", style='Header.TLabel').pack(anchor="w", pady=(15, 10))
        self.scoop_start = self.create_slider(vol_frame, "Scoop Start (ms)", 0.0, 50.0, defaults["scoop_start"], resolution=0.1)
        self.scoop_length = self.create_slider(vol_frame, "Scoop Length (ms)", 1.0, 100.0, defaults["scoop_length"], resolution=0.5)
        self.scoop_depth = self.create_slider(vol_frame, "Scoop Depth (%)", 0.0, 100.0, defaults["scoop_depth"], resolution=1.0)

        # --- FX & Extras ---
        fx_frame = ttk.Frame(pitch_frame)
        fx_frame.pack(fill="x", pady=(20, 0))
        ttk.Label(fx_frame, text="EFFECTS & EXTRAS", style='Header.TLabel').pack(anchor="w", pady=(0, 5))
        
        self.click_vol = self.create_slider(fx_frame, "Click Vol (Transient)", 0.0, 1.0, defaults["click_vol"], resolution=0.01)
        self.drive = self.create_slider(fx_frame, "Base Drive", 1.0, 10.0, defaults["drive"], resolution=0.1)
        self.tail_drive = self.create_slider(fx_frame, "Tail Saturation", 0.0, 10.0, defaults["tail_drive"], resolution=0.1)

        invert_frame = ttk.Frame(fx_frame)
        invert_frame.pack(fill="x", pady=4)
        self.invert_phase = tk.BooleanVar(value=defaults["invert_phase"])
        invert_check = ttk.Checkbutton(invert_frame, text="Invert Phase (Polarity)", variable=self.invert_phase, command=self.update_visualizer)
        invert_check.pack(side="left")

        # Group all variables for easy saving/loading
        self.params = {
            "start_freq": self.start_freq, "mid_freq": self.mid_freq, "end_freq": self.end_freq,
            "sweep_time_1": self.sweep_time_1, "sweep_time_2": self.sweep_time_2, "pitch_curve": self.pitch_curve,
            "vol_attack": self.vol_attack, "vol_hold": self.vol_hold, "vol_decay_1": self.vol_decay_1,
            "vol_sustain": self.vol_sustain, "vol_decay_2": self.vol_decay_2, "vol_curve": self.vol_curve,
            "scoop_start": self.scoop_start, "scoop_length": self.scoop_length, "scoop_depth": self.scoop_depth,
            "drive": self.drive, "click_vol": self.click_vol, "tail_drive": self.tail_drive, "invert_phase": self.invert_phase
        }

        # --- Visualizer ---
        self.canvas = tk.Canvas(root, height=180, bg=CANVAS_BG, highlightthickness=1, highlightbackground="#333333")
        self.canvas.pack(fill="x", pady=20)
        
        # --- Buttons ---
        btn_frame = ttk.Frame(root)
        btn_frame.pack(fill="x")

        self.play_btn = ttk.Button(btn_frame, text="▶ PLAY KICK", command=self.play_kick)
        self.play_btn.pack(side="left", expand=True, fill="x", padx=(0, 5))

        self.save_btn = ttk.Button(btn_frame, text="💾 EXPORT WAV", command=self.save_wav)
        self.save_btn.pack(side="right", expand=True, fill="x", padx=(5, 0))
        
        # Initial draw
        self.root.after(100, self.update_visualizer)
        
    def on_note_selected(self, event):
        note = self.note_cb.get()
        if note in NOTES:
            self.end_freq.set(NOTES[note])
            self.update_visualizer()

    def on_example_selected(self, event):
        preset_name = self.example_cb.get()
        if preset_name in BUILTIN_PRESETS:
            self.apply_preset_dict(BUILTIN_PRESETS[preset_name])

    def save_preset(self):
        state = {k: v.get() for k, v in self.params.items()}
        filepath = filedialog.asksaveasfilename(defaultextension=".json", filetypes=[("JSON files", "*.json")], title="Save Preset")
        if filepath:
            try:
                with open(filepath, 'w') as f:
                    json.dump(state, f, indent=4)
                messagebox.showinfo("Success", "Preset saved successfully!")
            except Exception as e:
                messagebox.showerror("Error", f"Failed to save preset:\n{e}")

    def load_preset(self):
        filepath = filedialog.askopenfilename(filetypes=[("JSON files", "*.json")], title="Load Preset")
        if filepath:
            try:
                with open(filepath, 'r') as f:
                    state = json.load(f)
                self.apply_preset_dict(state)
                self.example_cb.current(0)
            except Exception as e:
                messagebox.showerror("Error", f"Failed to load preset:\n{e}")

    def apply_preset_dict(self, state_dict):
        for k, v in state_dict.items():
            if k in self.params:
                self.params[k].set(v)
        self.update_visualizer()

    def create_slider(self, parent, label_text, min_val, max_val, default_val, resolution=1):
        frame = ttk.Frame(parent)
        frame.pack(fill="x", pady=4)
        
        ttk.Label(frame, text=label_text).pack(side="left")
        
        val_var = tk.DoubleVar(value=default_val)
        value_label = ttk.Label(frame, text=str(default_val), width=6, anchor="e")
        value_label.pack(side="right")
        
        def update_label(val):
            fmt = "{:.2f}" if resolution < 1 else "{:.0f}"
            value_label.config(text=fmt.format(float(val)))
            self.update_visualizer()

        slider = ttk.Scale(frame, from_=min_val, to=max_val, orient="horizontal", variable=val_var, command=update_label)
        slider.pack(side="right", fill="x", expand=True, padx=10)
        
        slider.bind("<ButtonRelease-1>", lambda e: self.update_visualizer())
        
        return val_var

    def generate_audio_data(self):
        """Core DSP logic. Returns (audio_array, amp_env_array)"""
        
        # 1. Total Duration from Volume Envelope
        a_ms = self.vol_attack.get()
        h_ms = self.vol_hold.get()
        d1_ms = self.vol_decay_1.get()
        sus_pct = self.vol_sustain.get() / 100.0
        d2_ms = self.vol_decay_2.get()
        v_curve = self.vol_curve.get()
        
        total_ms = a_ms + h_ms + d1_ms + d2_ms
        if total_ms <= 0: total_ms = 1
        
        duration = total_ms / 1000.0 
        t = np.linspace(0, duration, int(self.sample_rate * duration), endpoint=False)

        # 2. Pitch Envelope
        f_start = self.start_freq.get()
        f_mid = self.mid_freq.get()
        f_end = self.end_freq.get()
        t1 = self.sweep_time_1.get() / 1000.0
        t2 = self.sweep_time_2.get() / 1000.0
        p_curve = self.pitch_curve.get()
        
        freqs = np.zeros_like(t)
        
        mask_p1 = t < t1
        if np.any(mask_p1):
            x1 = np.clip(t[mask_p1] / t1, 0.0, 1.0) if t1 > 0 else 1.0
            freqs[mask_p1] = f_mid + (f_start - f_mid) * ((1.0 - x1) ** p_curve)
            
        mask_p2 = (t >= t1) & (t < t1 + t2)
        if np.any(mask_p2):
            x2 = np.clip((t[mask_p2] - t1) / t2, 0.0, 1.0) if t2 > 0 else 1.0
            freqs[mask_p2] = f_end + (f_mid - f_end) * ((1.0 - x2) ** p_curve)
            
        mask_p3 = t >= t1 + t2
        if np.any(mask_p3):
            freqs[mask_p3] = f_end
            
        # 3. Generate Oscillator
        phase = np.cumsum(freqs) / self.sample_rate
        audio = np.sin(2 * np.pi * phase)
        
        # 4. Volume Envelope (AHDSR Piecewise)
        amp_env = np.zeros_like(t)
        t_a = a_ms / 1000.0
        t_h = h_ms / 1000.0
        t_d1 = d1_ms / 1000.0
        t_d2 = d2_ms / 1000.0
        
        p1_end = t_a
        p2_end = p1_end + t_h
        p3_end = p2_end + t_d1
        
        # Attack
        mask_v1 = t < p1_end
        if np.any(mask_v1):
            x = np.clip(t[mask_v1] / t_a, 0.0, 1.0) if t_a > 0 else 1.0
            amp_env[mask_v1] = x ** (1.0 / v_curve)
            
        # Hold
        mask_v2 = (t >= p1_end) & (t < p2_end)
        if np.any(mask_v2):
            amp_env[mask_v2] = 1.0
            
        # Decay 1
        mask_v3 = (t >= p2_end) & (t < p3_end)
        if np.any(mask_v3):
            x = np.clip((t[mask_v3] - p2_end) / t_d1, 0.0, 1.0) if t_d1 > 0 else 1.0
            amp_env[mask_v3] = sus_pct + (1.0 - sus_pct) * ((1.0 - x) ** v_curve)
            
        # Decay 2
        mask_v4 = t >= p3_end
        if np.any(mask_v4):
            x = np.clip((t[mask_v4] - p3_end) / t_d2, 0.0, 1.0) if t_d2 > 0 else 1.0
            amp_env[mask_v4] = sus_pct * ((1.0 - x) ** v_curve)

        # 5. Volume Scoop
        s_start = self.scoop_start.get() / 1000.0
        s_length = self.scoop_length.get() / 1000.0
        s_depth = self.scoop_depth.get() / 100.0
        
        if s_depth > 0 and s_length > 0:
            mask_s = (t >= s_start) & (t < s_start + s_length)
            if np.any(mask_s):
                scoop_phase = (t[mask_s] - s_start) / s_length # 0 to 1
                dip_shape = 1.0 - (np.sin(scoop_phase * np.pi) * s_depth)
                amp_env[mask_s] *= dip_shape

        audio = audio * amp_env

        # 6. Click / Transient (5ms hi-freq sine sweep)
        c_vol = self.click_vol.get()
        if c_vol > 0.0:
            click_len = int(0.005 * self.sample_rate) # 5ms
            if click_len > len(t):
                click_len = len(t)
            
            c_freqs = np.linspace(10000, 2000, click_len)
            c_phase = np.cumsum(c_freqs) / self.sample_rate
            click_snd = np.sin(2 * np.pi * c_phase)
            click_snd *= np.linspace(1, 0, click_len) ** 2 # Fast fade out
            
            click_track = np.zeros_like(t)
            click_track[:click_len] = click_snd * c_vol
            audio += click_track
        
        # 7. Drive / Saturation (Base + Tail-weighted)
        base_drive = self.drive.get()
        t_drive = self.tail_drive.get()
        
        if t_drive > 0.0:
            drive_ramp = np.linspace(0, 1, len(audio)) ** 2
            drive_array = base_drive + (drive_ramp * t_drive * 5.0)
            audio = np.tanh(audio * drive_array) / np.tanh(np.max(drive_array))
        else:
            audio = np.tanh(audio * base_drive) / np.tanh(base_drive)

        # 8. Polarity Invert
        if self.invert_phase.get():
            audio = -audio

        # De-click
        fade_len = min(100, len(audio))
        if fade_len > 0:
            audio[-fade_len:] *= np.linspace(1, 0, fade_len) 

        return np.float32(audio), amp_env

    def update_visualizer(self, *args):
        """Draws the audio waveform onto the Canvas."""
        self.canvas.delete("waveform")
        try:
            audio, amp_env = self.generate_audio_data()
        except Exception as e:
            print("Visualizer error:", e)
            return
            
        width = self.canvas.winfo_width()
        height = self.canvas.winfo_height()
        
        if width <= 1 or height <= 1:
            return 
            
        samples_per_pixel = max(1, len(audio) // width)
        env_points = []
        
        for i in range(width):
            start = i * samples_per_pixel
            if start >= len(audio):
                break
            
            chunk = audio[start:start+samples_per_pixel]
            env_chunk = amp_env[start:start+samples_per_pixel]
            
            if len(chunk) > 0:
                # Draw the solid audio waveform
                max_val = np.max(chunk)
                min_val = np.min(chunk)
                y1 = height / 2 - (max_val * (height / 2.2))
                y2 = height / 2 - (min_val * (height / 2.2))
                self.canvas.create_line(i, y1, i, y2, fill=ACCENT_COLOR, tags="waveform")
                
                # Plot the yellow envelope line on top (just upper half)
                env_val = np.mean(env_chunk)
                env_y = height / 2 - (env_val * (height / 2.2))
                env_points.append((i, env_y))
                
        # Draw the yellow envelope line
        if len(env_points) > 1:
            flat_env = [coords for pt in env_points for coords in pt]
            self.canvas.create_line(flat_env, fill="#ffcc00", width=2, tags="waveform")

    def toggle_auto(self):
        if self.auto_var.get():
            self.auto_playing = True
            self.play_loop()
        else:
            self.auto_playing = False
            if self.auto_task:
                self.root.after_cancel(self.auto_task)
                self.auto_task = None

    def play_loop(self):
        if not self.auto_playing:
            return
            
        try:
            bpm = self.bpm_var.get()
            if bpm <= 0: bpm = 140
        except:
            bpm = 140
            
        ms_per_beat = int(60000 / bpm)
        
        self.auto_task = self.root.after(ms_per_beat, self.play_loop)
        self.play_kick()

    def play_kick(self):
        try:
            audio, _ = self.generate_audio_data()
            sd.play(audio, self.sample_rate)
        except Exception as e:
            print("Playback error:", e)

    def save_wav(self):
        audio, _ = self.generate_audio_data()
        filepath = filedialog.asksaveasfilename(
            defaultextension=".wav", 
            filetypes=[("WAV files", "*.wav")],
            title="Save Kick Drum"
        )
        if filepath:
            audio_int16 = np.int16(audio * 32767)
            wavfile.write(filepath, self.sample_rate, audio_int16)
            messagebox.showinfo("Success", f"Kick saved to:\n{filepath}")

if __name__ == "__main__":
    root = tk.Tk()
    app = KickSynthApp(root)
    root.update()
    app.update_visualizer()
    root.mainloop()