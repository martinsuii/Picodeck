#!/usr/bin/env python3
"""
PicoDeck — source editor + layout preview

  - file tree of the project (lazy-loaded)
  - text editor with save / save-as
  - Compile button (setup_env.sh + launcher/c.sh) with live output
  - optional Clean / Release / 2MB flags, Stop, Flash UF2
  - live preview pane (Home / Boot / Screensaver) driven by a .thm theme

Run:  python3 tools/picodeck_editor.py
"""

import os
import shutil
import subprocess
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SKIP_DIRS = {".git", "build", "__pycache__"}

PREVIEW_SCALE = 1.5
SCREEN_W, SCREEN_H = 320, 240

DEFAULT_DUCK = ["         __", "   ___( o)>", "   \\ <_. )", "    `---'"]
MENU_ITEMS = ["PLAY", "LOAD from SD", "TOOLS", "FUN", "SETTINGS"]


def hex2tk(h):
    h = h.lstrip("#")
    return "#" + (h.lower() if len(h) == 6 else "000000")


def shquote(s):
    return "'" + s.replace("'", "'\\''") + "'"


# ---------------------------------------------------------------------------
# Theme model (subset of the launcher's .thm format)
# ---------------------------------------------------------------------------

class Theme:
    def __init__(self):
        self.bg = "000000"
        self.duck_color = "FFFF00"
        self.duck = list(DEFAULT_DUCK)
        self.home_title1 = "PicoDeck"
        self.home_title2 = "Launcher"
        self.menu_bg = "000000"
        self.menu_title = "FFFF00"
        self.menu_text = "FFFFFF"
        self.menu_sel = "FFFF00"
        self.menu_hl = "282800"
        self.menu_dim = "808080"
        self.home_time = 1
        self.home_time_color = "C0C0C0"
        self.home_weather = 1
        self.home_weather_color = "C0C0C0"
        self.boot = [
            dict(type="text", x=-1, y=46, w=0, h=0, text="PicoDeck",
                 color="FFFF00", scale=2),
            dict(type="text", x=-1, y=70, w=0, h=0, text="Launcher",
                 color="FFFF00", scale=2),
            dict(type="bar", x=40, y=150, w=240, h=12, text="",
                 color="00FF00", scale=1),
            dict(type="pct", x=-1, y=172, w=0, h=0, text="",
                 color="FFFFFF", scale=1),
            dict(type="spinner", x=24, y=152, w=0, h=0, text="",
                 color="C0C0C0", scale=1),
        ]
        self.saver = [
            dict(type="text", x=-1, y=14, w=0, h=0, text="PicoDeck",
                 color="808080", scale=1),
            dict(type="duck", x=0, y=54, w=0, h=0, text="",
                 color="FFFF00", scale=1),
            dict(type="status", x=40, y=130, w=0, h=0, text="",
                 color="FFFFFF", scale=1),
            dict(type="time", x=-1, y=182, w=0, h=0, text="",
                 color="FFFFFF", scale=1),
            dict(type="weather", x=20, y=198, w=0, h=0, text="",
                 color="C0C0C0", scale=1),
            dict(type="text", x=-1, y=224, w=0, h=0, text="Press any button",
                 color="808080", scale=1),
        ]

    @classmethod
    def from_text(cls, text):
        th = cls()
        th.boot = []
        th.saver = []
        for raw in text.splitlines():
            line = raw.strip()
            if not line or line.startswith(("#", ";")) or "=" not in line:
                continue
            key, val = line.split("=", 1)
            key, val = key.strip(), val.strip()
            if key == "bg":
                th.bg = val
            elif key == "duck_color":
                th.duck_color = val
            elif key in ("duck1", "duck2", "duck3", "duck4"):
                th.duck[int(key[-1]) - 1] = val
            elif hasattr(th, key) and key not in ("boot", "saver"):
                if key in ("home_time", "home_weather"):
                    setattr(th, key, int(val) if val.isdigit() else 0)
                else:
                    setattr(th, key, val)
            elif key in ("boot", "saver"):
                e = cls._parse_elem(val)
                if e:
                    (th.boot if key == "boot" else th.saver).append(e)
        return th

    @staticmethod
    def _parse_elem(val):
        f = val.split(",")
        t = f[0].strip()
        def gi(i, d=0):
            try:
                return int(f[i])
            except Exception:
                return d
        def gs(i, d=""):
            return f[i] if i < len(f) else d
        if t == "text" and len(f) >= 5:
            return dict(type="text", x=gi(1), y=gi(2), text=gs(3),
                        color=gs(4, "FFFFFF"), scale=gi(5, 1), w=0, h=0)
        if t == "duck":
            return dict(type="duck", x=gi(1), y=gi(2), text="",
                        color="FFFF00", scale=1, w=0, h=0)
        if t == "bar":
            return dict(type="bar", x=gi(1), y=gi(2), w=gi(3), h=gi(4),
                        color=gs(5, "00FF00"), text="", scale=1)
        if t in ("pct", "spinner"):
            return dict(type=t, x=gi(1), y=gi(2), text="",
                        color=gs(3, "FFFFFF"), scale=1, w=0, h=0)
        if t in ("status", "usb", "sd", "battery"):
            return dict(type=t, x=gi(1), y=gi(2), text="",
                        color="FFFFFF", scale=1, w=0, h=0)
        if t == "weather":
            return dict(type="weather", x=gi(1), y=gi(2), text="",
                        color=gs(3, "C0C0C0"), scale=1, w=0, h=0)
        if t == "time":
            return dict(type="time", x=gi(1), y=gi(2), text="",
                        color=gs(3, "FFFFFF"), scale=gi(4, 1), w=0, h=0)
        return None


