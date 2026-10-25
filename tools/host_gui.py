# -*- coding: utf-8 -*-
"""
BMS 上位机 · Modbus RTU 从站监控/控制
====================================
配套从站：BMS-FreeRTOS modbus-rtu 分支（FreeModbus，9600 8N1，从站地址 1）

寄存器表（功能码 03，地址 0 起，共 24 个）：
  0  电芯数        1  电芯位图      2~16 Cell1~Cell15 电压 mV
  17 总压 mV       18 电流 mA int16（放电为正，充电为负）
  19 SOC x10（0xFFFF=未锚定）      20 温度 x10 int16
  21 故障位图      22 均衡位图      23 MOS 状态（bit0 充电管 bit1 放电管，1=开）
线圈（功能码 05 写 / 01 读，地址 0 起，共 3 个）：
  0 充电使能       1 放电使能       2 均衡使能
故障位定义（与固件 FaultCheck 一致）：
  0x01 过压OV  0x02 欠压UV  0x04 充电过流COC  0x08 放电过流DOC
  0x10 短路SC  0x20 过温OT  0x40 低温UT

依赖：pyserial（pip install pyserial）。tkinter 为 Python 自带。
运行：双击 启动上位机.bat，或  python host_gui.py   （不接硬件可勾选"演示模式"）
"""

import queue
import threading
import time
import traceback

import tkinter as tk
from tkinter import ttk, messagebox

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    serial = None  # 演示模式下没有 pyserial 也能开界面


# ----------------------------------------------------------------------------
# 最小 Modbus RTU 主站（只实现本项目用到的 03/01/05）
# ----------------------------------------------------------------------------
def crc16(data: bytes) -> int:
    """Modbus CRC-16。"""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


