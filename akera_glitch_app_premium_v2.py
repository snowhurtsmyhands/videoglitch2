#!/usr/bin/env python3
"""
AKERA SKY — Glitch Studio Premium
GPU-first preview pipeline with CPU fallbacks.

Required installs:
  pip install customtkinter opencv-python numpy Pillow tkinterdnd2
"""

import json
import os
import queue
import subprocess
import threading
import time
from collections import OrderedDict, deque
from pathlib import Path

import customtkinter as ctk
import cv2
import numpy as np
import tkinter as tk
from PIL import Image, ImageTk
from tkinter import colorchooser, filedialog, messagebox

try:
    import tkinterdnd2 as tkdnd_module
    from tkinterdnd2 import DND_FILES
    HAS_DND = True
except ImportError:
    tkdnd_module = None
    DND_FILES = "DND_Files"
    HAS_DND = False

ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("dark-blue")

ACCENT = "#c8ff00"
BG = "#0a0a0a"
BG2 = "#0f0f0f"
BG3 = "#161616"
MUTED = "#444444"
TEXT = "#e0e0d8"
PRESET_VERSION = 2
DEFAULT_TIMECODE_COLOR = "#ffb347"
DEFAULT_TIMECODE_TEMPLATE = "2021-08-03  {time}"
DEFAULT_TIMECODE_SIZE = 36

PREVIEW_QUALITY = {
    "Draft": {"scale": 0.22, "heavy": False, "fps_cap": 24, "max_preview_width": 520},
    "Balanced": {"scale": 0.34, "heavy": False, "fps_cap": 30, "max_preview_width": 760},
    "Ultra": {"scale": 0.58, "heavy": True, "fps_cap": 60, "max_preview_width": 1180},
}

BUILTIN_PRESETS = {
    "RAW": dict(grain=0, static_n=0, scan_int=0, vig=0.0, chroma=0, track=0, bleed=0, glitch=0,
                head_glitch=0, sat=100, cont=100, hue=0, sine_warp=0, pixel_sort=0, interlace=0,
                light_leak=0, scanlines=False, timestamp=False, crt=False, cold_tone=False, rgb_ghost=False,
                timecode_y=92, timecode_x=2, timecode_size=36, timecode_color=DEFAULT_TIMECODE_COLOR, timecode_template=DEFAULT_TIMECODE_TEMPLATE),
    "VHS": dict(grain=35, static_n=15, scan_int=20, vig=0.4, chroma=4, track=40, bleed=10, glitch=10,
                head_glitch=0, sat=70, cont=115, hue=0, sine_warp=15, pixel_sort=0, interlace=30,
                light_leak=10, scanlines=True, timestamp=True, crt=False, cold_tone=False, rgb_ghost=False,
                timecode_y=92, timecode_x=2, timecode_size=36, timecode_color=DEFAULT_TIMECODE_COLOR, timecode_template=DEFAULT_TIMECODE_TEMPLATE),
    "CRT": dict(grain=20, static_n=5, scan_int=60, vig=0.6, chroma=6, track=0, bleed=0, glitch=5,
                head_glitch=0, sat=110, cont=120, hue=0, sine_warp=0, pixel_sort=0, interlace=50,
                light_leak=5, scanlines=True, timestamp=False, crt=True, cold_tone=False, rgb_ghost=False,
                timecode_y=92, timecode_x=2, timecode_size=36, timecode_color=DEFAULT_TIMECODE_COLOR, timecode_template=DEFAULT_TIMECODE_TEMPLATE),
    "SIGNAL": dict(grain=60, static_n=40, scan_int=40, vig=0.5, chroma=12, track=60, bleed=15, glitch=40,
                head_glitch=30, sat=50, cont=130, hue=10, sine_warp=30, pixel_sort=20, interlace=40,
                light_leak=20, scanlines=True, timestamp=False, crt=False, cold_tone=True, rgb_ghost=True,
                timecode_y=92, timecode_x=2, timecode_size=36, timecode_color=DEFAULT_TIMECODE_COLOR, timecode_template=DEFAULT_TIMECODE_TEMPLATE),
    "AKERA": dict(grain=45, static_n=20, scan_int=35, vig=0.55, chroma=8, track=50, bleed=12, glitch=25,
                head_glitch=60, sat=40, cont=135, hue=-10, sine_warp=20, pixel_sort=30, interlace=35,
                light_leak=18, scanlines=True, timestamp=False, crt=False, cold_tone=True, rgb_ghost=True,
                timecode_y=92, timecode_x=2, timecode_size=36, timecode_color=DEFAULT_TIMECODE_COLOR, timecode_template=DEFAULT_TIMECODE_TEMPLATE),
}


def check_cuda():
    try:
        return cv2.cuda.getCudaEnabledDeviceCount() > 0
    except Exception:
        return False


HAS_CUDA = check_cuda()


def ffmpeg_encoder_available(name: str) -> bool:
    try:
        result = subprocess.run(['ffmpeg', '-hide_banner', '-encoders'], capture_output=True, text=True, timeout=5)
        return name in result.stdout
    except Exception:
        return False


def source_has_audio(path: str) -> bool:
    try:
        probe = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'a', '-show_entries', 'stream=codec_type', '-of', 'csv=p=0', path], capture_output=True, text=True, timeout=5)
        return 'audio' in probe.stdout
    except Exception:
        return False


def preferred_export_encoder() -> tuple[str, list[str], str]:
    if ffmpeg_encoder_available('h264_nvenc'):
        return 'h264_nvenc', ['-c:v', 'h264_nvenc', '-preset', 'p5', '-cq', '23', '-b:v', '0', '-pix_fmt', 'yuv420p'], 'NVENC'
    return 'libx264', ['-c:v', 'libx264', '-preset', 'medium', '-crf', '20', '-pix_fmt', 'yuv420p'], 'x264'


def app_data_dir() -> Path:
    base = Path(os.environ.get("APPDATA", str(Path.home())))
    p = base / "AkeraSkyGlitchStudio"
    p.mkdir(parents=True, exist_ok=True)
    return p


USER_PRESET_DIR = app_data_dir() / "presets"
USER_PRESET_DIR.mkdir(parents=True, exist_ok=True)


def enable_tkdnd(root):
    if not HAS_DND or tkdnd_module is None:
        return False
    tkapp = root.tk
    try:
        tkapp.call('package', 'require', 'tkdnd')
        return True
    except Exception:
        pass
    module_dir = Path(tkdnd_module.__file__).resolve().parent
    search_dirs = [module_dir, module_dir / 'tkdnd', module_dir / 'tkdnd2.9', module_dir / 'win-x64', module_dir / 'win-arm64', module_dir / 'win32']
    for d in search_dirs:
        if not d.exists():
            continue
        try:
            tkapp.call('lappend', 'auto_path', str(d))
            tkapp.call('package', 'require', 'tkdnd')
            return True
        except Exception:
            continue
    return False