# ---------------------------------------------------------------------------
# Preview renderer
# ---------------------------------------------------------------------------

class Preview:
    def __init__(self, parent, theme):
        self.theme = theme
        self.screen = "home"
        self.frame = ttk.Frame(parent)

        top = ttk.Frame(self.frame)
        top.pack(fill="x")
        ttk.Label(top, text="Preview").pack(side="left")
        self.screen_var = tk.StringVar(value="home")
        cb = ttk.Combobox(top, textvariable=self.screen_var, width=12,
                          values=["home", "boot", "screensaver"], state="readonly")
        cb.pack(side="right")
        cb.bind("<<ComboboxSelected>>", lambda e: self.redraw())
        ttk.Button(top, text="Load theme…", command=self.load_theme).pack(
            side="right", padx=4)

        self.canvas = tk.Canvas(self.frame, width=int(SCREEN_W * PREVIEW_SCALE),
                                height=int(SCREEN_H * PREVIEW_SCALE), bg="black",
                                highlightthickness=1, highlightbackground="#444")
        self.canvas.pack(pady=4)

    def load_theme(self):
        p = filedialog.askopenfilename(initialdir=os.path.join(ROOT, "themes"),
                                       filetypes=[("Theme files", "*.thm *.ini"),
                                                  ("All files", "*.*")])
        if not p:
            return
        try:
            with open(p, "r", encoding="utf-8", errors="replace") as f:
                self.theme = Theme.from_text(f.read())
            self.redraw()
        except Exception as ex:
            messagebox.showerror("Theme", str(ex))

    def redraw(self):
        self.screen = self.screen_var.get()
        c = self.canvas
        c.delete("all")
        s = PREVIEW_SCALE
        if self.screen == "home":
            self.draw_home(s)
        elif self.screen == "boot":
            self.draw_screen(self.theme.boot, s)
        else:
            self.draw_screen(self.theme.saver, s)

    # --- helpers -----------------------------------------------------------
    def _text(self, x, y, txt, color, scale=1, center=False):
        s = PREVIEW_SCALE
        if center:
            x = (SCREEN_W - len(txt) * 8 * scale) / 2
        self.canvas.create_text(x * s, y * s, text=txt, fill=hex2tk(color),
                                font=("TkFixedFont", int(8 * scale)), anchor="nw")

    def _rect(self, x, y, w, h, color):
        s = PREVIEW_SCALE
        self.canvas.create_rectangle(x * s, y * s, (x + w) * s, (y + h) * s,
                                     fill=hex2tk(color), outline="")

    def draw_home(self, s):
        t = self.theme
        self._rect(0, 0, SCREEN_W, SCREEN_H, t.menu_bg)
        self._text(0, 16, t.home_title1, t.menu_title, 2, center=True)
        self._text(0, 36, t.home_title2, t.menu_title, 2, center=True)
        self._rect(40, 64, SCREEN_W - 80, 1, t.menu_dim)

        for i, label in enumerate(MENU_ITEMS):
            y = 80 + i * 20
            sel = (i == 0)
            if sel:
                self._rect(54, y - 2, SCREEN_W - 108, 12, t.menu_hl)
            self._text(30, y, ">" if sel else " ", t.menu_sel if sel else t.menu_text)
            self._text(60, y, label, t.menu_sel if sel else t.menu_text)

        self._rect(20, 202, SCREEN_W - 40, 1, t.menu_dim)
        self._text(20, 210, "Game: Example", "00FF00")
        # status row
        self._text(20, 222, "SD", "00FF00")
        self._text(52, 222, "USB", "00FF00")
        self._text(84, 222, "WiFi", "00FF00")
        if t.home_weather:
            self._text(130, 222, "12.3C Clear", t.home_weather_color)
        if t.home_time:
            self._text(SCREEN_W - 8 * 8 - 8, 20, "12:34:56", t.home_time_color)
        # battery
        self._text(SCREEN_W - 70, 222, "82%", "00FF00")

    def draw_screen(self, elems, s):
        t = self.theme
        self._rect(0, 0, SCREEN_W, SCREEN_H, t.bg)
        for e in elems:
            self.draw_elem(e)

    def draw_elem(self, e):
        s = PREVIEW_SCALE
        c = self.canvas
        t = e["type"]
        col = e.get("color", "FFFFFF")
        if t == "text":
            self._text(e["x"], e["y"], e["text"], col, e["scale"],
                       center=(e["x"] < 0))
        elif t == "duck":
            for i, line in enumerate(self.theme.duck):
                c.create_text(e["x"] * s, (e["y"] + i * 12) * s, text=line,
                              fill=hex2tk(self.theme.duck_color),
                              font=("TkFixedFont", 8), anchor="nw")
        elif t == "bar":
            self._rect(e["x"], e["y"], e["w"], e["h"], "#444")
            self._rect(e["x"], e["y"], e["w"] * 0.5, e["h"], col)
        elif t == "pct":
            self._text(e["x"], e["y"], "50%", col, 1, center=(e["x"] < 0))
        elif t == "spinner":
            self._text(e["x"], e["y"], "/", col)
        elif t == "status":
            for i, (lab, val) in enumerate([("USB", "connected"),
                                            ("SD", "card present"),
                                            ("Battery", "82%")]):
                self._text(e["x"], e["y"] + i * 22, lab, "C0C0C0")
                self._text(e["x"] + 100, e["y"] + i * 22, val, "00FF00")
        elif t in ("usb", "sd", "battery"):
            self._text(e["x"], e["y"], t, col)
        elif t == "weather":
            self._text(e["x"], e["y"], "12.3C Clear", col)
        elif t == "time":
            self._text(e["x"], e["y"], "12:34:56", col, e["scale"],
                       center=(e["x"] < 0))