class RtuMaster:
    """一个串口 + 三个功能码。所有方法抛异常表示通信失败。"""

    def __init__(self, port: str, slave: int = 1, baud: int = 9600):
        self.slave = slave
        self.ser = serial.Serial(port, baud, bytesize=8,
                                 parity=serial.PARITY_NONE, stopbits=1,
                                 timeout=0.5, write_timeout=0.5)
        self.ser.reset_input_buffer()

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    def _xfer(self, req: bytes, expect_len: int) -> bytes:
        """发一帧收一帧：校验 CRC、长度、异常码，返回去掉 CRC 的正文。"""
        self.ser.reset_input_buffer()
        self.ser.write(req)
        resp = self.ser.read(expect_len)
        if len(resp) < 4:
            raise TimeoutError(f"响应不完整({len(resp)}字节)")
        body, rx_crc = resp[:-2], resp[-2] | (resp[-1] << 8)
        if crc16(body) != rx_crc:
            raise ValueError("CRC 校验错误")
        if body[0] != self.slave:
            raise ValueError(f"从站地址不符({body[0]})")
        if body[1] & 0x80:
            raise ValueError(f"从站异常码 {body[2]:#04x}")
        return body

    def read_holding(self, start: int, qty: int):
        """FC03 读保持寄存器 -> list[int]"""
        req = bytes([self.slave, 0x03, start >> 8, start & 0xFF,
                     qty >> 8, qty & 0xFF])
        crc = crc16(req)
        req += bytes([crc & 0xFF, crc >> 8])
        body = self._xfer(req, 5 + 2 * qty)
        if len(body) < 5 or body[2] != 2 * qty:
            raise ValueError("FC03 字节数不符")
        return [(body[3 + 2 * i] << 8) | body[4 + 2 * i] for i in range(qty)]

    def read_coils(self, start: int, qty: int):
        """FC01 读线圈 -> list[bool]"""
        req = bytes([self.slave, 0x01, start >> 8, start & 0xFF,
                     qty >> 8, qty & 0xFF])
        crc = crc16(req)
        req += bytes([crc & 0xFF, crc >> 8])
        body = self._xfer(req, 5 + (qty + 7) // 8)
        if len(body) < 4 or body[2] != (qty + 7) // 8:
            raise ValueError("FC01 字节数不符")
        return [bool(body[3 + i // 8] >> (i % 8) & 1) for i in range(qty)]

    def write_coil(self, addr: int, on: bool):
        """FC05 写单线圈。正常响应=请求回显，校验一致才算成功。"""
        val = 0xFF00 if on else 0x0000
        req = bytes([self.slave, 0x05, addr >> 8, addr & 0xFF,
                     val >> 8, val & 0xFF])
        crc = crc16(req)
        req += bytes([crc & 0xFF, crc >> 8])
        body = self._xfer(req, 8)
        if body[2:6] != req[2:6]:
            raise ValueError("FC05 回显不符")


def to_signed(v: int) -> int:
    return v - 65536 if v > 0x7FFF else v


# ----------------------------------------------------------------------------
# 数据快照与解码表
# ----------------------------------------------------------------------------
class Snapshot:
    __slots__ = ("regs", "coils", "ok", "err_text", "demo")

    def __init__(self, regs=None, coils=None, ok=False, err_text="", demo=False):
        self.regs = regs or [0] * 24
        self.coils = coils or [False] * 3
        self.ok = ok
        self.err_text = err_text
        self.demo = demo


FAULT_BITS = [
    (0x01, "过压OV"), (0x02, "欠压UV"), (0x04, "充电过流COC"),
    (0x08, "放电过流DOC"), (0x10, "短路SC"), (0x20, "过温OT"), (0x40, "低温UT"),
]
# 界面故障条用短名（固定宽度内塞得下全部故障，不会顶宽）
FAULT_SHORT = {0x01: "过压", 0x02: "欠压", 0x04: "充电过流", 0x08: "放电过流",
               0x10: "短路", 0x20: "过温", 0x40: "低温"}
CELL_NAMES = [f"Cell{i + 1}" for i in range(15)]


def decode_faults(v: int):
    return [name for bit, name in FAULT_BITS if v & bit]


# ----------------------------------------------------------------------------
# 采集线程：串口轮询 / 演示数据生成
# ----------------------------------------------------------------------------
class Worker(threading.Thread):
    """唯一的串口使用者。GUI 只通过 rx_queue(快照) / tx_queue(写线圈命令) 交互。"""

    POLL_S = 1.0

    def __init__(self, app):
        super().__init__(daemon=True)
        self.app = app
        self.stop_flag = threading.Event()

    def run(self):
        demo = self.app.demo_mode
        master = None
        if not demo:
            if serial is None:
                self.app.rx.put(Snapshot(err_text="未安装 pyserial"))
                return
            try:
                master = RtuMaster(self.app.port, slave=1)
                self.app.rx.put(Snapshot(ok=True))     # 连接成功事件
            except Exception as e:
                self.app.rx.put(Snapshot(err_text=str(e)))
                return
        t = 0
        while not self.stop_flag.is_set():
            t += 1
            while True:                       # 先处理 GUI 下发的写线圈命令
                try:
                    idx, on = self.app.tx.get_nowait()
                except queue.Empty:
                    break
                if demo:
                    self.app.demo_coils[idx] = on
                    self.app.log(f"演示: 写线圈{idx} = {int(on)}")
                else:
                    try:
                        master.write_coil(idx, on)
                        self.app.log(f"写线圈{idx}（{['充电', '放电', '均衡'][idx]}使能）= {int(on)}")
                    except Exception as e:
                        self.app.log(f"写线圈失败: {e}")
            if demo:
                self.app.rx.put(self._demo_snapshot(t))
            else:
                try:
                    regs = master.read_holding(0, 24)
                    coils = master.read_coils(0, 3)
                    self.app.rx.put(Snapshot(regs, coils, ok=True))
                except Exception as e:
                    self.app.rx.put(Snapshot(err_text=str(e)))
            time.sleep(self.POLL_S)
        if master:
            master.close()

    def _demo_snapshot(self, t: int) -> Snapshot:
        """无硬件演示数据：9 节慢波动、每 10s 一批均衡、周期性演示一次欠压。"""
        used = [0, 1, 4, 5, 6, 9, 10, 11, 14]          # 与固件 cell_used 一致
        regs = [0] * 24
        for i in used:
            regs[2 + i] = 3700 + int(18 * ((i * 37 + t) % 20 - 10))
        regs[2 + 5] = 3540                              # Cell6 偏低制造压差
        regs[0] = len(used)
        regs[1] = sum(1 << i for i in used)
        regs[17] = sum(regs[2 + i] for i in used)
        regs[18] = 4 if t % 15 else 7
        regs[19] = 453
        regs[20] = 295
        regs[21] = 0x02 if (t // 30) % 4 == 3 else 0
        bal = 0x0003 if (t // 10) % 2 else 0
        regs[22] = bal
        regs[23] = 0x02 if regs[21] else 0x03
        return Snapshot(regs, list(self.app.demo_coils), ok=True, demo=True)


# ----------------------------------------------------------------------------
# 主题与绘图小工具
# ----------------------------------------------------------------------------
PAGE   = "#0e1014"
PANEL  = "#15181e"
CARD   = "#1b1f27"
CARD2  = "#222834"
BORDER = "#252a34"
TXT    = "#e8ebf1"
SUB    = "#8b93a3"
GREY   = "#5a616e"
GREEN  = "#2ecc71"
RED    = "#ef5350"
AMBER  = "#f5b942"
BLUE   = "#5aa9f0"

FONT_UI   = ("Microsoft YaHei UI", 9)
FONT_SM   = ("Microsoft YaHei UI", 8)
FONT_NAME = ("Consolas", 8)
FONT_VAL  = ("Consolas", 13, "bold")
FONT_BIG  = ("Consolas", 17, "bold")
FONT_LOGO = ("Microsoft YaHei UI", 13, "bold")


def round_rect(cv: tk.Canvas, x1, y1, x2, y2, r, **kw):
    """Canvas 圆角矩形（smooth 折线近似）。"""
    pts = [x1 + r, y1, x2 - r, y1, x2, y1, x2, y1 + r,
           x2, y2 - r, x2, y2, x2 - r, y2, x1 + r, y2,
           x1, y2, x1, y2 - r, x1, y1 + r, x1, y1]
    return cv.create_polygon(pts, smooth=True, **kw)


def panel(parent, title=None):
    f = tk.Frame(parent, bg=PANEL, highlightthickness=1, highlightbackground=BORDER)
    if title is not None:
        tk.Label(f, text=title, bg=PANEL, fg=SUB,
                 font=("Microsoft YaHei UI", 8, "bold")).pack(anchor="w", padx=12,
                                                              pady=(8, 2))
    return f


def dark_titlebar(root: tk.Tk):
    """Win10 20H1+ / Win11 深色标题栏。失败静默（老系统不支持）。"""
    try:
        import ctypes
        root.update_idletasks()
        hwnd = ctypes.windll.user32.GetParent(root.winfo_id())
        value = ctypes.c_int(1)
        ctypes.windll.dwmapi.DwmSetWindowAttribute(
            hwnd, 20, ctypes.byref(value), ctypes.sizeof(value))
    except Exception:
        pass


class ToggleSwitch(tk.Canvas):
    """画出来的拨动开关。"""

    W, H = 46, 24

    def __init__(self, parent, on: bool, command):
        super().__init__(parent, width=self.W, height=self.H, bg=CARD,
                         highlightthickness=0, cursor="hand2")
        self.on = on
        self.command = command
        self.bind("<Button-1>", lambda e: self.command())
        self.draw()

    def draw(self):
        self.delete("all")
        track = GREEN if self.on else "#3a4150"
        round_rect(self, 2, 3, self.W - 2, self.H - 3, (self.H - 6) / 2, fill=track)
        cx = self.W - 14 if self.on else 14
        self.create_oval(cx - 8, 4, cx + 8, self.H - 4, fill="#f4f6fa", width=0)

    def set(self, on: bool):
        if on != self.on:
            self.on = on
            self.draw()


# ----------------------------------------------------------------------------
# GUI
# ----------------------------------------------------------------------------
class App:
    def __init__(self, root: tk.Tk):
        self.root = root
        self.rx: "queue.Queue[Snapshot]" = queue.Queue()
        self.tx: "queue.Queue[tuple[int, bool]]" = queue.Queue()
        self.worker = None
        self.demo_mode = False
        self.demo_coils = [True, True, True]
        self.port = ""
        self.err_count = 0
        self.online = False
        self.last_snap = None
        self.last_fault = -1
        self.last_bal = -1
        self._ui_cache = {}          # 控件上次绘制状态：变化才重绘（防闪烁/抖动）
        self._bar_w = {}

        root.title("BMS 上位机 · Modbus RTU (9600 8N1)")
        root.configure(bg=PAGE)
        root.geometry("1180x860")
        root.minsize(1040, 740)
        dark_titlebar(root)
        self._build()
        self.refresh_ports()
        self.drain()
        root.protocol("WM_DELETE_WINDOW", self.on_close)
        self._center()

    def _center(self):
        try:
            self.root.update_idletasks()
            w, h = 1180, 860
            x = (self.root.winfo_screenwidth() - w) // 2
            y = (self.root.winfo_screenheight() - h) // 2 - 20
            self.root.geometry(f"{w}x{h}+{x}+{y}")
        except Exception:
            pass

    # ---------------------------------------------------------------- 界面
    def _build(self):
        tk.Frame(self.root, bg=GREEN, height=3).pack(fill="x")   # 顶部强调线
        self._build_topbar()

        body = tk.Frame(self.root, bg=PAGE)
        body.pack(fill="both", expand=True, padx=12, pady=(8, 0))
        body.columnconfigure(0, weight=5)
        body.columnconfigure(1, weight=4)
        body.rowconfigure(0, weight=1)

        self._build_cells(body)
        right = tk.Frame(body, bg=PAGE)
        right.grid(row=0, column=1, sticky="nsew", padx=(10, 0))
        right.columnconfigure(0, weight=1)
        self._build_summary(right)
        self._build_fault(right)
        self._build_ctrl(right)
        self._build_log(self.root)

    def _build_topbar(self):
        top = tk.Frame(self.root, bg=PAGE)
        top.pack(fill="x", padx=12, pady=(10, 4))

        left = tk.Frame(top, bg=PAGE)
        left.pack(side="left")
        tk.Label(left, text="● BMS 上位机", bg=PAGE, fg=GREEN,
                 font=FONT_LOGO).pack(side="left")
        tk.Label(left, text="Modbus RTU · 9600 8N1 · 从站 1", bg=PAGE, fg=SUB,
                 font=FONT_SM).pack(side="left", padx=(10, 0), pady=(5, 0))

        right = tk.Frame(top, bg=PAGE)
        right.pack(side="right")
        self.status_pill = tk.Canvas(right, width=92, height=26, bg=PAGE,
                                     highlightthickness=0)
        self.status_pill.pack(side="right", padx=(12, 0))
        self.lbl_err = tk.Label(right, text="", fg=SUB, bg=PAGE, font=FONT_SM, width=24, anchor="e")
        self.lbl_err.pack(side="right", padx=8)
        self._draw_pill(False)

        mid = tk.Frame(top, bg=PAGE)
        mid.pack(side="right", padx=10)
        self.var_demo = tk.BooleanVar(value=False)
        tk.Checkbutton(mid, text="演示模式", variable=self.var_demo,
                       command=self.on_demo_toggle, bg=PAGE, fg=SUB,
                       activebackground=PAGE, activeforeground=TXT,
                       selectcolor=CARD, font=FONT_UI).pack(side="right", padx=(8, 0))
        self.btn_conn = tk.Button(mid, text="连 接", width=8, relief="flat",
                                  command=self.toggle_conn, bg=GREEN, fg="#0b0e12",
                                  activebackground="#27ae60",
                                  font=("Microsoft YaHei UI", 9, "bold"))
        self.btn_conn.pack(side="right", padx=8)
        self.cb_port = ttk.Combobox(mid, width=17, state="readonly", font=FONT_SM)
        self.cb_port.pack(side="right")
        tk.Button(mid, text="↻", width=3, relief="flat", bg=CARD, fg=TXT,
                  activebackground=CARD2, command=self.refresh_ports).pack(
            side="right", padx=4)
        style = ttk.Style()
        try:
            style.theme_use("clam")
            style.configure("TCombobox", fieldbackground=CARD, background=CARD,
                            foreground=TXT, arrowcolor=TXT, bordercolor=BORDER,
                            lightcolor=CARD, darkcolor=CARD)
            style.map("TCombobox", fieldbackground=[("readonly", CARD)],
                      foreground=[("readonly", TXT)])
        except Exception:
            pass

    def _draw_pill(self, online: bool):
        c = self.status_pill
        c.delete("all")
        round_rect(c, 1, 2, 91, 24, 11, fill=CARD, outline=BORDER)
        c.create_oval(10, 8, 18, 16, fill=GREEN if online else GREY, width=0)
        c.create_text(52, 13, text="在 线" if online else "离 线",
                      fill=GREEN if online else SUB, font=("Microsoft YaHei UI", 9, "bold"))

    def _build_cells(self, parent):
        p = panel(parent, "单体电压 · mV")
        p.grid(row=0, column=0, sticky="nsew")
        grid = tk.Frame(p, bg=PANEL)
        grid.pack(fill="both", expand=True, padx=9, pady=(2, 9))
        for c in range(5):
            grid.columnconfigure(c, weight=1, uniform="cell")
        for r in range(3):
            grid.rowconfigure(r, weight=1)

        self.tiles = []
        for i, name in enumerate(CELL_NAMES):
            tile = tk.Frame(grid, bg=CARD, highlightthickness=1,
                            highlightbackground=BORDER)
            tile.grid(row=i // 5, column=i % 5, sticky="nsew", padx=3, pady=3)
            head = tk.Frame(tile, bg=CARD)
            head.pack(fill="x", padx=8, pady=(6, 0))
            tk.Label(head, text=name, bg=CARD, fg=SUB, font=FONT_NAME).pack(side="left")
            lbl_bal = tk.Label(head, text="", bg=CARD, fg=AMBER, font=FONT_SM, width=2)
            lbl_bal.pack(side="right")
            bar = tk.Canvas(tile, width=10, height=6, bg=CARD, highlightthickness=0)
            bar.pack(side="bottom", fill="x", padx=10, pady=(0, 10))
            bar.bind("<Configure>", lambda e, i=i: self._ui_cache.pop(("bar", i), None))
            lbl_val = tk.Label(tile, text="—", bg=CARD, fg=GREY, font=FONT_VAL, width=5)
            lbl_val.pack(expand=True, pady=(4, 0))
            lbl_delta = tk.Label(tile, text="", bg=CARD, fg=GREY, font=FONT_NAME, width=7)
            lbl_delta.pack(pady=(0, 2))
            self.tiles.append({"frame": tile, "val": lbl_val,
                               "bal": lbl_bal, "delta": lbl_delta, "bar": bar})

    def _kpi(self, parent, label, row, col, big=False):
        card = tk.Frame(parent, bg=CARD, highlightthickness=1,
                        highlightbackground=BORDER)
        card.grid(row=row, column=col, sticky="nsew", padx=2, pady=2)
        tk.Label(card, text=label, bg=CARD, fg=SUB, font=FONT_SM).pack(
            anchor="w", padx=10, pady=(7, 0))
        var = tk.StringVar(value="--")
        tk.Label(card, textvariable=var, bg=CARD, fg=TXT, width=10, anchor="w",
                 font=FONT_BIG if big else ("Consolas", 12, "bold")).pack(
            anchor="w", padx=10, pady=(0, 7))
        return card, var

    def _build_summary(self, parent):
        p = panel(parent, "电池组")
        p.grid(row=0, column=0, sticky="ew")
        grid = tk.Frame(p, bg=PANEL)
        grid.pack(fill="x", padx=8, pady=(2, 4))
        for c in range(2):
            grid.columnconfigure(c, weight=1, uniform="kpi")

        _, self.var_total = self._kpi(grid, "总压", 0, 0, big=True)
        _, self.var_temp = self._kpi(grid, "温度 °C", 0, 1, big=True)

        # SOC 卡片：大数字 + 圆角进度条
        soccard = tk.Frame(grid, bg=CARD, highlightthickness=1,
                           highlightbackground=BORDER)
        soccard.grid(row=1, column=0, columnspan=2, sticky="nsew", padx=2, pady=2)
        top = tk.Frame(soccard, bg=CARD)
        top.pack(fill="x", padx=10, pady=(7, 0))
        tk.Label(top, text="SOC", bg=CARD, fg=SUB, font=FONT_SM).pack(side="left")
        self.var_soc = tk.StringVar(value="--")
        tk.Label(top, textvariable=self.var_soc, bg=CARD, fg=GREEN, width=8,
                 anchor="e", font=("Consolas", 15, "bold")).pack(side="right")
        self.soc_bar = tk.Canvas(soccard, width=10, height=12, bg=CARD, highlightthickness=0)
        self.soc_bar.pack(fill="x", padx=10, pady=(2, 8))
        self.soc_bar.bind("<Configure>", lambda e: self._ui_cache.pop(("socbar",), None))

        _, self.var_cur = self._kpi(grid, "电流 mA（放电为正）", 2, 0)
        _, self.var_spread = self._kpi(grid, "压差 ΔV（mV）", 2, 1)

        balcard = tk.Frame(grid, bg=CARD, highlightthickness=1,
                           highlightbackground=BORDER)
        balcard.grid(row=3, column=0, columnspan=2, sticky="nsew", padx=2, pady=2)
        tk.Label(balcard, text="均衡集合", bg=CARD, fg=SUB, font=FONT_SM).pack(
            anchor="w", padx=10, pady=(6, 0))
        self.var_balnow = tk.StringVar(value="--")
        tk.Label(balcard, textvariable=self.var_balnow, bg=CARD, fg=AMBER,
                 width=44, anchor="w",
                 font=("Microsoft YaHei UI", 10, "bold")).pack(
            anchor="w", padx=10, pady=(0, 6))

    def _build_fault(self, parent):
        p = panel(parent, "故障状态")
        p.grid(row=1, column=0, sticky="ew", pady=8)
        self.lbl_fault = tk.Label(p, text="等待数据…", bg=CARD, fg=SUB,
                                  width=33, anchor="w",
                                  font=("Microsoft YaHei UI", 11, "bold"),
                                  padx=12, pady=6)
        self.lbl_fault.pack(anchor="w", padx=10, pady=(0, 10))

    def _build_ctrl(self, parent):
        p = panel(parent, "远程控制 · 线圈 0/1/2，点击卡片或开关切换请求")
        p.grid(row=2, column=0, sticky="ew")
        inner = tk.Frame(p, bg=PANEL)
        inner.pack(fill="x", padx=10, pady=(2, 4))

        self.coil_cards = []
        infos = [("允许充电", "线圈 0", "实际充电管：--"),
                 ("允许放电", "线圈 1", "实际放电管：--"),
                 ("允许均衡", "线圈 2", "实际均衡：--")]
        for i, (title, coil, actual) in enumerate(infos):
            card = tk.Frame(inner, bg=CARD, highlightthickness=1,
                            highlightbackground=BORDER, cursor="hand2")
            card.pack(fill="x", pady=3)
            head = tk.Frame(card, bg=CARD)
            head.pack(fill="x", padx=12, pady=(8, 0))
            lbl_title = tk.Label(head, text=title, bg=CARD, fg=TXT,
                                 font=("Microsoft YaHei UI", 11, "bold"))
            lbl_title.pack(side="left")
            tk.Label(head, text=coil, bg=CARD, fg=SUB, font=FONT_SM).pack(
                side="left", padx=8, pady=(3, 0))
            switch = ToggleSwitch(head, True, lambda i=i: self.on_coil_click(i))
            switch.pack(side="right")
            lbl_state = tk.Label(head, text="● 允许中", bg=CARD, fg=GREEN,
                                 font=("Microsoft YaHei UI", 9, "bold"))
            lbl_state.pack(side="right", padx=8)
            lbl_actual = tk.Label(card, text=actual, bg=CARD, fg=SUB, font=FONT_SM, width=42, anchor="w")
            lbl_actual.pack(anchor="w", padx=12, pady=(1, 8))
            for w in (card, head, lbl_title, lbl_state, lbl_actual):
                w.bind("<Button-1>", lambda e, i=i: self.on_coil_click(i))
            self.coil_cards.append({"frame": card, "state": lbl_state,
                                    "actual": lbl_actual, "switch": switch})

        tk.Label(p, text="卡片开关 = 上位机“请求”；灰色小字 = 固件实际执行结果。"
                         "固件按 请求 AND 无故障 执行：故障期间请求“允许”不会生效，"
                         "以实际行为准（不一致时该行变琥珀色）。"
                         "均衡另需：静置、SOC 已锚定、压差≥30mV。",
                 bg=PANEL, fg=GREY, justify="left", anchor="w", font=FONT_SM,
                 wraplength=420).pack(fill="x", padx=12, pady=(0, 8))

    def _build_log(self, parent):
        p = panel(parent, "事件日志（边沿触发）")
        p.pack(fill="x", expand=False, padx=12, pady=(8, 12))
        self.txt_log = tk.Text(p, height=4, state="disabled", bg="#0b0d11",
                               fg="#9aa3b2", relief="flat", font=("Consolas", 9),
                               insertbackground=TXT)
        self.txt_log.pack(fill="both", expand=True, padx=8, pady=(0, 8))

    # ---------------------------------------------------------------- 事件
    def refresh_ports(self):
        if serial is None:
            self.cb_port["values"] = []
            return
        ports = [f"{p.device}  {p.description}"
                 for p in serial.tools.list_ports.comports()]
        self.cb_port["values"] = ports
        if ports:
            self.cb_port.current(0)

    def on_demo_toggle(self):
        self.demo_mode = self.var_demo.get()
        if self.worker:
            self.toggle_conn()
        if self.demo_mode:
            self.start_worker()

    def toggle_conn(self):
        if self.worker:
            self.stop_worker()
        else:
            if not self.demo_mode:
                pick = self.cb_port.get().split()[0] if self.cb_port.get() else ""
                if not pick:
                    messagebox.showwarning("提示", "先选择串口（或勾选演示模式）")
                    return
                self.port = pick
            self.start_worker()

    def start_worker(self):
        self.worker = Worker(self)
        self.worker.start()
        self.btn_conn["text"] = "断 开"
        self.btn_conn.config(bg=CARD2, fg=TXT, activebackground=CARD)
        self.log("已启动" + ("（演示模式）" if self.demo_mode else f" @ {self.port}"))

    def stop_worker(self):
        if self.worker:
            self.worker.stop_flag.set()
            self.worker.join(timeout=2)
            self.worker = None
        self.btn_conn["text"] = "连 接"
        self.btn_conn.config(bg=GREEN, fg="#0b0e12", activebackground="#27ae60")
        self.set_online(False)

    def on_coil_click(self, idx: int):
        if not self.worker:
            messagebox.showinfo("提示", "先连接（或开演示模式）")
            return
        cur = self.last_snap is not None and self.last_snap.coils[idx]
        self.tx.put((idx, not cur))

    def on_close(self):
        self.stop_worker()
        self.root.destroy()

    # ---------------------------------------------------------------- 数据泵
    def drain(self):
        if self.worker and not self.worker.is_alive():
            self.worker = None
            self.btn_conn["text"] = "连 接"
            self.btn_conn.config(bg=GREEN, fg="#0b0e12", activebackground="#27ae60")
            self.set_online(False)
        try:
            while True:
                self.apply_snapshot(self.rx.get_nowait())
        except queue.Empty:
            pass
        self.root.after(200, self.drain)

    def apply_snapshot(self, s: Snapshot):
        if s.err_text and not s.ok:
            self.err_count += 1
            self.set_online(False)
            self.lbl_err.config(text=f"错误 {self.err_count}: {s.err_text}")
            if self.err_count == 1 or self.err_count % 10 == 0:
                self.log(f"通信失败({self.err_count}): {s.err_text}")
            return
        if not s.demo and s.err_text == "" and s.regs == [0] * 24:
            self.set_online(True)
            return
        self.set_online(True)
        self.lbl_err.config(text=f"错误 {self.err_count}" if self.err_count else "")
        regs, coils = s.regs, s.coils
        self.last_snap = s

        # --- 单体电压瓦片（所有控件“变化才改”，避免每秒重绘抖动）---
        vals = [regs[2 + i] if regs[1] & (1 << i) else None for i in range(15)]
        live = [v for v in vals if v is not None]
        vmin, vmax = (min(live), max(live)) if live else (0, 0)
        bal = regs[22]
        for i, t in enumerate(self.tiles):
            bar = t["bar"]
            if vals[i] is None:
                self._set(("val", i), t["val"], text="未接", fg=GREY)
                self._set(("bal", i), t["bal"], text="")
                self._set(("delta", i), t["delta"], text="")
                self._set(("edge", i), t["frame"], highlightbackground=BORDER)
                self._canvas_state(("bar", i), bar, 0.0, "#262c38")
                continue
            v = vals[i]
            self._set(("val", i), t["val"], text=str(v))
            self._set(("delta", i), t["delta"], text=f"Δ{v - vmin:+d}")
            edge, fg, fill = BORDER, TXT, "#3d4657"
            if bal & (1 << i):
                self._set(("bal", i), t["bal"], text="⚡")
                edge, fill = AMBER, AMBER
            else:
                self._set(("bal", i), t["bal"], text="")
                if vmax > vmin:
                    if v == vmax:
                        edge, fg, fill = RED, RED, RED
                    elif v == vmin:
                        edge, fg, fill = BLUE, BLUE, BLUE
            self._set(("valfg", i), t["val"], fg=fg)
            self._set(("edge", i), t["frame"], highlightbackground=edge)
            self._canvas_state(("bar", i), bar,
                               max(0.0, min(1.0, (v - 3300) / 900)), fill)

        # --- 摘要 ---
        self._setvar(self.var_total, f"{regs[17] / 1000:.2f} V")
        self._setvar(self.var_temp, f"{to_signed(regs[20]) / 10:.1f}")
        self._setvar(self.var_cur, f"{to_signed(regs[18]):+d}")
        spread = vmax - vmin
        self._setvar(self.var_spread, f"{spread}" + ("  ⚠" if spread >= 30 else ""))
        bal_cells = [CELL_NAMES[i] for i in range(15) if bal & (1 << i)]
        self._setvar(self.var_balnow, "、".join(bal_cells) if bal_cells else "无")

        if regs[19] == 0xFFFF:
            self._setvar(self.var_soc, "未锚定")
            self._soc_state(-1, GREEN)
        else:
            self._setvar(self.var_soc, f"{regs[19] / 10:.1f} %")
            ratio = regs[19] / 1000
            color = GREEN if ratio > 0.2 else (AMBER if ratio > 0.1 else RED)
            self._soc_state(ratio, color)

        # --- 故障（边沿打印）---
        if regs[21] != self.last_fault:
            if regs[21] == 0:
                self.lbl_fault.config(text="✔ 系统正常", fg=GREEN, bg="#12291c")
                if self.last_fault > 0:
                    self.log("故障全部解除")
            else:
                names = decode_faults(regs[21])
                short = [FAULT_SHORT[b] for b, _ in FAULT_BITS if regs[21] & b]
                self.lbl_fault.config(text="✘ " + " ".join(short) +
                                      f"  (0x{regs[21]:02X})", fg="#fff", bg="#5a1f21")
                if 0 <= self.last_fault != regs[21]:
                    self.log(f"故障出现: {'、'.join(names)} (0x{regs[21]:02X})")
            self.last_fault = regs[21]

        # --- 控制卡片 ---
        chg_on, dsg_on = bool(regs[23] & 1), bool(regs[23] & 2)
        actuals = [f"实际充电管：{'✔ 开' if chg_on else '✘ 关'}",
                   f"实际放电管：{'✔ 开' if dsg_on else '✘ 关'}",
                   "实际均衡：" + ("、".join(bal_cells) if bal_cells else "无")]
        for i, c in enumerate(self.coil_cards):
            c["switch"].set(coils[i])
            if coils[i]:
                self._set(("ctrl_r", i), c["state"], text="● 允许中", fg=GREEN)
                self._set(("ctrl_e", i), c["frame"], highlightbackground="#22402e")
            else:
                self._set(("ctrl_r", i), c["state"], text="○ 已禁止", fg=GREY)
                self._set(("ctrl_e", i), c["frame"], highlightbackground=BORDER)
            mismatch = (i < 2 and coils[i] != (chg_on if i == 0 else dsg_on)) or (i == 2 and coils[i] and not bal_cells)
            self._set(("ctrl_a", i), c["actual"], text=actuals[i],
                      fg=AMBER if mismatch else SUB)

        # --- 均衡集合变化（边沿打印）---
        if bal != self.last_bal:
            if self.last_bal >= 0:
                on = [CELL_NAMES[i] for i in range(15) if bal & (1 << i)]
                off = [CELL_NAMES[i] for i in range(15)
                       if self.last_bal & (1 << i) and not bal & (1 << i)]
                msg = []
                if on:
                    msg.append("开始均衡 " + "、".join(on))
                if off:
                    msg.append("停止均衡 " + "、".join(off))
                if msg:
                    self.log("；".join(msg))
            self.last_bal = bal

    # ---------- 增量绘制辅助：状态没变就不碰控件 ----------
    def _set(self, key, widget, **kw):
        sig = tuple(sorted(kw.items()))
        if self._ui_cache.get(key) == sig:
            return
        self._ui_cache[key] = sig
        widget.config(**kw)

    @staticmethod
    def _setvar(var, text):
        if var.get() != text:
            var.set(text)

    def _canvas_state(self, key, cv, ratio, fill):
        """仅当宽度/比例/颜色变化时才真正重画（瓦片迷你条）。"""
        w = cv.winfo_width()
        if w < 20:
            w = self._bar_w.get(key, 70)
        self._bar_w[key] = w
        sig = (w, round(ratio, 3), fill)
        if self._ui_cache.get(key) == sig:
            return
        self._ui_cache[key] = sig
        cv.delete("all")
        h = int(cv.cget("height")) or 6
        round_rect(cv, 0, 0, w, h, h / 2, fill="#262c38")
        if ratio > 0.02:
            round_rect(cv, 0, 0, max(h, w * ratio), h, h / 2, fill=fill)

    def _soc_state(self, ratio, color):
        """SOC 圆角进度条：状态不变不重画。"""
        c = self.soc_bar
        w = c.winfo_width()
        if w < 40:
            w = self._bar_w.get(("socbar",), 300)
        self._bar_w[("socbar",)] = w
        sig = (w, round(ratio, 3), color)
        if self._ui_cache.get(("socbar",)) == sig:
            return
        self._ui_cache[("socbar",)] = sig
        c.delete("all")
        round_rect(c, 0, 1, w, 11, 5, fill="#262c38")
        if ratio >= 0:
            round_rect(c, 0, 1, max(12, w * ratio), 11, 5, fill=color)

    def set_online(self, on: bool):
        if on != self.online:
            self.online = on
            self._draw_pill(on)
            if not on and self.worker is None:
                self.err_count = 0
                self.lbl_err.config(text="")

    def log(self, msg: str):
        self.txt_log.config(state="normal")
        self.txt_log.insert("end", time.strftime("%H:%M:%S ") + msg + "\n")
        self.txt_log.see("end")
        self.txt_log.config(state="disabled")


def main():
    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == "__main__":
    try:
        main()
    except Exception:
        traceback.print_exc()
        input("回车退出...")