# ---------- Timecode ----------
def format_timecode_text(template: str, frame_idx: int, fps: float):
    fps_safe = max(fps, 1.0)
    sec_total = frame_idx / fps_safe
    whole = int(sec_total)
    minutes, seconds = divmod(whole % 3600, 60)
    hours = (whole // 3600) % 24
    values = {
        "frame": frame_idx,
        "fps": f"{fps_safe:.2f}",
        "time": f"{hours:02d}:{minutes:02d}:{seconds:02d}",
        "hours": f"{hours:02d}",
        "minutes": f"{minutes:02d}",
        "seconds": f"{seconds:02d}",
    }
    try:
        return template.format(**values)
    except Exception:
        return template


def render_timecode(frame, cfg, frame_idx, fps):
    if not cfg.get("timestamp"):
        return frame
    h, w = frame.shape[:2]
    text = format_timecode_text(cfg.get("timecode_template", DEFAULT_TIMECODE_TEMPLATE), frame_idx, fps)
    y = int(np.clip(cfg.get("timecode_y", 92) * h / 100.0, 12, h - 8))
    x = int(np.clip(cfg.get("timecode_x", 2) * w / 100.0, 2, w - 20))
    size = max(0.25, cfg.get("timecode_size", DEFAULT_TIMECODE_SIZE) / 100.0)
    color_hex = cfg.get("timecode_color", DEFAULT_TIMECODE_COLOR)
    color_hex = color_hex.lstrip("#")
    if len(color_hex) == 6:
        rgb = tuple(int(color_hex[i:i + 2], 16) for i in (0, 2, 4))
        bgr = (rgb[2], rgb[1], rgb[0])
    else:
        bgr = (230, 210, 0)
    cv2.putText(frame, text, (x + 1, y + 1), cv2.FONT_HERSHEY_SIMPLEX, size, (10, 10, 10), 2, cv2.LINE_AA)
    cv2.putText(frame, text, (x, y), cv2.FONT_HERSHEY_SIMPLEX, size, bgr, 1, cv2.LINE_AA)
    return frame


# ---------- CPU effects ----------
def fx_cold_tone(frame):
    f = frame.astype(np.float32)
    f[:, :, 0] *= 1.10
    f[:, :, 1] *= 0.96
    f[:, :, 2] *= 0.85
    return np.clip(f, 0, 255).astype(np.uint8)


def fx_sat_cont(frame, sat, cont):
    f = np.clip((frame.astype(np.float32) - 128) * (cont / 100.0) + 128, 0, 255).astype(np.uint8)
    if sat != 100:
        hsv = cv2.cvtColor(f, cv2.COLOR_BGR2HSV).astype(np.float32)
        hsv[:, :, 1] = np.clip(hsv[:, :, 1] * (sat / 100.0), 0, 255)
        f = cv2.cvtColor(hsv.astype(np.uint8), cv2.COLOR_HSV2BGR)
    return f


def fx_hue_shift(frame, hue_deg):
    if hue_deg == 0:
        return frame
    hsv = cv2.cvtColor(frame, cv2.COLOR_BGR2HSV).astype(np.int16)
    hsv[:, :, 0] = (hsv[:, :, 0] + int(hue_deg / 2)) % 180
    return cv2.cvtColor(hsv.astype(np.uint8), cv2.COLOR_HSV2BGR)


def fx_chroma(frame, v):
    if v == 0:
        return frame
    h, w = frame.shape[:2]
    b, g, r = cv2.split(frame)
    r = cv2.warpAffine(r, np.float32([[1, 0, v], [0, 1, 0]]), (w, h))
    b = cv2.warpAffine(b, np.float32([[1, 0, -v], [0, 1, 0]]), (w, h))
    return cv2.merge([b, g, r])


def fx_bleed(frame, v):
    if v == 0:
        return frame
    b, g, r = cv2.split(frame.astype(np.float32))
    ks = int(max(1, v // 5)) * 2 + 1
    r = cv2.addWeighted(r, 1 - v / 100, cv2.GaussianBlur(r, (ks, ks), 0), v / 100, 0)
    return cv2.merge([b, g, r]).astype(np.uint8)


def fx_vignette(frame, v):
    if v == 0:
        return frame
    h, w = frame.shape[:2]
    y, x = np.ogrid[:h, :w]
    d = np.sqrt((x - w / 2) ** 2 + (y - h / 2) ** 2)
    vig = np.clip(1.0 - v * (d / np.sqrt((w / 2) ** 2 + (h / 2) ** 2)), 0, 1)
    return np.clip(frame.astype(np.float32) * np.stack([vig] * 3, -1), 0, 255).astype(np.uint8)


def fx_scanlines(frame, intensity):
    if intensity <= 0:
        return frame
    out = frame.astype(np.float32)
    out[::2] *= (1.0 - intensity / 100.0)
    return np.clip(out, 0, 255).astype(np.uint8)


def fx_grain(frame, v, tile=None):
    if v == 0:
        return frame
    if tile is None:
        noise = np.random.normal(0, v * 1.2, frame.shape).astype(np.int16)
    else:
        th, tw = tile.shape[:2]
        h, w = frame.shape[:2]
        reps_y = int(np.ceil(h / max(th, 1)))
        reps_x = int(np.ceil(w / max(tw, 1)))
        noise = np.tile(tile, (reps_y, reps_x, 1))[:h, :w].astype(np.int16)
        noise = (noise.astype(np.float32) * (v / 100.0)).astype(np.int16)
    return np.clip(frame.astype(np.int16) + noise, 0, 255).astype(np.uint8)


def fx_tracking(frame, v, fidx):
    if v == 0:
        return frame
    h = frame.shape[0]
    rng = np.random.RandomState(fidx * 7 + 3)
    out = frame.copy()
    for _ in range(max(1, int(v / 15))):
        if rng.random() < v / 100:
            y = rng.randint(0, max(1, h - 10))
            shift = int((rng.random() - 0.5) * v * 2)
            line_h = rng.randint(1, 6)
            out[y:y + line_h] = np.roll(frame[y:y + line_h], shift, axis=1)
    return out


def fx_glitch(frame, v, fidx):
    if v == 0:
        return frame
    rng = np.random.RandomState(fidx * 13)
    if rng.random() > v / 100:
        return frame
    h, w = frame.shape[:2]
    out = frame.copy()
    for _ in range(max(1, int(v / 15))):
        bh = rng.randint(2, min(30, h) + 1)
        y = rng.randint(0, max(1, h - bh + 1))
        shift = int((rng.random() - 0.5) * w * 0.3)
        out[y:y + bh] = np.roll(frame[y:y + bh], shift, axis=1)
    return out


def fx_head_glitch(frame, v, fidx):
    if v == 0:
        return frame
    rng = np.random.RandomState(fidx * 31 + 7)
    if rng.random() > v / 80:
        return frame
    h, w = frame.shape[:2]
    out = frame.copy()
    reach = int((v / 100) * h * 0.45)
    zone_h = max(2, min(h, reach))
    for _ in range(max(1, int(v / 14))):
        y = rng.randint(0, zone_h)
        x = rng.randint(0, max(1, w - 2))
        bh = rng.randint(2, min(10, h - y) + 1)
        bw = rng.randint(4, min(20, w - x) + 1)
        if rng.random() < 0.4:
            out[y:y + bh, x:x + bw] = rng.randint(0, 255, (bh, bw, 3), dtype=np.uint8)
        else:
            out[y:y + bh, x:x + bw] = 0
    return out


def fx_interlace(frame, v, fidx):
    if v == 0:
        return frame
    strength = v / 100.0
    out = frame.astype(np.float32)
    ghost = np.roll(frame, int(2 + strength * 4), axis=1).astype(np.float32)
    blend = strength * 0.35 * (0.5 + 0.5 * np.sin(fidx * 0.3))
    out[1::2] = np.clip(out[1::2] * (1 - blend) + ghost[1::2] * blend, 0, 255)
    return out.astype(np.uint8)


def fx_pixel_sort(frame, v, fidx):
    if v == 0:
        return frame
    rng = np.random.RandomState(fidx * 41 + 11)
    if rng.random() > v / 80:
        return frame
    h, w = frame.shape[:2]
    out = frame.copy()
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    for _ in range(max(1, int(v / 15))):
        x = rng.randint(0, max(1, w - 2))
        x2 = min(w, x + rng.randint(2, max(3, int(w * 0.03))))
        y1 = rng.randint(0, max(1, h // 2))
        y2 = rng.randint(max(y1 + 1, h // 2), h)
        idx = np.argsort(gray[y1:y2, x:x2].mean(axis=1))
        out[y1:y2, x:x2] = out[y1:y2, x:x2][idx]
    return out


def fx_sine_warp(frame, v, fidx, lite=False):
    if v == 0:
        return frame
    h = frame.shape[0]
    out = np.zeros_like(frame)
    amp = (v * 0.22) if lite else (v * 0.3)
    phase = fidx * (0.055 if lite else 0.08)
    stride = 2 if lite else 1
    for y in range(0, h, stride):
        shift = int(amp * np.sin(3.0 * y / max(h, 1) * 2 * np.pi + phase))
        out[y] = np.roll(frame[y], shift, axis=0)
        if stride > 1 and y + 1 < h:
            out[y + 1] = out[y]
    return out


def fx_rgb_ghost(frame, fidx):
    h, w = frame.shape[:2]
    b, g, r = cv2.split(frame.astype(np.float32))
    t = fidx * 0.04
    dr = int(np.sin(t * 1.1) * 4)
    db = int(np.cos(t * 0.9) * 4)
    r2 = cv2.warpAffine(r, np.float32([[1, 0, dr], [0, 1, 0]]), (w, h))
    b2 = cv2.warpAffine(b, np.float32([[1, 0, -db], [0, 1, 0]]), (w, h))
    return cv2.merge([np.clip(b2, 0, 255), g, np.clip(r2, 0, 255)]).astype(np.uint8)


def fx_crt(frame):
    h, w = frame.shape[:2]
    out = frame.copy()
    for i in range(int(h * 0.07)):
        a = i / max(int(h * 0.07), 1)
        out[i] = (out[i].astype(np.float32) * a).astype(np.uint8)
        out[h - 1 - i] = (out[h - 1 - i].astype(np.float32) * a).astype(np.uint8)
    for i in range(int(w * 0.05)):
        a = i / max(int(w * 0.05), 1)
        out[:, i] = (out[:, i].astype(np.float32) * a).astype(np.uint8)
        out[:, w - 1 - i] = (out[:, w - 1 - i].astype(np.float32) * a).astype(np.uint8)
    return out


def process_frame_full(frame, cfg, fidx, fps=24):
    frame = fx_sat_cont(frame, cfg['sat'], cfg['cont'])
    if cfg.get('cold_tone'):
        frame = fx_cold_tone(frame)
    frame = fx_hue_shift(frame, cfg.get('hue', 0))
    frame = fx_chroma(frame, cfg['chroma'])
    if cfg.get('rgb_ghost'):
        frame = fx_rgb_ghost(frame, fidx)
    frame = fx_bleed(frame, cfg['bleed'])
    frame = fx_sine_warp(frame, cfg.get('sine_warp', 0), fidx, lite=False)
    frame = fx_tracking(frame, cfg['track'], fidx)
    frame = fx_head_glitch(frame, cfg['head_glitch'], fidx)
    frame = fx_interlace(frame, cfg.get('interlace', 0), fidx)
    frame = fx_pixel_sort(frame, cfg.get('pixel_sort', 0), fidx)
    frame = fx_glitch(frame, cfg['glitch'], fidx)
    frame = fx_grain(frame, cfg['grain'])
    if cfg.get('scanlines'):
        frame = fx_scanlines(frame, cfg['scan_int'])
    frame = fx_vignette(frame, cfg['vig'])
    if cfg.get('crt'):
        frame = fx_crt(frame)
    return render_timecode(frame, cfg, fidx, fps)


# ---------- GPU Preview ----------
class GPUPreviewPipeline:
    """GPU-first preview chain: one upload, one download, CPU fallback only where needed."""

    def __init__(self):
        self.enabled = HAS_CUDA
        self.cuda_effects = ["resize", "sat/cont", "hue", "cold_tone", "chroma", "bleed", "scanlines", "vignette"]
        self.cpu_only_effects = ["pixel_sort", "sine_warp", "tracking", "head_glitch", "glitch", "crt", "timecode", "grain"]
        self._gpu_ops_ok = self._probe_ops() if self.enabled else {}
        self.last_fallbacks = []

    def _probe_ops(self):
        probe = {}
        try:
            gm = cv2.cuda_GpuMat(4, 4, cv2.CV_8UC3)
            gm.upload(np.zeros((4, 4, 3), dtype=np.uint8))
            probe["resize"] = hasattr(cv2.cuda, "resize")
            probe["warpAffine"] = hasattr(cv2.cuda, "warpAffine")
            probe["cvtColor"] = hasattr(cv2.cuda, "cvtColor")
            probe["split"] = hasattr(cv2.cuda, "split")
            probe["merge"] = hasattr(cv2.cuda, "merge")
        except Exception:
            return {}
        return probe

    def _get_noise_tile(self):
        rng = np.random.RandomState(7)
        return rng.normal(0, 20, (96, 96, 3)).astype(np.int16)

    def capability_report(self):
        if not self.enabled:
            return {
                "gpu_preview": False,
                "gpu_effects": 0,
                "total_preview_effects": 10,
                "decode": "CPU",
                "note": "OpenCV CUDA module unavailable",
            }
        active = 1 if self._gpu_ops_ok.get("resize") else 0
        return {
            "gpu_preview": active > 0,
            "gpu_effects": active,
            "total_preview_effects": 10,
            "decode": "CPU",
            "note": "GPU resize path active" if active else "CUDA detected but resize op unavailable",
        }

    def run(self, frame, cfg, fidx, fps, quality, playback):
        profile = PREVIEW_QUALITY.get(quality, PREVIEW_QUALITY["Balanced"])
        h, w = frame.shape[:2]
        tw = max(240, min(int(w * profile["scale"]), profile["max_preview_width"]))
        th = max(135, int(h * tw / max(w, 1)))
        self.last_fallbacks = []

        if not self.enabled:
            return self._run_cpu_preview(frame, cfg, fidx, fps, quality, playback, (tw, th))

        try:
            gpu = cv2.cuda_GpuMat()
            gpu.upload(frame)
            gpu = cv2.cuda.resize(gpu, (tw, th), interpolation=cv2.INTER_AREA)

            out = gpu.download()
        except Exception as e:
            self.last_fallbacks.append(f"gpu_core:{e}")
            return self._run_cpu_preview(frame, cfg, fidx, fps, quality, playback, (tw, th))

        # CPU-only / heavy effects after one download.
        live_light = playback and quality != "Ultra"
        out = fx_sat_cont(out, cfg['sat'], cfg['cont'])
        if cfg.get('cold_tone'):
            out = fx_cold_tone(out)
        out = fx_hue_shift(out, cfg.get('hue', 0))
        out = fx_chroma(out, int(cfg['chroma'] * (0.7 if playback else 1.0)))
        out = fx_bleed(out, cfg['bleed'])
        if cfg.get('scanlines'):
            out = fx_scanlines(out, cfg['scan_int'])
        out = fx_vignette(out, cfg['vig'])
        if cfg.get('rgb_ghost'):
            out = fx_rgb_ghost(out, fidx)
        if cfg.get('sine_warp', 0) > 0:
            out = fx_sine_warp(out, cfg['sine_warp'] * (0.3 if live_light else 1.0), fidx, lite=live_light)
        if cfg.get('track', 0) > 0:
            out = fx_tracking(out, cfg['track'] * (0.55 if live_light else 0.9), fidx)
        if cfg.get('head_glitch', 0) > 0:
            out = fx_head_glitch(out, cfg['head_glitch'] * (0.45 if live_light else 0.85), fidx)
        if cfg.get('interlace', 0) > 0:
            out = fx_interlace(out, cfg['interlace'] * (0.8 if live_light else 1.0), fidx)
        if cfg.get('pixel_sort', 0) > 0 and (quality == "Ultra" or not playback):
            out = fx_pixel_sort(out, cfg['pixel_sort'] * (0.65 if quality != "Ultra" else 1.0), fidx)
        if cfg.get('glitch', 0) > 0:
            out = fx_glitch(out, cfg['glitch'] * (0.65 if live_light else 1.0), fidx)
        if cfg.get('grain', 0) > 0:
            out = fx_grain(out, cfg['grain'] * (0.35 if live_light else 0.55), tile=self._get_noise_tile() if cfg.get('_preview_use_tiled_grain') else None)
        if cfg.get('crt') and (quality == "Ultra" or not playback):
            out = fx_crt(out)
        out = render_timecode(out, cfg, fidx, fps)
        return out

    def _run_cpu_preview(self, frame, cfg, fidx, fps, quality, playback, size):
        small = cv2.resize(frame, size, interpolation=cv2.INTER_AREA)
        live_light = playback and quality != "Ultra"
        small = fx_sat_cont(small, cfg['sat'], cfg['cont'])
        if cfg.get('cold_tone'):
            small = fx_cold_tone(small)
        small = fx_hue_shift(small, cfg.get('hue', 0))
        small = fx_chroma(small, int(cfg['chroma'] * (0.6 if live_light else 0.85)))
        if cfg.get('rgb_ghost'):
            small = fx_rgb_ghost(small, fidx)
        small = fx_bleed(small, cfg['bleed'])
        if cfg.get('sine_warp', 0):
            small = fx_sine_warp(small, cfg['sine_warp'] * (0.28 if live_light else 0.7), fidx, lite=live_light)
        if cfg.get('track', 0):
            small = fx_tracking(small, cfg['track'] * (0.55 if live_light else 0.8), fidx)
        if cfg.get('head_glitch', 0):
            small = fx_head_glitch(small, cfg['head_glitch'] * (0.45 if live_light else 0.8), fidx)
        if cfg.get('interlace', 0):
            small = fx_interlace(small, cfg['interlace'], fidx)
        if cfg.get('pixel_sort', 0) and (quality == "Ultra" or not playback):
            small = fx_pixel_sort(small, cfg['pixel_sort'] * (0.35 if quality != "Ultra" else 0.65), fidx)
        if cfg.get('glitch', 0):
            small = fx_glitch(small, cfg['glitch'] * (0.68 if live_light else 1.0), fidx)
        if cfg.get('grain', 0):
            small = fx_grain(small, cfg['grain'] * (0.30 if live_light else 0.45), tile=self._get_noise_tile() if cfg.get('_preview_use_tiled_grain') else None)
        if cfg.get('scanlines'):
            small = fx_scanlines(small, cfg['scan_int'])
        small = fx_vignette(small, cfg['vig'])
        if cfg.get('crt') and (quality == "Ultra" or not playback):
            small = fx_crt(small)
        return render_timecode(small, cfg, fidx, fps)

# ---------- Presets ----------
class PresetManager:
    def __init__(self):
        self.user_dir = USER_PRESET_DIR
        self.user_dir.mkdir(parents=True, exist_ok=True)

    def list_user_presets(self):
        return sorted(p.stem for p in self.user_dir.glob("*.json"))

    def save_user_preset(self, name: str, cfg: dict):
        safe = "".join(c for c in name.strip() if c.isalnum() or c in "-_ ").strip()
        if not safe:
            raise ValueError("Preset name is empty")
        payload = {
            "version": PRESET_VERSION,
            "name": safe,
            "created_at": int(time.time()),
            "settings": cfg,
        }
        path = self.user_dir / f"{safe}.json"
        path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
        return path

    def load_user_preset(self, name: str):
        path = self.user_dir / f"{name}.json"
        data = json.loads(path.read_text(encoding="utf-8"))
        settings = data.get("settings", {})
        version = data.get("version", 0)
        if version > PRESET_VERSION:
            raise ValueError("Preset version is newer than this app")
        return settings

    def delete_user_preset(self, name: str):
        p = self.user_dir / f"{name}.json"
        if p.exists():
            p.unlink()


# ---------- GUI ----------
class GlitchApp(ctk.CTk):
    def __init__(self):
        super().__init__()
        self.title("AKERA SKY — Glitch Studio Premium")
        self.geometry("1240x790")
        self.minsize(980, 620)
        self.configure(fg_color=BG)

        self.pipeline = GPUPreviewPipeline()
        self.preset_manager = PresetManager()
        self.dnd_ready = enable_tkdnd(self)

        self.video_path = None
        self.cap = None
        self.total_frames = 0
        self.fps = 24
        self.current_frame_idx = 0
        self._capture_frame_pos = -1
        self._frame_cache = OrderedDict()
        self._cache_limit = 40
        self.decode_mode = "CPU"

        self.preview_img = None
        self._preview_data = None
        self.playing = False
        self.preview_quality = tk.StringVar(value="Balanced")
        self._display_fps = 0.0
        self._perf_samples = deque(maxlen=40)
        self._canvas_image_id = None
        self._noise_tile = np.random.normal(0, 18, (120, 120, 3)).astype(np.int16)
        self.export_encoder_name = preferred_export_encoder()[2]

        self.playback_thread = None
        self.preview_thread = None
        self.preview_queue = queue.Queue(maxsize=2)
        self.result_queue = queue.Queue(maxsize=2)
        self.shutdown_flag = threading.Event()
        self.frame_drop_count = 0
        self.playback_skip_count = 0

        self.sliders = {}
        self.toggle_vars = {}
        self.timecode_dragging = False
        self.timecode_text_var = tk.StringVar(value=DEFAULT_TIMECODE_TEMPLATE)
        self.timecode_color_var = tk.StringVar(value=DEFAULT_TIMECODE_COLOR)
        self.user_preset_name_var = tk.StringVar(value="MyPreset")
        self.user_preset_var = tk.StringVar(value="")

        self._build_ui()
        self._apply_builtin_preset("AKERA")
        self._refresh_user_presets()
        self._start_workers()
        self.after(33, self._poll_preview_results)

        caps = self.pipeline.capability_report()
        dnd_state = "ready" if self.dnd_ready else "fallback" if HAS_DND else "missing"
        self.status_label.configure(
            text=f"GPU Preview: {'ON' if caps['gpu_preview'] else 'OFF'}  •  GPU Effects: {caps['gpu_effects']}/{caps['total_preview_effects']}  •  Decode: {caps['decode']}  •  Export: {self.export_encoder_name}  •  {caps.get('note','')}  •  DnD: {dnd_state}"
        )
        self._update_perf_label()

    def _start_workers(self):
        self.preview_thread = threading.Thread(target=self._preview_worker_loop, daemon=True)
        self.preview_thread.start()

    def _build_ui(self):
        self.grid_columnconfigure(0, weight=3)
        self.grid_columnconfigure(1, weight=2)
        self.grid_rowconfigure(0, weight=1)

        left = ctk.CTkFrame(self, fg_color=BG, corner_radius=0)
        left.grid(row=0, column=0, sticky="nsew", padx=(16, 8), pady=16)
        left.grid_rowconfigure(1, weight=1)
        left.grid_columnconfigure(0, weight=1)

        ctk.CTkLabel(left, text="// AKERA SKY  GLITCH STUDIO  PREMIUM", font=ctk.CTkFont("Courier New", 11), text_color=MUTED).grid(row=0, column=0, sticky="w", pady=(0, 8))

        self.drop_frame = ctk.CTkFrame(left, fg_color=BG2, corner_radius=10, border_width=1, border_color="#242424")
        self.drop_frame.grid(row=1, column=0, sticky="nsew")
        self.drop_frame.grid_rowconfigure(0, weight=1)
        self.drop_frame.grid_columnconfigure(0, weight=1)

        self.canvas = tk.Canvas(self.drop_frame, bg="#0a0a0a", highlightthickness=0)
        self.canvas.grid(row=0, column=0, sticky="nsew")
        self.canvas.bind("<Configure>", lambda _e: self._redraw_preview())
        self.canvas.bind("<Button-1>", self._on_canvas_click)
        self.canvas.bind("<B1-Motion>", self._on_canvas_drag)
        self.canvas.bind("<ButtonRelease-1>", self._on_canvas_release)

        self.drop_label = ctk.CTkLabel(self.drop_frame, text="DROP VIDEO HERE\n\nor click to browse", font=ctk.CTkFont("Courier New", 13), text_color=MUTED)
        self.drop_label.place(relx=0.5, rely=0.5, anchor="center")
        self.drop_label.bind("<Button-1>", lambda _e: self._browse())
        self.drop_frame.bind("<Button-1>", lambda _e: self._browse())

        if self.dnd_ready:
            self.drop_frame.drop_target_register(DND_FILES)
            self.drop_frame.dnd_bind('<<Drop>>', self._on_drop)
            self.canvas.drop_target_register(DND_FILES)
            self.canvas.dnd_bind('<<Drop>>', self._on_drop)

        bar = ctk.CTkFrame(left, fg_color=BG, corner_radius=0)
        bar.grid(row=2, column=0, sticky="ew", pady=(8, 0))
        bar.grid_columnconfigure(1, weight=1)

        self.play_btn = ctk.CTkButton(bar, text="▶  PLAY", width=90, height=32, font=ctk.CTkFont("Courier New", 11), fg_color=BG3, border_width=1, border_color=MUTED, text_color=TEXT, hover_color="#1e1e1e", command=self._toggle_play)
        self.play_btn.grid(row=0, column=0, padx=(0, 8))

        self.seek_var = tk.DoubleVar(value=0)
        self.seekbar = ctk.CTkSlider(bar, from_=0, to=100, variable=self.seek_var, height=18, button_color=ACCENT, button_hover_color=ACCENT, progress_color="#2a2a2a", fg_color="#1a1a1a", command=self._on_seek)
        self.seekbar.grid(row=0, column=1, sticky="ew", padx=(0, 8))

        self.frame_label = ctk.CTkLabel(bar, text="0 / 0", font=ctk.CTkFont("Courier New", 10), text_color=MUTED, width=110)
        self.frame_label.grid(row=0, column=2)

        perf_panel = ctk.CTkFrame(left, fg_color="transparent")
        perf_panel.grid(row=3, column=0, sticky="ew", pady=(6, 0))
        perf_panel.grid_columnconfigure(0, weight=1)
        self.status_label = ctk.CTkLabel(perf_panel, text="No file loaded", font=ctk.CTkFont("Courier New", 10), text_color=MUTED)
        self.status_label.grid(row=0, column=0, sticky="w")
        self.perf_label = ctk.CTkLabel(perf_panel, text="Preview -- fps", font=ctk.CTkFont("Courier New", 10), text_color=ACCENT)
        self.perf_label.grid(row=0, column=1, sticky="e")

        right = ctk.CTkScrollableFrame(self, fg_color=BG2, corner_radius=12, border_width=1, border_color="#232323", scrollbar_button_color=BG3)
        right.grid(row=0, column=1, sticky="nsew", padx=(0, 16), pady=16)
        right.grid_columnconfigure(0, weight=1)

        row = self._section(right, "BUILT-IN PRESETS", 0)
        pgrid = ctk.CTkFrame(right, fg_color="transparent")
        pgrid.grid(row=row, column=0, sticky="ew", pady=(0, 10))
        self.preset_btns = {}
        for i, name in enumerate(BUILTIN_PRESETS.keys()):
            b = ctk.CTkButton(pgrid, text=name, width=70, height=26, font=ctk.CTkFont("Courier New", 10), fg_color=BG3, border_width=1, border_color=MUTED, text_color=MUTED, hover_color="#1e1e1e", command=lambda n=name: self._apply_builtin_preset(n))
            b.grid(row=i // 3, column=i % 3, padx=3, pady=3)
            self.preset_btns[name] = b
        row += 1

        row = self._section(right, "USER PRESETS", row)
        up = ctk.CTkFrame(right, fg_color="transparent")
        up.grid(row=row, column=0, sticky="ew", pady=(0, 10))
        up.grid_columnconfigure(0, weight=1)
        self.user_preset_menu = ctk.CTkOptionMenu(up, variable=self.user_preset_var, values=["(none)"], fg_color=BG3, button_color=BG3, button_hover_color="#1e1e1e", command=lambda _x: None)
        self.user_preset_menu.grid(row=0, column=0, sticky="ew", padx=(0, 6))
        ctk.CTkEntry(up, textvariable=self.user_preset_name_var, placeholder_text="Preset name").grid(row=1, column=0, sticky="ew", pady=(6, 0), padx=(0, 6))
        btns = ctk.CTkFrame(up, fg_color="transparent")
        btns.grid(row=2, column=0, sticky="ew", pady=(6, 0))
        ctk.CTkButton(btns, text="Save Current as Preset", command=self._save_user_preset, fg_color=BG3).pack(side="left", padx=2)
        ctk.CTkButton(btns, text="Load Preset", command=self._load_user_preset, fg_color=BG3).pack(side="left", padx=2)
        ctk.CTkButton(btns, text="Delete Preset", command=self._delete_user_preset, fg_color="#3a1b1b", hover_color="#572424").pack(side="left", padx=2)
        row += 1

        row = self._section(right, "PREVIEW MODE", row)
        self.preview_quality_seg = ctk.CTkSegmentedButton(right, values=list(PREVIEW_QUALITY.keys()), variable=self.preview_quality, height=28, fg_color=BG3, selected_color=ACCENT, selected_hover_color="#d8ff5a", unselected_color=BG3, unselected_hover_color="#1e1e1e", text_color=TEXT, command=lambda _v: self._request_preview())
        self.preview_quality_seg.grid(row=row, column=0, sticky="ew", pady=(0, 10))
        row += 1

        row = self._tog(right, row, "Cold Tone", "cold_tone")
        row = self._tog(right, row, "RGB Ghost Drift", "rgb_ghost")
        row = self._tog(right, row, "Scanlines", "scanlines")
        row = self._tog(right, row, "CRT Curve", "crt")
        row = self._tog(right, row, "Enable Timecode", "timestamp")

        row = self._section(right, "TIMECODE", row)
        ctk.CTkEntry(right, textvariable=self.timecode_text_var, placeholder_text="AKERA SKY // {time}").grid(row=row, column=0, sticky="ew", pady=(0, 6))
        row += 1
        ctk.CTkButton(right, text="Pick Timecode Color", command=self._pick_timecode_color, fg_color=BG3).grid(row=row, column=0, sticky="ew", pady=(0, 8))
        row += 1
        row = self._sl(right, row, "Timecode X", "timecode_x", 0, 100, default=2)
        row = self._sl(right, row, "Timecode Y", "timecode_y", 0, 100, default=92)
        row = self._sl(right, row, "Timecode Size", "timecode_size", 25, 180, default=55)

        row = self._section(right, "SIGNAL CORRUPTION", row)
        row = self._sl(right, row, "Head Glitch", "head_glitch", 0, 100)
        row = self._sl(right, row, "Interlace Flicker", "interlace", 0, 100)
        row = self._sl(right, row, "Pixel Sort", "pixel_sort", 0, 100)
        row = self._sl(right, row, "Glitch Blocks", "glitch", 0, 100)
        row = self._sl(right, row, "Tracking Error", "track", 0, 100)
        row = self._sl(right, row, "Static Noise", "static_n", 0, 100)

        row = self._section(right, "ANALOG", row)
        row = self._sl(right, row, "Film Grain", "grain", 0, 100)
        row = self._sl(right, row, "Sine Warp", "sine_warp", 0, 100)
        row = self._sl(right, row, "Light Leak", "light_leak", 0, 100)
        row = self._sl(right, row, "Color Bleed", "bleed", 0, 30)
        row = self._sl(right, row, "Chroma Shift", "chroma", 0, 30)
        row = self._sl(right, row, "Scanline Depth", "scan_int", 0, 100)
        row = self._sl(right, row, "Vignette", "vig", 0, 100, scale=0.01)

        row = self._section(right, "COLOR", row)
        row = self._sl(right, row, "Hue Shift", "hue", -180, 180, default=0)
        row = self._sl(right, row, "Saturation", "sat", 0, 200, default=100)
        row = self._sl(right, row, "Contrast", "cont", 50, 200, default=100)

        ctk.CTkFrame(right, fg_color=MUTED, height=1).grid(row=row, column=0, sticky="ew", pady=12)
        row += 1

        self.export_btn = ctk.CTkButton(right, text="EXPORT VIDEO", height=40, font=ctk.CTkFont("Courier New", 12, weight="bold"), fg_color="transparent", border_width=1, border_color=ACCENT, text_color=ACCENT, hover_color="#1a2200", command=self._export)
        self.export_btn.grid(row=row, column=0, sticky="ew", pady=(0, 6))
        row += 1
        self.prog_bar = ctk.CTkProgressBar(right, height=6, progress_color=ACCENT, fg_color=BG3)
        self.prog_bar.grid(row=row, column=0, sticky="ew")
        self.prog_bar.set(0)
        row += 1
        self.export_label = ctk.CTkLabel(right, text="", font=ctk.CTkFont("Courier New", 10), text_color=MUTED)
        self.export_label.grid(row=row, column=0, pady=(4, 0))

    def _section(self, p, label, row):
        f = ctk.CTkFrame(p, fg_color="transparent")
        f.grid(row=row, column=0, sticky="ew", pady=(12, 3))
        ctk.CTkLabel(f, text=label, font=ctk.CTkFont("Courier New", 9), text_color=MUTED).pack(side="left")
        ctk.CTkFrame(f, fg_color=MUTED, height=1).pack(side="left", fill="x", expand=True, padx=(8, 0))
        return row + 1

    def _sl(self, p, row, label, key, mn, mx, default=None, scale=1.0):
        if default is None:
            default = mn
        f = ctk.CTkFrame(p, fg_color="transparent")
        f.grid(row=row, column=0, sticky="ew", pady=2)
        f.grid_columnconfigure(1, weight=1)
        ctk.CTkLabel(f, text=label, font=ctk.CTkFont("Courier New", 10), text_color="#888", width=128, anchor="w").grid(row=0, column=0)
        var = tk.DoubleVar(value=default)
        val_lbl = ctk.CTkLabel(f, text=str(default), font=ctk.CTkFont("Courier New", 10), text_color=TEXT, width=44)
        val_lbl.grid(row=0, column=2, padx=(4, 0))

        def on_change(v):
            num = float(v)
            val_lbl.configure(text=str(int(num)) if scale == 1.0 else str(round(num * scale, 2)))
            self._request_preview()

        ctk.CTkSlider(f, from_=mn, to=mx, variable=var, height=14, button_color=ACCENT, button_hover_color=ACCENT, progress_color="#2a2a2a", fg_color="#151515", command=on_change).grid(row=0, column=1, sticky="ew", padx=6)
        self.sliders[key] = (var, scale)
        return row + 1

    def _tog(self, p, row, label, key):
        f = ctk.CTkFrame(p, fg_color="transparent")
        f.grid(row=row, column=0, sticky="ew", pady=3)
        f.grid_columnconfigure(0, weight=1)
        ctk.CTkLabel(f, text=label, font=ctk.CTkFont("Courier New", 10), text_color="#888").grid(row=0, column=0, sticky="w")
        var = tk.BooleanVar(value=False)
        ctk.CTkSwitch(f, text="", variable=var, width=40, onvalue=True, offvalue=False, progress_color=ACCENT, button_color="#e0e0e0", command=self._request_preview).grid(row=0, column=1)
        self.toggle_vars[key] = var
        return row + 1

    def _get_cfg(self):
        cfg = {}
        for k, (var, scale) in self.sliders.items():
            v = var.get()
            cfg[k] = (v * scale) if scale != 1.0 else v
        for k, var in self.toggle_vars.items():
            cfg[k] = var.get()
        cfg["timecode_template"] = self.timecode_text_var.get().strip() or "{time}"
        cfg["timecode_color"] = self.timecode_color_var.get()
        return cfg

    def _apply_cfg(self, cfg):
        for k, (var, scale) in self.sliders.items():
            if k in cfg:
                var.set(cfg[k] / scale if scale != 1.0 else cfg[k])
        for k, var in self.toggle_vars.items():
            if k in cfg:
                var.set(cfg[k])
        if "timecode_template" in cfg:
            self.timecode_text_var.set(cfg["timecode_template"])
        if "timecode_color" in cfg:
            self.timecode_color_var.set(cfg["timecode_color"])
        self._request_preview()

    def _apply_builtin_preset(self, name):
        self._apply_cfg(BUILTIN_PRESETS[name])
        for n, b in self.preset_btns.items():
            on = (n == name)
            b.configure(fg_color=ACCENT if on else BG3, text_color=BG if on else MUTED, border_color=ACCENT if on else MUTED)

    def _refresh_user_presets(self):
        names = self.preset_manager.list_user_presets()
        vals = names if names else ["(none)"]
        self.user_preset_menu.configure(values=vals)
        if vals:
            self.user_preset_var.set(vals[0])

    def _save_user_preset(self):
        name = self.user_preset_name_var.get().strip()
        try:
            self.preset_manager.save_user_preset(name, self._get_cfg())
        except Exception as e:
            messagebox.showerror("Preset Save", str(e))
            return
        self._refresh_user_presets()
        self.status_label.configure(text=f"Saved user preset: {name}")

    def _load_user_preset(self):
        name = self.user_preset_var.get().strip()
        if not name or name == "(none)":
            return
        try:
            cfg = self.preset_manager.load_user_preset(name)
            self._apply_cfg(cfg)
            self.status_label.configure(text=f"Loaded user preset: {name}")
        except Exception as e:
            messagebox.showerror("Preset Load", str(e))

    def _delete_user_preset(self):
        name = self.user_preset_var.get().strip()
        if not name or name == "(none)":
            return
        self.preset_manager.delete_user_preset(name)
        self._refresh_user_presets()
        self.status_label.configure(text=f"Deleted user preset: {name}")

    def _pick_timecode_color(self):
        color = colorchooser.askcolor(color=self.timecode_color_var.get(), title="Timecode color")
        if color and color[1]:
            self.timecode_color_var.set(color[1])
            self._request_preview()

    def _browse(self):
        path = filedialog.askopenfilename(filetypes=[("Video", "*.mp4 *.mov *.avi *.mkv *.m4v *.webm"), ("All", "*.*")])
        if path:
            self._load_video(path)

    def _parse_drop_paths(self, data):
        try:
            items = self.tk.splitlist(data)
        except Exception:
            items = [data]
        out = []
        for raw in items:
            p = raw.strip().strip("{}\"")
            if p:
                out.append(p)
        return out

    def _on_drop(self, event):
        for p in self._parse_drop_paths(event.data):
            if os.path.isfile(p):
                self._load_video(p)
                return

    def _load_video(self, path):
        if self.cap:
            self.cap.release()
        self.cap = cv2.VideoCapture(path)
        if not self.cap.isOpened():
            messagebox.showerror("Error", f"Cannot open:\n{path}")
            return
        self.video_path = path
        self.fps = self.cap.get(cv2.CAP_PROP_FPS) or 24
        self.total_frames = int(self.cap.get(cv2.CAP_PROP_FRAME_COUNT))
        self.current_frame_idx = 0
        self._capture_frame_pos = -1
        self._frame_cache.clear()
        self.drop_label.place_forget()
        w = int(self.cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        h = int(self.cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        self.seekbar.configure(to=max(1, self.total_frames - 1))
        caps = self.pipeline.capability_report()
        self.status_label.configure(text=f"{Path(path).name}  —  {w}x{h} {self.fps:.0f}fps {self.total_frames}f  • GPU Preview: {'ON' if caps['gpu_preview'] else 'OFF'}  • GPU Effects: {caps['gpu_effects']}/{caps['total_preview_effects']}  • Decode: {self.decode_mode}  • Export: {self.export_encoder_name}  • {caps.get('note','')}")
        self._request_preview(force=True)

    def _on_seek(self, val):
        self.current_frame_idx = int(float(val))
        self._request_preview(force=True)

    def _read_frame(self, idx):
        if idx in self._frame_cache:
            self._frame_cache.move_to_end(idx)
            return self._frame_cache[idx].copy()
        if not self.cap:
            return None
        if self._capture_frame_pos != idx:
            self.cap.set(cv2.CAP_PROP_POS_FRAMES, idx)
        ok, frame = self.cap.read()
        if not ok:
            return None
        self._capture_frame_pos = idx + 1
        self._frame_cache[idx] = frame.copy()
        self._frame_cache.move_to_end(idx)
        while len(self._frame_cache) > self._cache_limit:
            self._frame_cache.popitem(last=False)
        return frame

    def _request_preview(self, force=False):
        if not self.cap or not self.video_path:
            return
        frame = self._read_frame(self.current_frame_idx)
        if frame is None:
            return
        cfg = self._build_runtime_preview_cfg()
        payload = {
            "idx": self.current_frame_idx,
            "frame": frame,
            "cfg": cfg,
            "fps": self.fps,
            "quality": self.preview_quality.get(),
            "playback": self.playing,
            "force": force,
        }
        if self.preview_queue.full():
            self.frame_drop_count += 1
            try:
                self.preview_queue.get_nowait()
            except queue.Empty:
                pass
        try:
            self.preview_queue.put_nowait(payload)
        except queue.Full:
            pass

    def _build_runtime_preview_cfg(self):
        cfg = self._get_cfg()
        if not self.playing:
            return cfg
        quality = self.preview_quality.get()
        cfg['pixel_sort'] = 0 if quality != "Ultra" else cfg.get('pixel_sort', 0)
        cfg['head_glitch'] = min(cfg.get('head_glitch', 0), 45 if quality == "Balanced" else 30)
        cfg['track'] = min(cfg.get('track', 0), 40)
        cfg['sine_warp'] = cfg.get('sine_warp', 0) * (0.35 if quality == "Balanced" else 0.2)
        cfg['glitch'] = cfg.get('glitch', 0) * (0.7 if quality != "Ultra" else 1.0)
        cfg['grain'] = min(cfg.get('grain', 0), 40)
        cfg['interlace'] = min(cfg.get('interlace', 0), 55)
        if quality == "Draft":
            cfg['crt'] = False
            cfg['scanlines'] = cfg.get('scanlines', False) and cfg.get('scan_int', 0) <= 45
        cfg['_preview_fast_resample'] = self.playing or quality != 'Ultra'
        cfg['_preview_use_tiled_grain'] = self.playing
        return cfg

    def _preview_worker_loop(self):
        while not self.shutdown_flag.is_set():
            try:
                item = self.preview_queue.get(timeout=0.1)
            except queue.Empty:
                continue
            t0 = time.time()
            try:
                out = self.pipeline.run(item["frame"], item["cfg"], item["idx"], item["fps"], item["quality"], item["playback"])
                elapsed = max(1e-6, time.time() - t0)
                r = {
                    "idx": item["idx"],
                    "frame": out,
                    "fps": 1.0 / elapsed,
                    "fallbacks": list(self.pipeline.last_fallbacks),
                }
                if self.result_queue.full():
                    self.result_queue.get_nowait()
                self.result_queue.put_nowait(r)
            except Exception as e:
                self.result_queue.put({"error": str(e)})

    def _poll_preview_results(self):
        try:
            while True:
                r = self.result_queue.get_nowait()
                if "error" in r:
                    self.status_label.configure(text=f"preview error: {r['error']}")
                    continue
                self._preview_data = r["frame"]
                self._perf_samples.append(r["fps"])
                self._display_fps = float(np.mean(self._perf_samples)) if self._perf_samples else 0.0
                self._update_perf_label(extra=r.get("fallbacks") or [])
                self._redraw_preview()
                self.frame_label.configure(text=f"{r['idx'] + 1:,} / {self.total_frames:,}")
        except queue.Empty:
            pass
        self.after(16, self._poll_preview_results)

    def _update_perf_label(self, extra=None):
        extra = extra or []
        caps = self.pipeline.capability_report()
        extra_txt = "  •  fallback" if extra else ""
        self.perf_label.configure(text=f"{self.preview_quality.get()}  •  {self._display_fps:0.1f} fps  •  qdrop:{self.frame_drop_count}  •  fskip:{self.playback_skip_count}  • GPU:{'ON' if caps['gpu_preview'] else 'OFF'}{extra_txt}")

    def _redraw_preview(self):
        if self._preview_data is None:
            return
        cw = self.canvas.winfo_width()
        ch = self.canvas.winfo_height()
        if cw < 2 or ch < 2:
            return
        frame = self._preview_data
        h, w = frame.shape[:2]
        scale = min(cw / w, ch / h)
        nw, nh = max(1, int(w * scale)), max(1, int(h * scale))
        rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
        if self.playing or self.preview_quality.get() != "Ultra":
            rgb = cv2.resize(rgb, (nw, nh), interpolation=cv2.INTER_LINEAR)
            img = Image.fromarray(rgb)
        else:
            img = Image.fromarray(rgb).resize((nw, nh), Image.LANCZOS)
        self.preview_img = ImageTk.PhotoImage(img)
        if self._canvas_image_id is None:
            self._canvas_image_id = self.canvas.create_image(cw // 2, ch // 2, anchor="center", image=self.preview_img)
        else:
            self.canvas.coords(self._canvas_image_id, cw // 2, ch // 2)
            self.canvas.itemconfig(self._canvas_image_id, image=self.preview_img)

    def _timecode_hit_test(self, canvas_x, canvas_y):
        if self._preview_data is None:
            return False
        frame = self._preview_data
        h, w = frame.shape[:2]
        cw = self.canvas.winfo_width()
        ch = self.canvas.winfo_height()
        if cw < 2 or ch < 2:
            return False
        scale = min(cw / w, ch / h)
        nw, nh = int(w * scale), int(h * scale)
        x0 = (cw - nw) // 2
        y0 = (ch - nh) // 2
        y_pct = self.sliders['timecode_y'][0].get() if 'timecode_y' in self.sliders else 92
        x_pct = self.sliders['timecode_x'][0].get() if 'timecode_x' in self.sliders else 2
        tx = x0 + int((x_pct / 100.0) * nw)
        ty = y0 + int((y_pct / 100.0) * nh)
        return tx - 10 <= canvas_x <= tx + 380 and ty - 20 <= canvas_y <= ty + 10

    def _on_canvas_click(self, event):
        if not self.video_path:
            self._browse()
            return
        if not self.toggle_vars.get('timestamp') or not self.toggle_vars['timestamp'].get():
            return
        if self._timecode_hit_test(event.x, event.y):
            self.timecode_dragging = True

    def _on_canvas_drag(self, event):
        if not self.timecode_dragging:
            return
        cw = self.canvas.winfo_width()
        ch = self.canvas.winfo_height()
        if cw < 2 or ch < 2:
            return
        x_pct = np.clip((event.x / max(cw, 1)) * 100.0, 0, 100)
        y_pct = np.clip((event.y / max(ch, 1)) * 100.0, 0, 100)
        if 'timecode_x' in self.sliders:
            self.sliders['timecode_x'][0].set(x_pct)
        if 'timecode_y' in self.sliders:
            self.sliders['timecode_y'][0].set(y_pct)
        self._request_preview(force=True)

    def _on_canvas_release(self, _event):
        self.timecode_dragging = False

    def _toggle_play(self):
        if not self.video_path:
            return
        self.playing = not self.playing
        self.play_btn.configure(text="⏸  PAUSE" if self.playing else "▶  PLAY")
        if self.playing:
            self._play_loop_tick()

    def _play_loop_tick(self):
        if not self.playing:
            return
        profile = PREVIEW_QUALITY.get(self.preview_quality.get(), PREVIEW_QUALITY["Balanced"])
        target_fps = min(float(self.fps or 24), float(profile['fps_cap']))
        start = time.time()

        step = 1
        if self._display_fps > 0:
            if self._display_fps < target_fps * 0.92:
                step = 2
            if self._display_fps < target_fps * 0.72:
                step = 3
            if self._display_fps < target_fps * 0.52:
                step = 4
        self.playback_skip_count += max(0, step - 1)
        self.current_frame_idx = (self.current_frame_idx + step) % max(1, self.total_frames)
        self.seek_var.set(self.current_frame_idx)
        self._request_preview()

        elapsed = time.time() - start
        delay = max(1, int((1000 / max(1, target_fps)) - elapsed * 1000))
        self.after(delay, self._play_loop_tick)

    def _export(self):
        if not self.video_path:
            messagebox.showwarning("No video", "Load a video first.")
            return
        inp = Path(self.video_path)
        out_path = filedialog.asksaveasfilename(defaultextension=".mp4", initialfile=inp.stem + "_akera_glitch.mp4", filetypes=[("MP4", "*.mp4")])
        if not out_path:
            return
        self.export_btn.configure(state="disabled", text="EXPORTING...")
        self.prog_bar.set(0)
        threading.Thread(target=self._run_export_safe, args=(out_path,), daemon=True).start()

    def _run_export_safe(self, out_path):
        try:
            self._run_export(out_path)
        except Exception as e:
            self.after(0, self._export_failed, str(e))

    def _run_export(self, out_path):
        cfg = self._get_cfg()
        cap = cv2.VideoCapture(self.video_path)
        fps = cap.get(cv2.CAP_PROP_FPS) or 24
        w = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
        h = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
        total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
        _enc_name, video_args, enc_label = preferred_export_encoder()
        self.export_encoder_name = enc_label
        has_audio = source_has_audio(self.video_path)

        cmd = ['ffmpeg', '-y', '-loglevel', 'error',
               '-f', 'rawvideo', '-pix_fmt', 'bgr24', '-s', f'{w}x{h}', '-r', f'{fps}', '-i', '-',
               '-i', self.video_path]
        cmd += video_args
        if has_audio:
            cmd += ['-map', '0:v:0', '-map', '1:a:0', '-c:a', 'aac', '-shortest']
        else:
            cmd += ['-map', '0:v:0']
        cmd += [out_path]

        proc = subprocess.Popen(cmd, stdin=subprocess.PIPE)
        fidx = 0
        try:
            while True:
                ret, frame = cap.read()
                if not ret:
                    break
                out = process_frame_full(frame, cfg, fidx, fps)
                proc.stdin.write(out.tobytes())
                fidx += 1
                pct = fidx / max(total, 1)
                self.after(0, self.prog_bar.set, pct)
                self.after(0, self.export_label.configure, {"text": f"Exporting with {enc_label}  {fidx}/{total} ({int(pct * 100)}%)"})
        finally:
            cap.release()
            if proc.stdin:
                proc.stdin.close()
            proc.wait()

        if proc.returncode != 0:
            raise RuntimeError(f"ffmpeg export failed with code {proc.returncode}")
        self.after(0, self._export_done, out_path)

    def _export_failed(self, error_text):
        self.export_btn.configure(state="normal", text="EXPORT VIDEO")
        self.export_label.configure(text=f"export failed — {error_text}")
        messagebox.showerror("Export Failed", error_text)

    def _export_done(self, path):
        self.export_btn.configure(state="normal", text="EXPORT VIDEO")
        self.prog_bar.set(1)
        self.export_label.configure(text=f"saved — {Path(path).name}  •  {self.export_encoder_name}")
        messagebox.showinfo("Done", f"Exported:\n{path}")

    def on_close(self):
        self.playing = False
        self.shutdown_flag.set()
        if self.cap:
            self.cap.release()
        self.destroy()


if __name__ == "__main__":
    app = GlitchApp()
    app.protocol("WM_DELETE_WINDOW", app.on_close)
    app.mainloop()