# ---------------------------------------------------------------------------
# Source editor
# ---------------------------------------------------------------------------

class Editor:
    def __init__(self, root):
        self.root = root
        root.title("PicoDeck — source editor")
        root.geometry("1400x850")

        self.current_path = None
        self.modified = False
        self.proc = None

        self._build_toolbar()
        self._build_body()
        self.populate_tree()

    def _build_toolbar(self):
        bar = ttk.Frame(self.root)
        bar.pack(fill="x", padx=6, pady=4)

        ttk.Button(bar, text="Open folder…", command=self.pick_root).pack(side="left")
        ttk.Button(bar, text="Save", command=self.save).pack(side="left", padx=4)
        ttk.Button(bar, text="Save As…", command=self.save_as).pack(side="left")

        ttk.Separator(bar, orient="vertical").pack(side="left", fill="y", padx=8)

        self.opt_clean = tk.BooleanVar(value=False)
        self.opt_release = tk.BooleanVar(value=False)
        self.opt_2mb = tk.BooleanVar(value=False)
        ttk.Checkbutton(bar, text="clean", variable=self.opt_clean).pack(side="left")
        ttk.Checkbutton(bar, text="release", variable=self.opt_release).pack(side="left")
        ttk.Checkbutton(bar, text="2MB", variable=self.opt_2mb).pack(side="left")

        self.btn_build = ttk.Button(bar, text="Compile", command=self.compile)
        self.btn_build.pack(side="left", padx=8)
        ttk.Button(bar, text="Stop", command=self.stop_build).pack(side="left")
        ttk.Button(bar, text="Flash UF2", command=self.flash).pack(side="left", padx=4)

        self.status = tk.StringVar(value="Ready.")
        ttk.Label(bar, textvariable=self.status).pack(side="right")

    def _build_body(self):
        pane = ttk.PanedWindow(self.root, orient="horizontal")
        pane.pack(fill="both", expand=True, padx=6)

        left = ttk.Frame(pane)
        pane.add(left, weight=1)
        self.tree = ttk.Treeview(left, show="tree")
        self.tree.pack(side="left", fill="both", expand=True)
        sb = ttk.Scrollbar(left, orient="vertical", command=self.tree.yview)
        sb.pack(side="right", fill="y")
        self.tree.configure(yscrollcommand=sb.set)
        self.tree.bind("<<TreeviewOpen>>", self.on_open)
        self.tree.bind("<<TreeviewSelect>>", self.on_select)
        self.tree.bind("<Button-1>", self.on_click)
        self._clicked = None

        mid = ttk.Frame(pane)
        pane.add(mid, weight=3)
        self.text = tk.Text(mid, wrap="none", undo=True, font=("TkFixedFont", 10))
        self.text.pack(side="left", fill="both", expand=True)
        ysb = ttk.Scrollbar(mid, orient="vertical", command=self.text.yview)
        ysb.pack(side="right", fill="y")
        self.text.configure(yscrollcommand=ysb.set)
        self.text.bind("<<Modified>>", self.on_modified)

        right = ttk.Frame(pane)
        pane.add(right, weight=2)
        self.preview = Preview(right, Theme())
        self.preview.frame.pack(fill="both", expand=True)
        ttk.Button(right, text="Refresh preview",
                   command=self.preview.redraw).pack(pady=2)

        out = ttk.LabelFrame(self.root, text="Build output")
        out.pack(fill="both", expand=False, padx=6, pady=6)
        self.output = tk.Text(out, height=12, wrap="none", font=("TkFixedFont", 9),
                              bg="#111", fg="#ddd")
        self.output.pack(side="left", fill="both", expand=True)
        osb = ttk.Scrollbar(out, orient="vertical", command=self.output.yview)
        osb.pack(side="right", fill="y")
        self.output.configure(yscrollcommand=osb.set)

    # tree
    def populate_tree(self):
        self.tree.delete(*self.tree.get_children())
        node = self.tree.insert("", "end", text=ROOT, open=True, values=[ROOT])
        self._load_dir(node, ROOT)

    def pick_root(self):
        global ROOT
        d = filedialog.askdirectory(initialdir=ROOT)
        if d:
            ROOT = d
            self.populate_tree()

    def _load_dir(self, node, path):
        try:
            entries = sorted(os.listdir(path))
        except Exception:
            return
        for name in entries:
            if name in SKIP_DIRS:
                continue
            full = os.path.join(path, name)
            if os.path.isdir(full):
                child = self.tree.insert(node, "end", text=name + "/",
                                         values=[full])
                # placeholder so the expand arrow shows before first open
                self.tree.insert(child, "end", text="…")
            else:
                self.tree.insert(node, "end", text=name, values=[full])

    def on_click(self, event):
        self._clicked = self.tree.identify_row(event.y)

    def on_open(self, _e=None):
        node = self._clicked if self._clicked else self.tree.focus()
        vals = self.tree.item(node, "values")
        if not vals or not os.path.isdir(vals[0]):
            return
        children = self.tree.get_children(node)
        # replace the placeholder (a child with no path value) on first open
        if len(children) == 1 and not self.tree.item(children[0], "values"):
            self.tree.delete(children[0])
            self._load_dir(node, vals[0])

    def on_select(self, _e=None):
        sel = self.tree.selection()
        if not sel:
            return
        node = sel[0]
        vals = self.tree.item(node, "values")
        if not vals or os.path.isdir(vals[0]):
            return
        if not self.confirm_discard():
            return
        try:
            with open(vals[0], "r", encoding="utf-8", errors="replace") as f:
                data = f.read()
        except Exception as ex:
            messagebox.showerror("Open failed", str(ex))
            return
        self.text.delete("1.0", tk.END)
        self.text.insert("1.0", data)
        self.text.edit_modified(False)
        self.current_path = vals[0]
        self.modified = False
        self.status.set(os.path.relpath(vals[0], ROOT))

    def on_modified(self, _e=None):
        if self.text.edit_modified():
            self.modified = True
            self.status.set("* " + (os.path.relpath(self.current_path, ROOT)
                                    if self.current_path else ""))

    def confirm_discard(self):
        return (not self.modified) or messagebox.askyesno(
            "Unsaved changes", "Discard unsaved changes?")

    def save(self):
        if not self.current_path:
            return self.save_as()
        try:
            with open(self.current_path, "w", encoding="utf-8") as f:
                f.write(self.text.get("1.0", "end-1c"))
            self.text.edit_modified(False)
            self.modified = False
            self.status.set("Saved " + os.path.relpath(self.current_path, ROOT))
        except Exception as ex:
            messagebox.showerror("Save failed", str(ex))

    def save_as(self):
        p = filedialog.asksaveasfilename(initialdir=ROOT)
        if p:
            self.current_path = p
            self.save()

    # compile
    def build_cmd(self):
        flags = []
        if self.opt_release.get():
            flags.append("-DLAUNCHER_RELEASE")
        if self.opt_2mb.get():
            flags.append("-DLAUNCHER_2MB=1")
        clean = ""
        if self.opt_clean.get():
            clean = "rm -rf launcher/build launcher/PICODECK.* && "
        return ("cd %s && source setup_env.sh && %s"
                "cd launcher && EXTRA_CFLAGS=%s bash c.sh"
                % (shquote(ROOT), clean, shquote(" ".join(flags))))

    def compile(self):
        if self.proc:
            return
        cmd = self.build_cmd()
        self.output.delete("1.0", tk.END)
        self.output.insert(tk.END, "$ " + cmd + "\n\n")
        self.btn_build.config(state="disabled")
        self.status.set("Compiling…")

        def run():
            try:
                self.proc = subprocess.Popen(
                    ["bash", "-lc", cmd], stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT, text=True, bufsize=1)
                for line in self.proc.stdout:
                    self.root.after(0, self.append_output, line)
                self.proc.wait()
                rc = self.proc.returncode
            except Exception as ex:
                self.root.after(0, self.append_output, "ERROR: %s\n" % ex)
                rc = -1
            finally:
                self.proc = None
            self.root.after(0, self.build_done, rc)

        threading.Thread(target=run, daemon=True).start()

    def append_output(self, line):
        self.output.insert(tk.END, line)
        self.output.see(tk.END)

    def build_done(self, rc):
        self.btn_build.config(state="normal")
        self.append_output("\n=== exit %d ===\n" % rc)
        self.status.set("Build OK." if rc == 0 else "Build failed (%d)." % rc)

    def stop_build(self):
        if self.proc:
            self.proc.terminate()

    # flash
    def flash(self):
        uf2 = os.path.join(ROOT, "launcher", "PICODECK_install.uf2")
        if not os.path.exists(uf2):
            messagebox.showinfo("Flash", "Build the install UF2 first.")
            return
        dest = self.find_bootsel()
        if not dest:
            dest = filedialog.askdirectory(title="Select the RPI-RP2 / BOOTSEL drive")
            if not dest:
                return
        try:
            shutil.copy(uf2, dest)
            messagebox.showinfo("Flash", "Copied to " + dest)
        except Exception as ex:
            messagebox.showerror("Flash failed", str(ex))

    @staticmethod
    def find_bootsel():
        for base in ("/media", "/run/media", "/Volumes"):
            if not os.path.isdir(base):
                continue
            for dirpath, dirnames, _ in os.walk(base):
                for d in list(dirnames):
                    if "RPI-RP2" in d.upper():
                        return os.path.join(dirpath, d)
                if dirpath.count(os.sep) - base.count(os.sep) > 3:
                    dirnames[:] = []
        return None


def main():
    root = tk.Tk()
    Editor(root)
    root.mainloop()


if __name__ == "__main__":
    main()
