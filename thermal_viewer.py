#!/usr/bin/env python3
"""
Live 8x8 thermal camera visualizer for AMG8833-style serial output.

Expected frame format in serial stream: 8 lines, each with 8 float values.
Example row:
22.50 22.75 23.50 23.50 25.25 26.25 27.25 26.75

The parser ignores non-data lines, so debug messages from firmware are fine.
"""

import argparse
import queue
import re
import threading
import time
import tkinter as tk
from tkinter import ttk
from tkinter import TclError

import serial


FLOAT_RE = re.compile(r"[-+]?\d+(?:\.\d+)?")


class ThermalViewer:
    def __init__(
        self,
        root: tk.Tk,
        port: str,
        baudrate: int,
        temp_min: float = 20.0,
        temp_max: float = 35.0,
        timeout: float = 0.2,
    ):
        self.root = root
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout

        self.running = True
        self.frame_queue: queue.Queue[list[list[float]]] = queue.Queue(maxsize=5)

        self.rows: list[list[float]] = []
        self.last_frame: list[list[float]] = [[0.0 for _ in range(8)] for _ in range(8)]

        self.cell_size = 48
        self.canvas_size = self.cell_size * 8

        self.fixed_min = tk.StringVar(value=f"{temp_min:.1f}")
        self.fixed_max = tk.StringVar(value=f"{temp_max:.1f}")
        self.auto_scale = tk.BooleanVar(value=False)
        self.last_valid_tmin = temp_min
        self.last_valid_tmax = temp_max

        self.status_var = tk.StringVar(value="Connecting...")
        self.minmax_var = tk.StringVar(value="frame min/max: --.- C / --.- C")
        self.scale_var = tk.StringVar(value=f"color scale: {temp_min:.1f} C to {temp_max:.1f} C")

        self._build_ui()
        self._connect_serial()

        self.reader_thread = threading.Thread(target=self._serial_reader, daemon=True)
        self.reader_thread.start()

        self.root.after(30, self._ui_tick)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _build_ui(self) -> None:
        self.root.title("Thermal Camera Viewer")
        self.root.geometry("520x560")
        self.root.minsize(460, 500)
        try:
            # Helps some WMs treat it as a small utility app instead of a tiled main window.
            self.root.wm_attributes("-type", "utility")
        except TclError:
            pass
        try:
            # Stable class for compositor rules (useful for Hyprland windowrulev2).
            self.root.tk.call("wm", "class", self.root._w, "ThermalViewer")
        except (TclError, AttributeError):
            pass
        self.root.configure(bg="#101418")

        container = ttk.Frame(self.root, padding=10)
        container.pack(fill=tk.BOTH, expand=True)

        top_row = ttk.Frame(container)
        top_row.pack(fill=tk.X)

        ttk.Label(top_row, text="Serial Port:").pack(side=tk.LEFT)
        ttk.Label(top_row, text=self.port).pack(side=tk.LEFT, padx=(6, 14))
        ttk.Label(top_row, text="Baud:").pack(side=tk.LEFT)
        ttk.Label(top_row, text=str(self.baudrate)).pack(side=tk.LEFT, padx=(6, 14))
        ttk.Label(top_row, textvariable=self.status_var).pack(side=tk.LEFT)

        controls = ttk.Frame(container)
        controls.pack(fill=tk.X, pady=(8, 8))

        ttk.Checkbutton(controls, text="Auto scale", variable=self.auto_scale).pack(side=tk.LEFT)
        ttk.Label(controls, text="Min").pack(side=tk.LEFT, padx=(16, 4))
        ttk.Entry(controls, textvariable=self.fixed_min, width=6).pack(side=tk.LEFT)
        ttk.Label(controls, text="Max").pack(side=tk.LEFT, padx=(10, 4))
        ttk.Entry(controls, textvariable=self.fixed_max, width=6).pack(side=tk.LEFT)
        ttk.Label(controls, textvariable=self.minmax_var).pack(side=tk.LEFT, padx=(16, 0))
        ttk.Label(controls, textvariable=self.scale_var).pack(side=tk.LEFT, padx=(16, 0))

        self.canvas = tk.Canvas(
            container,
            width=self.canvas_size,
            height=self.canvas_size,
            bg="#0b0f12",
            highlightthickness=0,
        )
        self.canvas.pack()

        self.cells: list[list[int]] = [[0 for _ in range(8)] for _ in range(8)]
        self.labels: list[list[int]] = [[0 for _ in range(8)] for _ in range(8)]

        for y in range(8):
            for x in range(8):
                x1 = x * self.cell_size
                y1 = y * self.cell_size
                x2 = x1 + self.cell_size
                y2 = y1 + self.cell_size
                rect = self.canvas.create_rectangle(
                    x1,
                    y1,
                    x2,
                    y2,
                    fill="#000000",
                    outline="#1f2a30",
                )
                label = self.canvas.create_text(
                    x1 + self.cell_size / 2,
                    y1 + self.cell_size / 2,
                    text="0.0",
                    fill="#dce7ef",
                    font=("TkDefaultFont", 9),
                )
                self.cells[y][x] = rect
                self.labels[y][x] = label

    def _connect_serial(self) -> None:
        try:
            self.ser = serial.Serial(self.port, self.baudrate, timeout=self.timeout)
            # Let MCU reset and start streaming.
            time.sleep(1.5)
            self.status_var.set("Connected")
        except Exception as exc:
            self.ser = None
            self.status_var.set(f"Serial error: {exc}")

    def _serial_reader(self) -> None:
        if self.ser is None:
            return

        while self.running:
            try:
                raw = self.ser.readline()
                if not raw:
                    continue

                line = raw.decode("utf-8", errors="ignore").strip()
                nums = [float(x) for x in FLOAT_RE.findall(line)]

                # Keep only clean matrix rows: exactly 8 numbers in a line.
                if len(nums) == 8:
                    self.rows.append(nums)
                    if len(self.rows) == 8:
                        frame = self.rows
                        self.rows = []
                        try:
                            self.frame_queue.put_nowait(frame)
                        except queue.Full:
                            # Drop oldest queued frame to keep UI responsive.
                            try:
                                _ = self.frame_queue.get_nowait()
                            except queue.Empty:
                                pass
                            self.frame_queue.put_nowait(frame)
                else:
                    # Any non-row line breaks frame assembly to avoid mixing data.
                    self.rows = []

            except Exception as exc:
                self.status_var.set(f"Read error: {exc}")
                self.running = False
                break

    def _ui_tick(self) -> None:
        updated = False
        while True:
            try:
                frame = self.frame_queue.get_nowait()
                self.last_frame = frame
                updated = True
            except queue.Empty:
                break

        if updated:
            self._draw_frame(self.last_frame)

        if self.running:
            self.root.after(30, self._ui_tick)

    def _draw_frame(self, frame: list[list[float]]) -> None:
        values = [v for row in frame for v in row]
        fmin = min(values)
        fmax = max(values)

        if self.auto_scale.get():
            tmin = fmin
            tmax = fmax
        else:
            tmin, tmax = self._get_fixed_scale_limits()

        if tmax <= tmin:
            tmax = tmin + 0.001

        self.minmax_var.set(f"frame min/max: {fmin:.2f} C / {fmax:.2f} C")
        self.scale_var.set(f"color scale: {tmin:.1f} C to {tmax:.1f} C")

        for y in range(8):
            for x in range(8):
                value = frame[y][x]
                color = self._temperature_to_color(value, tmin, tmax)
                self.canvas.itemconfig(self.cells[y][x], fill=color)
                self.canvas.itemconfig(self.labels[y][x], text=f"{value:.1f}")

    def _get_fixed_scale_limits(self) -> tuple[float, float]:
        try:
            tmin = float(self.fixed_min.get().strip())
        except (ValueError, TclError, AttributeError):
            tmin = self.last_valid_tmin

        try:
            tmax = float(self.fixed_max.get().strip())
        except (ValueError, TclError, AttributeError):
            tmax = self.last_valid_tmax

        self.last_valid_tmin = tmin
        self.last_valid_tmax = tmax
        return tmin, tmax

    @staticmethod
    def _temperature_to_color(value: float, tmin: float, tmax: float) -> str:
        # Normalized 0..1
        t = (value - tmin) / (tmax - tmin)
        t = max(0.0, min(1.0, t))

        # 4-point gradient: blue -> cyan -> yellow -> red
        if t < 0.33:
            p = t / 0.33
            r = int(0 + p * 0)
            g = int(0 + p * 255)
            b = int(180 + p * (255 - 180))
        elif t < 0.66:
            p = (t - 0.33) / 0.33
            r = int(0 + p * 255)
            g = int(255)
            b = int(255 - p * 255)
        else:
            p = (t - 0.66) / 0.34
            r = int(255)
            g = int(255 - p * 255)
            b = 0

        return f"#{r:02x}{g:02x}{b:02x}"

    def _on_close(self) -> None:
        self.running = False
        try:
            if self.ser is not None and self.ser.is_open:
                self.ser.close()
        except Exception:
            pass
        self.root.destroy()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Serial thermal camera viewer (8x8)")
    parser.add_argument("--port", default="/dev/ttyACM0", help="Serial port (default: /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=9600, help="Baud rate (default: 9600)")
    parser.add_argument("--tmin", type=float, default=20.0, help="Fixed scale min temp in C (default: 20.0)")
    parser.add_argument("--tmax", type=float, default=35.0, help="Fixed scale max temp in C (default: 35.0)")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    root = tk.Tk()
    _ = ThermalViewer(
        root,
        port=args.port,
        baudrate=args.baud,
        temp_min=args.tmin,
        temp_max=args.tmax,
    )
    root.mainloop()


if __name__ == "__main__":
    main()
