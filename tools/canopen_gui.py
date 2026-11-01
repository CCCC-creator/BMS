#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BMS 上位机 · CANopen 从站监控/控制
==================================
配套从站：BMS-FreeRTOS canopen 分支（CANopenNode V4，节点 1，500 kbit/s）

报文：
  0x181 TPDO1  250ms  总压 mV / 电芯数 / 电芯位图 / 电流 mA / SYS_STAT
  0x281 TPDO2  250ms  SOC 0.1% / 温度 0.1℃ / 故障位图 / 均衡位图 / MOS
  0x701 心跳    500ms  NMT 状态（05=Operational  7F=Pre-operational）
  0x081 EMCY   事件
  0x201 RPDO1  控制字 1 字节（bit0 充电 bit1 放电 bit2 均衡），建议 ≤1000ms
  0x601/0x581  SDO 轮询单体电压（0x2000 子索引 1~15）

依赖：python-can（pip install python-can）。tkinter 为 Python 自带。
运行：py -3 tools/canopen_gui.py            图形界面
      py -3 tools/canopen_gui.py --probe 5  无界面自检
"""

import argparse
import queue
import threading
import time
import tkinter as tk

try:
    import can
except ImportError:
    raise SystemExit("缺少 python-can：py -3 -m pip install python-can")

# ----------------------------------------------------------------------------
# 协议常量
# ----------------------------------------------------------------------------
NODE_ID = 1
COB_TPDO1 = 0x180 + NODE_ID
COB_TPDO2 = 0x280 + NODE_ID
COB_HB = 0x700 + NODE_ID
COB_EMCY = 0x080 + NODE_ID
COB_RPDO1 = 0x200 + NODE_ID
COB_SDO_TX = 0x600 + NODE_ID
COB_SDO_RX = 0x580 + NODE_ID

CTRL_CHG, CTRL_DSG, CTRL_BAL = 0x01, 0x02, 0x04
CTRL_PERIOD_MS = 100
SDO_GAP_S = 0.10      # 每 100ms 发一条 SDO（一轮约 1.5s），避免打满从机接收 FIFO

FAULT_BITS = [
    (0x01, "过压"), (0x02, "欠压"), (0x04, "充电过流"), (0x08, "放电过流"),
    (0x10, "短路"), (0x20, "过温"), (0x40, "低温"),
]
FAULT_NAME = {m: n for m, n in FAULT_BITS}

SYS_BITS = [
    (0x80, "CC_READY"), (0x20, "XREADY"), (0x10, "ALERT"),
    (0x08, "UV"), (0x04, "OV"), (0x02, "SCD"), (0x01, "OCD"),
]
SYS_NAME = {m: n for m, n in SYS_BITS}

EMCY_TEXT = {
    0x8250: "RPDO 超时（控制字断流）→ 从机降为 Pre-operational",
    0x8220: "RPDO 长度超出映射（控制字 DLC 应为 1）",
    0x8210: "PDO 长度不足",
    0x8110: "CAN 接收溢出（总线太挤：另一个主站也在轮询 SDO？）",
    0x8120: "CAN 进入错误被动（错误计数偏高，检查接线/终端电阻/重复 ID）",
    0x8130: "CAN 发送错误（总线无 ACK / 掉线）",
    0x8140: "CAN bus-off（节点已脱离总线）",
    0x3300: "电压类故障",
    0x2300: "电流类故障",
    0x4000: "温度类故障",
    0x0000: "故障恢复",
}
EMCY_CLASS = {0x17: "通信", 0x04: "长度", 0x40: "电压", 0x41: "电流", 0x42: "温度"}
NMT_STATE = {0: "初始化", 4: "Stopped", 5: "Operational", 127: "Pre-operational"}

# ----------------------------------------------------------------------------
# 配色 / 字体（与 Modbus 上位机保持一致）
# ----------------------------------------------------------------------------
PAGE = "#0e1014"
PANEL = "#15181e"
CARD = "#1b1f27"
CARD2 = "#222834"
BORDER = "#252a34"
TXT = "#e8ebf1"
SUB = "#8b93a3"
GREY = "#5a616e"
GREEN = "#2ecc71"
RED = "#ef5350"
AMBER = "#f5b942"
BLUE = "#5aa9f0"
BAL_TILE = "#33290f"      # 正在均衡的格子底色（暗琥珀）

FONT_UI = ("Microsoft YaHei UI", 9)
FONT_SM = ("Microsoft YaHei UI", 8)
FONT_NAME = ("Consolas", 8)
FONT_VAL = ("Consolas", 13, "bold")
FONT_BIG = ("Consolas", 17, "bold")
FONT_LOGO = ("Microsoft YaHei UI", 13, "bold")
FONT_SEC = ("Microsoft YaHei UI", 8, "bold")
FONT_BTN = ("Microsoft YaHei UI", 10, "bold")

S = 1.0          # 布局 DPI 缩放（144dpi → 1.5）


def P(v):
    return int(round(v * S))


def round_rect(cv, x1, y1, x2, y2, r, **kw):
    """Canvas 圆角矩形（smooth 折线近似）"""
    pts = [x1 + r, y1, x2 - r, y1, x2, y1, x2, y1 + r,
           x2, y2 - r, x2, y2, x2 - r, y2, x1 + r, y2,
           x1, y2, x1, y2 - r, x1, y1 + r, x1, y1]
    return cv.create_polygon(pts, smooth=True, **kw)


def panel(parent, title=None):
    f = tk.Frame(parent, bg=PANEL, highlightthickness=1, highlightbackground=BORDER)
    if title is not None:
        tk.Label(f, text=title, bg=PANEL, fg=SUB, font=FONT_SEC).pack(
            anchor="w", padx=P(12), pady=(P(8), P(2)))
    return f


def dark_titlebar(root):
    """Win10 20H1+ / Win11 深色标题栏"""
    try:
        import ctypes
        root.update_idletasks()
        hwnd = ctypes.windll.user32.GetParent(root.winfo_id())
        value = ctypes.c_int(1)
        ctypes.windll.dwmapi.DwmSetWindowAttribute(
            hwnd, 20, ctypes.byref(value), ctypes.sizeof(value))
    except Exception:                                           # noqa: BLE001
        pass


class Pill(tk.Canvas):
    """圆角状态标签"""

    def __init__(self, parent, text, fg=SUB, bg=CARD, width=None):
        w = width or P(104)
        super().__init__(parent, width=w, height=P(24), bg=parent["bg"],
                         highlightthickness=0)
        self._pw = w          # 注意：不能用 self._w，tkinter 内部占用了这个名字
        self.set(text, fg, bg)

    def set(self, text, fg=SUB, bg=CARD):
        self.delete("all")
        h = P(24)
        round_rect(self, 1, 1, self._pw - 1, h - 1, (h - 2) / 2, fill=bg, outline=BORDER)
        self.create_text(self._pw / 2, h / 2, text=text, fill=fg,
                         font=("Microsoft YaHei UI", 8, "bold"))


class ToggleSwitch(tk.Canvas):
    """拨动开关"""

    def __init__(self, parent, on, command):
        self.W, self.H = P(46), P(24)
        super().__init__(parent, width=self.W, height=self.H, bg=CARD,
                         highlightthickness=0, cursor="hand2")
        self.on = on
        self.command = command
        self.bind("<Button-1>", lambda e: self.command())
        self.draw()

    def draw(self):
        self.delete("all")
        track = GREEN if self.on else "#3a4150"
        round_rect(self, P(2), P(3), self.W - P(2), self.H - P(3),
                   (self.H - P(6)) / 2, fill=track)
        cx = self.W - P(14) if self.on else P(14)
        self.create_oval(cx - P(8), P(4), cx + P(8), self.H - P(4),
                         fill="#f4f6fa", width=0)

    def set(self, on):
        if on != self.on:
            self.on = on
            self.draw()


# ================================================================ 通信层
class Link:
    """CAN 收发与解析。总线访问只在一个线程里，UI 只读快照。"""

    def __init__(self, log):
        self.log = log
        self.bus = None
        self.running = False
        self.thread = None
        self.lock = threading.Lock()
        self.state = {
            "conn": False, "node": None,
            "total_mv": None, "ncell": None, "cellmap": None, "current_ma": None,
            "sysstat": None, "soc": None, "temp": None, "fault": None,
            "balmask": None, "mos": None,
            "cells": [None] * 15, "cells_ts": 0.0,
            "ts": {}, "rx": 0, "tx": 0, "rx201": 0, "tx201": 0,
            "emcy": [],
        }
        self.ctrl_enable = False
        self.ctrl_word = 0
        self._ctrl_t = 0.0
        self._sdo_t = 0.0
        self._sdo_sub = 1

    # -------------------------------------------------- 连接
    def open(self):
        try:
            self.bus = can.Bus(interface="pcan", channel="PCAN_USBBUS1", bitrate=500000)
        except Exception as e:                                  # noqa: BLE001
            self.log(f"打开 PCAN 失败：{e}", "err")
            return False
        self.running = True
        self.thread = threading.Thread(target=self._loop, daemon=True)
        self.thread.start()
        with self.lock:
            self.state["conn"] = True
        self.log("PCAN 已连接，500 kbit/s", "ok")
        return True

    def close(self):
        self.running = False
        if self.thread:
            self.thread.join(timeout=1.0)
        if self.bus:
            try:
                self.bus.shutdown()
            except Exception:                                   # noqa: BLE001
                pass
            self.bus = None
        with self.lock:
            self.state["conn"] = False
        self.log("已断开", "muted")

    # -------------------------------------------------- 发送
    def _send(self, cob, data):
        if not self.bus:
            return
        try:
            self.bus.send(can.Message(arbitration_id=cob, data=data, is_extended_id=False))
            with self.lock:
                self.state["tx"] += 1
                if cob == COB_RPDO1:
                    self.state["tx201"] += 1
        except Exception as e:                                  # noqa: BLE001
            self.log(f"发送失败：{e}", "err")

    def nmt_reset(self):
        self._send(0x000, [0x81, NODE_ID])
        self.log("已发送 NMT Reset Communication（0x000: 81 01）", "info")

    def nmt_start(self):
        self._send(0x000, [0x01, NODE_ID])
        self.log("已发送 NMT Start（0x000: 01 01）", "info")

    def set_control(self, word, enable):
        self.ctrl_word = word & 0x07
        self.ctrl_enable = enable

    def sdo_read_cell(self, sub):
        self._send(COB_SDO_TX, bytes([0x40, 0x00, 0x20, sub, 0, 0, 0, 0]))

    # -------------------------------------------------- 主循环
    def _loop(self):
        while self.running:
            try:
                msg = self.bus.recv(timeout=0.02)
            except Exception as e:                              # noqa: BLE001
                self.log(f"接收异常：{e}", "err")
                break
            if msg is not None:
                self._handle(msg)

            now = time.time()
            if self.ctrl_enable and now - self._ctrl_t > CTRL_PERIOD_MS / 1000.0:
                self._ctrl_t = now
                self._send(COB_RPDO1, bytes([self.ctrl_word]))
            if now - self._sdo_t > SDO_GAP_S and self.state["node"] == 5:
                self._sdo_t = now
                self.sdo_read_cell(self._sdo_sub)
                self._sdo_sub = self._sdo_sub % 15 + 1

    def _handle(self, msg):
        cid, d = msg.arbitration_id, bytes(msg.data)
        now = time.time()
        with self.lock:
            st = self.state
            st["rx"] += 1
            st["ts"][cid] = now

            if cid == COB_TPDO1 and len(d) >= 8:
                st["total_mv"] = d[0] | (d[1] << 8)
                st["ncell"] = d[2]
                st["cellmap"] = d[3] | (d[4] << 8)
                cur = d[5] | (d[6] << 8)
                st["current_ma"] = cur - 0x10000 if cur & 0x8000 else cur
                st["sysstat"] = d[7]

            elif cid == COB_TPDO2 and len(d) >= 8:
                st["soc"] = d[0] | (d[1] << 8)
                t = d[2] | (d[3] << 8)
                st["temp"] = t - 0x10000 if t & 0x8000 else t
                st["fault"] = d[4]
                st["balmask"] = d[5] | (d[6] << 8)
                st["mos"] = d[7]

            elif cid == COB_HB and len(d) >= 1:
                st["node"] = d[0]

            elif cid == COB_RPDO1:
                st["rx201"] += 1          # 总线上的控制字（别人发的也算）

            elif cid == COB_EMCY and len(d) >= 8:
                code = d[0] | (d[1] << 8)        # CiA 301：错误码小端
                reg, cls = d[2], d[3]
                info = d[4] | (d[5] << 8) | (d[6] << 16) | (d[7] << 24)
                text = EMCY_TEXT.get(code, "未定义错误码")
                extra = ""
                if code in (0x3300, 0x2300, 0x4000) and info:
                    extra = f"  位图=0x{info:02X}({self._fault_text(info)})"
                elif code == 0x8250:
                    extra = "  类别=%s" % EMCY_CLASS.get(cls, hex(cls))
                st["emcy"].append((now, time.strftime("%H:%M:%S"),
                                   f"0x{code:04X} {text}{extra}  寄存器=0x{reg:02X}",
                                   "reset" if code == 0 else "emcy"))
                st["emcy"] = st["emcy"][-40:]

            elif cid == COB_SDO_RX and len(d) >= 8:
                if d[0] == 0x4B and d[1] == 0x00 and d[2] == 0x20:
                    st["cells"][d[3] - 1] = d[4] | (d[5] << 8)
                    st["cells_ts"] = now

    @staticmethod
    def _fault_text(bits):
        return "|".join(n for m, n in FAULT_BITS if bits & m) or "无"

    def snapshot(self):
        with self.lock:
            st = dict(self.state)
            st["cells"] = list(self.state["cells"])
            st["ts"] = dict(self.state["ts"])
            st["emcy"] = list(self.state["emcy"])
            return st


# ================================================================ 界面
class App:
    def __init__(self, root):
        self.root = root
        self.q = queue.Queue()
        self.link = Link(self.log_threadsafe)
        self._emcy_seen = 0
        self._rx201_seen = 0
        self._foreign_warn = False
        self._conn_t = 0.0          # 本次连接建立的时刻
        self._conn_prev = False

        root.title("BMS 上位机 · CANopen (500 kbit/s)")
        root.configure(bg=PAGE)
        root.geometry(f"{P(1250)}x{P(800)}")
        root.minsize(P(1120), P(720))
        dark_titlebar(root)
        self._build()
        self._center()
        root.protocol("WM_DELETE_WINDOW", self.on_close)
        root.after(50, self._tick)

    def _center(self):
        try:
            self.root.update_idletasks()
            w = P(1250)
            h = max(P(700), self.root.winfo_reqheight())   # 按内容实际高度，避免大片空白
            x = (self.root.winfo_screenwidth() - w) // 2
            y = (self.root.winfo_screenheight() - h) // 2 - P(20)
            self.root.geometry(f"{w}x{h}+{x}+{y}")
        except Exception:                                       # noqa: BLE001
            pass

    # -------------------------------------------------- 日志
    def log_threadsafe(self, text, level="muted"):
        self.q.put((text, level))

    def _drain_log(self):
        while True:
            try:
                text, level = self.q.get_nowait()
            except queue.Empty:
                return
            self._append_log(text, level)

    def _append_log(self, text, level="muted"):
        self.log_txt.configure(state="normal")
        self.log_txt.insert("end", time.strftime("%H:%M:%S  "), "ts")
        self.log_txt.insert("end", text + "\n", level)
        if int(self.log_txt.index("end-1c").split(".")[0]) > 200:
            self.log_txt.delete("1.0", "50.0")
        self.log_txt.configure(state="disabled")
        self.log_txt.see("end")

    # -------------------------------------------------- 布局
    def _build(self):
        tk.Frame(self.root, bg=GREEN, height=P(3)).pack(fill="x")   # 顶部强调线
        self._build_topbar()

        body = tk.Frame(self.root, bg=PAGE)
        body.pack(fill="both", expand=True, padx=P(12), pady=(P(8), 0))
        body.columnconfigure(0, weight=3, uniform="col")
        body.columnconfigure(1, weight=5, uniform="col")
        body.columnconfigure(2, weight=3, uniform="col")
        body.rowconfigure(0, weight=1)

        left = tk.Frame(body, bg=PAGE)
        left.grid(row=0, column=0, sticky="nsew")
        center = tk.Frame(body, bg=PAGE)
        center.grid(row=0, column=1, sticky="nsew", padx=P(10))
        right = tk.Frame(body, bg=PAGE)
        right.grid(row=0, column=2, sticky="nsew")

        self._build_metrics(left)
        self._build_switch(left)
        self._build_fault(left)
        self._build_cells(center)
        self._build_emcy(center)
        self._build_control(right)
        self._build_sysstat(right)
        self._build_log(self.root)

    def _build_topbar(self):
        top = tk.Frame(self.root, bg=PAGE)
        top.pack(fill="x", padx=P(12), pady=(P(10), P(4)))

        left = tk.Frame(top, bg=PAGE)
        left.pack(side="left")
        tk.Label(left, text="● BMS 上位机", bg=PAGE, fg=GREEN,
                 font=FONT_LOGO).pack(side="left")
        tk.Label(left, text="CANopen · 500 kbit/s · 节点 1", bg=PAGE, fg=SUB,
                 font=FONT_SM).pack(side="left", padx=(P(10), 0), pady=(P(5), 0))

        right = tk.Frame(top, bg=PAGE)
        right.pack(side="right")
        self.pill_link = Pill(right, "离线", SUB, CARD, P(84))
        self.pill_link.pack(side="right", padx=(P(8), 0))
        self.pill_nmt = Pill(right, "NMT --", SUB, CARD, P(160))
        self.pill_nmt.pack(side="right", padx=(P(8), 0))
        self.warn_slot = tk.Frame(right, bg=PAGE)
        self.warn_slot.pack(side="right", padx=(P(6), 0))
        self.pill_warn = Pill(self.warn_slot, "外部主站占用 0x201", AMBER, CARD, P(176))

        self.btn_nmt = tk.Button(right, text="NMT Start", relief="flat",
                                 command=self._nmt_start, bg=CARD, fg=TXT,
                                 activebackground=CARD2, activeforeground=TXT,
                                 font=FONT_UI, padx=P(10), pady=P(4), cursor="hand2")
        self.btn_nmt.pack(side="right", padx=P(6))
        self.btn_reset = tk.Button(right, text="Reset Comm", relief="flat",
                                   command=self._nmt_reset, bg=CARD, fg=TXT,
                                   activebackground=CARD2, activeforeground=TXT,
                                   font=FONT_UI, padx=P(10), pady=P(4), cursor="hand2")
        self.btn_reset.pack(side="right", padx=P(6))
        self.btn_conn = tk.Button(right, text="连 接", width=8, relief="flat",
                                  command=self._toggle_conn, bg=GREEN, fg="#0b0e12",
                                  activebackground="#27ae60",
                                  font=("Microsoft YaHei UI", 9, "bold"))
        self.btn_conn.pack(side="right", padx=P(6))

    # -------------------------------------------------- 左列
    def _build_metrics(self, parent):
        p = panel(parent, "运行指标")
        p.pack(fill="x", pady=(0, P(10)))
        self.lb_stall = tk.Label(p, text="", font=FONT_SM, fg=AMBER, bg=PANEL)
        self.lb_stall.place(relx=1.0, x=-P(12), y=P(7), anchor="ne")
        grid = tk.Frame(p, bg=PANEL)
        grid.pack(fill="both", expand=True, padx=P(8), pady=(0, P(8)))
        for c in (0, 1):
            grid.columnconfigure(c, weight=1)
        self.m_total = self._metric(grid, 0, 0, "总压", "V", TXT)
        self.m_current = self._metric(grid, 0, 1, "电流", "mA", BLUE)
        self.m_soc = self._metric(grid, 1, 0, "SOC", "%", GREEN)
        self.m_temp = self._metric(grid, 1, 1, "温度", "℃", AMBER)

    def _metric(self, parent, r, c, title, unit, color):
        f = tk.Frame(parent, bg=PANEL)
        f.grid(row=r, column=c, sticky="nsew", padx=P(6), pady=P(3))
        tk.Label(f, text=title, font=FONT_SM, fg=SUB, bg=PANEL, anchor="w").pack(anchor="w")
        line = tk.Frame(f, bg=PANEL)
        line.pack(anchor="w")
        val = tk.Label(line, text="--", font=FONT_BIG, fg=color, bg=PANEL, width=5, anchor="e")
        val.pack(side="left")
        tk.Label(line, text=unit, font=FONT_SM, fg=SUB, bg=PANEL).pack(
            side="left", padx=(P(4), 0), pady=(P(6), 0))
        return val

    def _build_switch(self, parent):
        p = panel(parent, "开关与均衡")
        p.pack(fill="x", pady=(0, P(10)))
        row = tk.Frame(p, bg=PANEL)
        row.pack(anchor="w", padx=P(12), pady=(P(2), P(4)))
        self.pill_chg = Pill(row, "充电管 --", SUB, CARD, P(112))
        self.pill_chg.pack(side="left", padx=(0, P(6)))
        self.pill_dsg = Pill(row, "放电管 --", SUB, CARD, P(112))
        self.pill_dsg.pack(side="left")
        row2 = tk.Frame(p, bg=PANEL)
        row2.pack(anchor="w", padx=P(12), pady=(0, P(10)))
        self.pill_bal = Pill(row2, "均衡 关闭", SUB, CARD, P(232))
        self.pill_bal.pack(side="left")

    def _build_fault(self, parent):
        p = panel(parent, "故障位图")
        p.pack(fill="both", expand=True)
        grid = tk.Frame(p, bg=PANEL)
        grid.pack(anchor="w", padx=P(10), pady=(P(2), P(8)))
        self.fault_pills = {}
        for i, (mask, name) in enumerate(FAULT_BITS):
            lb = Pill(grid, name, GREY, CARD, P(104))
            lb.grid(row=i // 2, column=i % 2, padx=P(2), pady=P(2), sticky="w")
            self.fault_pills[mask] = lb

    # -------------------------------------------------- 中列
    def _build_cells(self, parent):
        p = panel(parent, "单体电压 · mV")
        p.pack(fill="x", pady=(0, P(10)))
        head = tk.Frame(p, bg=PANEL)
        head.pack(fill="x", padx=P(12), pady=(0, P(4)))
        self.lb_cellstat = tk.Label(head, text="等待数据", font=FONT_SM, fg=SUB,
                                    bg=PANEL, width=28, anchor="e")
        self.lb_cellstat.pack(side="right")

        grid = tk.Frame(p, bg=PANEL)
        grid.pack(fill="both", expand=True, padx=P(8), pady=(0, P(10)))
        self.cell_tiles = []
        for i in range(15):
            r, c = divmod(i, 5)
            t = tk.Frame(grid, bg=CARD, highlightthickness=1, highlightbackground=BORDER)
            t.grid(row=r, column=c, padx=P(3), pady=P(3), sticky="nsew")
            bar = tk.Frame(t, width=P(4), bg=CARD)          # 均衡时点亮成琥珀色
            bar.pack(side="left", fill="y")
            # 竖排"均衡"两个字：固定宽度，开关时不影响布局
            vert = tk.Label(t, text="", font=("Microsoft YaHei UI", 8, "bold"),
                            fg="#0b0e12", bg=CARD, width=2, justify="center")
            vert.pack(side="left", fill="y")
            body = tk.Frame(t, bg=CARD)
            body.pack(side="left", fill="both", expand=True)
            hdr = tk.Frame(body, bg=CARD, height=P(16))
            hdr.pack(fill="x", padx=P(8), pady=(P(4), 0))
            hdr.pack_propagate(False)
            lb_no = tk.Label(hdr, text=f"{i + 1:02d}", font=FONT_NAME, fg=SUB, bg=CARD, anchor="w")
            lb_no.pack(side="left")
            # 固定尺寸的图标槽：字号/内容变化都不影响布局
            lb_bal = tk.Label(hdr, text="", font=("Segoe UI Symbol", 11), fg=AMBER, bg=CARD,
                              width=2, anchor="e")
            lb_bal.pack(side="right")
            lb_v = tk.Label(body, text="--", font=FONT_VAL, fg=TXT, bg=CARD, anchor="w",
                            width=6, height=1)
            lb_v.pack(anchor="w", padx=P(8), pady=(0, P(4)))
            self.cell_tiles.append((t, lb_no, lb_v, lb_bal, bar, vert))
        for c in range(5):
            grid.columnconfigure(c, weight=1, uniform="cell")
        for r in range(3):
            grid.rowconfigure(r, weight=1, uniform="cellrow")

    def _update_cells(self, st, live=True):
        vals = [v for v in st["cells"] if v]
        vmin = min(vals) if vals else None
        vmax = max(vals) if vals else None
        bal = (st["balmask"] or 0) if live else 0
        for i, (tile, lb_no, lb_v, lb_bal, bar, vert) in enumerate(self.cell_tiles):
            installed = bool((st["cellmap"] or 0) & (1 << i))
            v = st["cells"][i]
            on_bal = installed and ((bal >> i) & 1) == 1

            # 只切换颜色与文字，字体/尺寸恒定 → 开关均衡时布局不动
            bar.configure(bg=AMBER if on_bal else CARD)
            vert.configure(text="均\n衡" if on_bal else "",
                           bg=AMBER if on_bal else CARD,
                           fg="#0b0e12" if on_bal else CARD)
            lb_bal.configure(text="⚡" if on_bal else "")
            lb_no.configure(fg=AMBER if on_bal else (GREY if not installed else SUB))

            if not installed:
                lb_v.configure(text="未安装", font=FONT_SM, fg=GREY)
                tile.configure(highlightbackground=BORDER)
                continue

            lb_v.configure(font=FONT_VAL)
            if not live or v is None:
                lb_v.configure(text="--", fg=SUB)
                tile.configure(highlightbackground=BORDER)
                continue

            lb_v.configure(text=str(v))
            if v >= 4250 or v <= 3000:
                lb_v.configure(fg=RED)
                tile.configure(highlightbackground=RED)
            elif v == vmin:
                lb_v.configure(fg=AMBER)
                tile.configure(highlightbackground=AMBER)
            elif v == vmax:
                lb_v.configure(fg=BLUE)
                tile.configure(highlightbackground=BLUE)
            else:
                lb_v.configure(fg=TXT)
                tile.configure(highlightbackground=AMBER if on_bal else BORDER)

        if vals and live:
            self.lb_cellstat.configure(text=f"min {vmin}   max {vmax}   差 {vmax - vmin} mV",
                                       fg=SUB)
        elif not live:
            self.lb_cellstat.configure(text="等待数据", fg=SUB)

    def _build_emcy(self, parent):
        p = panel(parent, "EMCY 事件")
        p.pack(fill="both", expand=True)
        self.emcy_txt = tk.Text(p, height=6, bg=PANEL, fg=TXT, bd=0,
                                highlightthickness=0, font=FONT_NAME, wrap="none",
                                state="disabled")
        self.emcy_txt.pack(fill="both", expand=True, padx=P(10), pady=(0, P(8)))
        for name, color in (("emcy", RED), ("reset", GREEN), ("ts", SUB), ("muted", SUB)):
            self.emcy_txt.tag_configure(name, foreground=color)
        self.emcy_txt.configure(state="normal")
        self.emcy_txt.insert("end", "暂无事件。超时、长度超限、电池故障都会在这里出现。\n", "muted")
        self.emcy_txt.configure(state="disabled")

    # -------------------------------------------------- 右列
    def _build_control(self, parent):
        p = panel(parent, "控制 · RPDO 0x201")
        p.pack(fill="x", pady=(0, P(10)))

        tk.Label(p, text="控制字 = 0x201 报文里的 1 个字节，三位各管一件事",
                 font=FONT_SM, fg=SUB, bg=PANEL).pack(anchor="w", padx=P(12), pady=(0, P(6)))

        self.v_chg = tk.BooleanVar(value=True)
        self.v_dsg = tk.BooleanVar(value=True)
        self.v_bal = tk.BooleanVar(value=False)
        self.sw = {}
        for key, text, bit, var in (
            ("chg", "充电允许", "bit0", self.v_chg),
            ("dsg", "放电允许", "bit1", self.v_dsg),
            ("bal", "均衡使能", "bit2", self.v_bal),
        ):
            row = tk.Frame(p, bg=CARD, highlightthickness=1, highlightbackground=BORDER)
            row.pack(fill="x", padx=P(12), pady=P(2))
            sw = ToggleSwitch(row, var.get(), lambda k=key: self._toggle_bit(k))
            sw.pack(side="right", padx=P(10), pady=P(6))
            tk.Label(row, text=bit, font=FONT_SM, fg=GREY, bg=CARD).pack(
                side="right", padx=(0, P(8)))
            tk.Label(row, text=text, font=FONT_UI, fg=TXT, bg=CARD, anchor="w").pack(
                side="left", padx=P(10), pady=P(8))
            self.sw[key] = sw

        tk.Frame(p, bg=BORDER, height=1).pack(fill="x", padx=P(12), pady=P(8))

        line = tk.Frame(p, bg=PANEL)
        line.pack(fill="x", padx=P(12))
        tk.Label(line, text="控制字", font=FONT_SM, fg=SUB, bg=PANEL).pack(side="left", pady=(P(2), 0))
        self.lb_word = tk.Label(line, text="0x07", font=("Consolas", 17, "bold"), fg=BLUE, bg=PANEL)
        self.lb_word.pack(side="left", padx=P(8))
        self.lb_bits = tk.Label(line, text="0b111", font=FONT_NAME, fg=SUB, bg=PANEL)
        self.lb_bits.pack(side="left")

        tk.Label(p, text="常用：0x07 全开 · 0x03 关均衡 · 0x00 全关",
                 font=FONT_SM, fg=SUB, bg=PANEL).pack(anchor="w", padx=P(12), pady=(P(4), P(8)))

        self.btn_send = tk.Button(p, text="开始下发", relief="flat", bg=BLUE, fg="#0b0e12",
                                  activebackground="#4a94d8", font=FONT_BTN,
                                  pady=P(8), cursor="hand2", command=self._toggle_send)
        self.btn_send.pack(fill="x", padx=P(12))
        self.lb_hint = tk.Label(p, text="", font=FONT_SM, fg=AMBER, bg=PANEL,
                                wraplength=P(280), justify="left", anchor="w")
        self.lb_hint.pack(anchor="w", fill="x", padx=P(12), pady=(P(6), 0))
        self.var_auto = tk.BooleanVar(value=True)
        tk.Checkbutton(p, text="自动恢复（检测到非 Operational 自动 Reset Comm + Start）",
                       variable=self.var_auto,
                       bg=PANEL, fg=SUB, activebackground=PANEL, activeforeground=TXT,
                       selectcolor=CARD, font=FONT_SM, bd=0, highlightthickness=0,
                       anchor="w").pack(anchor="w", padx=P(8), pady=(P(2), 0))
        self._recover_t = 0.0

        tk.Label(p, text="100 ms 周期；停发 1 s 后从机全关并降级",
                 font=FONT_SM, fg=SUB, bg=PANEL, wraplength=P(280),
                 justify="left").pack(anchor="w", padx=P(12), pady=(P(2), P(10)))

        self._refresh_ctrl()

    def _build_sysstat(self, parent):
        p = panel(parent, "芯片状态 · SYS_STAT")
        p.pack(fill="x")
        self.lb_sysstat = tk.Label(p, text="0x--", font=("Consolas", 16, "bold"),
                                   fg=TXT, bg=PANEL, anchor="w")
        self.lb_sysstat.pack(anchor="w", padx=P(12), pady=(P(2), P(4)))
        grid = tk.Frame(p, bg=PANEL)
        grid.pack(anchor="w", padx=P(10), pady=(0, P(10)))
        self.sys_pills = {}
        for i, (mask, name) in enumerate(SYS_BITS):
            lb = Pill(grid, name, GREY, CARD, P(104))
            lb.grid(row=i // 2, column=i % 2, padx=P(2), pady=P(2), sticky="w")
            self.sys_pills[mask] = lb

    # -------------------------------------------------- 日志区
    def _build_log(self, root):
        box = panel(root, "运行日志")
        box.pack(fill="x", padx=P(12), pady=(P(8), P(10)))
        self.lb_stat = tk.Label(box, text="RX 0   TX 0", font=FONT_SM, fg=SUB,
                                bg=PANEL, width=24, anchor="e")
        self.lb_stat.pack(side="right", padx=P(12), pady=(P(8), 0), anchor="n")
        self.log_txt = tk.Text(box, height=5, bg=PANEL, fg=TXT, bd=0,
                               highlightthickness=0, font=FONT_NAME, wrap="none",
                               state="disabled")
        self.log_txt.pack(fill="x", padx=P(8), pady=(0, P(8)))
        for name, color in (("ok", GREEN), ("err", RED), ("info", BLUE),
                            ("emcy", RED), ("reset", GREEN), ("muted", SUB), ("ts", GREY)):
            self.log_txt.tag_configure(name, foreground=color)

    # -------------------------------------------------- 交互
    def auto_connect(self):
        self._toggle_conn()

    def _toggle_conn(self):
        if self.link.state["conn"]:
            self.link.close()
            self.btn_conn.configure(text="连 接", bg=GREEN)
            self.pill_link.set("离线", SUB, CARD)
            self.pill_nmt.set("NMT --", SUB, CARD)
        else:
            if self.link.open():
                self.btn_conn.configure(text="断 开", bg=AMBER)

    def _nmt_reset(self):
        if self.link.state["conn"]:
            self.link.nmt_reset()
        else:
            self.log_threadsafe("先连接总线", "err")

    def _nmt_start(self):
        if self.link.state["conn"]:
            self.link.nmt_start()
        else:
            self.log_threadsafe("先连接总线", "err")

    def _toggle_bit(self, key):
        var = {"chg": self.v_chg, "dsg": self.v_dsg, "bal": self.v_bal}[key]
        var.set(not var.get())
        self._refresh_ctrl()

    def _ctrl_word(self):
        return ((CTRL_CHG if self.v_chg.get() else 0)
                | (CTRL_DSG if self.v_dsg.get() else 0)
                | (CTRL_BAL if self.v_bal.get() else 0))

    def _refresh_ctrl(self):
        word = self._ctrl_word()
        self.lb_word.configure(text=f"0x{word:02X}")
        self.lb_bits.configure(text=f"0b{word:03b}")
        for key, var in (("chg", self.v_chg), ("dsg", self.v_dsg), ("bal", self.v_bal)):
            self.sw[key].set(var.get())
        self.link.set_control(word, self.link.ctrl_enable)

    def _toggle_send(self):
        if not self.link.state["conn"]:
            self.log_threadsafe("先连接总线", "err")
            return
        node = self.link.state["node"]
        if not self.link.ctrl_enable and node != 5:
            self.log_threadsafe("从机不在 Operational，0x201 不会被接收 —— 先点 NMT Start"
                                "（若 Start 无效，先点 Reset Comm）", "err")
        on = not self.link.ctrl_enable
        self.link.set_control(self._ctrl_word(), on)
        self.btn_send.configure(text="停止下发" if on else "开始下发",
                                bg=RED if on else BLUE,
                                fg="#ffffff" if on else "#0b0e12")
        self.log_threadsafe("开始周期下发 0x201" if on else "停止下发 0x201", "info")

    def on_close(self):
        try:
            self.link.close()
        finally:
            self.root.destroy()

    # -------------------------------------------------- 周期刷新
    def _tick(self):
        self._drain_log()
        st = self.link.snapshot()
        now = time.time()

        if st["conn"] and not self._conn_prev:
            self._conn_t = now                     # 记录连接时刻
        self._conn_prev = st["conn"]
        if st["conn"]:
            self.pill_link.set("在线", GREEN, CARD)
            node = st["node"]
            if node is None:
                self.pill_nmt.set("NMT 等待心跳", SUB, CARD)
            else:
                name = NMT_STATE.get(node, hex(node))
                ok = node == 5
                self.pill_nmt.set(name, "#0b0e12" if ok else "#0b0e12",
                                  GREEN if ok else AMBER)
        else:
            self.pill_link.set("离线", SUB, CARD)
            self.pill_nmt.set("NMT --", SUB, CARD)

        # 外部主站占用 0x201 检测
        if st["rx201"] != self._rx201_seen:
            self._rx201_seen = st["rx201"]
            if not self._foreign_warn:
                self._foreign_warn = True
                self.pill_warn.pack()
                self.log_threadsafe("检测到外部主站正在发 0x201（PCAN-View？）——"
                                    "两边会互相覆盖控制字，请先停掉它", "err")
        elif st["conn"] and self._foreign_warn and now - st["ts"].get(COB_RPDO1, 0) > 3.0:
            self._foreign_warn = False
            self.pill_warn.pack_forget()

        # Operational 提示 + 数据暂停提示
        node = st["node"]
        if not st["conn"]:
            self.lb_hint.configure(text="未连接总线")
        elif node != 5:
            self.lb_hint.configure(
                text="⚠ 从机不在 Operational：控制字不生效、TPDO 也不发。\n"
                     "   先点 NMT Start；拉不回来就先 Reset Comm 再 Start。",
                fg=AMBER)
        elif self.link.ctrl_enable:
            self.lb_hint.configure(text="✓ Operational，控制字每 100 ms 下发中", fg=GREEN)
        else:
            self.lb_hint.configure(text="✓ Operational，可以开始下发", fg=SUB)

        # 可选：观察到非 Operational 超过 5 s 就自动 Reset Comm + Start
        if (self.var_auto.get() and st["conn"] and node != 5
                and now - self._recover_t > 5.0):
            self._recover_t = now
            self.link.nmt_reset()
            self.root.after(800, self.link.nmt_start)
            self.log_threadsafe("自动恢复：Reset Comm → NMT Start", "info")

        t1 = st["ts"].get(COB_TPDO1, 0)
        age = now - t1 if t1 else 999
        self.lb_stall.configure(text=f"数据暂停 {age:.1f}s" if (st["conn"] and age > 1.5) else "")

        # 数据新鲜度：TPDO 超过 1.5 s 没来就不显示旧值（否则看起来像"现在是关闭"）
        live1 = st["conn"] and (now - st["ts"].get(COB_TPDO1, 0) < 1.5) and st["total_mv"] is not None
        live2 = st["conn"] and (now - st["ts"].get(COB_TPDO2, 0) < 1.5) and st["mos"] is not None

        # 运行指标
        self.m_total.configure(text=f"{st['total_mv'] / 1000:.2f}" if live1 else "--")
        self.m_current.configure(text=f"{st['current_ma']:+d}" if live1 else "--")
        if live2 and st["soc"] is not None:
            self.m_soc.configure(text="--" if st["soc"] == 0xFFFF else f"{st['soc'] / 10:.1f}")
        else:
            self.m_soc.configure(text="--")
        self.m_temp.configure(text=f"{st['temp'] / 10:.1f}" if live2 else "--")

        # MOS / 均衡（无新鲜数据时显示 --，不显示上一帧的"断/关闭"）
        if live2:
            chg, dsg = bool(st["mos"] & 1), bool(st["mos"] & 2)
            self.pill_chg.set("充电管 通" if chg else "充电管 断",
                              "#0b0e12" if chg else "#c9ced8", GREEN if chg else CARD2)
            self.pill_dsg.set("放电管 通" if dsg else "放电管 断",
                              "#0b0e12" if dsg else "#c9ced8", GREEN if dsg else CARD2)
            n = bin(st["balmask"] or 0).count("1")
            self.pill_bal.set(f"均衡 {n} 节",
                              "#0b0e12" if n else "#c9ced8", AMBER if n else CARD2)
        else:
            self.pill_chg.set("充电管 --", GREY, CARD)
            self.pill_dsg.set("放电管 --", GREY, CARD)
            self.pill_bal.set("均衡 --（数据暂停）", GREY, CARD)

        # 故障位（旧数据不清空会误报"正在故障"）
        if live2:
            for mask, lb in self.fault_pills.items():
                on = bool(st["fault"] & mask)
                lb.set(FAULT_NAME[mask], "#0b0e12" if on else GREY, RED if on else CARD)
        else:
            for mask, lb in self.fault_pills.items():
                lb.set(FAULT_NAME[mask], GREY, CARD)

        # SYS_STAT
        ss = st["sysstat"]
        if ss is not None:
            self.lb_sysstat.configure(text=f"0x{ss:02X}")
            for mask, lb in self.sys_pills.items():
                on = bool(ss & mask)
                lb.set(SYS_NAME[mask], BLUE if on else GREY, CARD2 if on else CARD)

        # 单体电压（SDO 超过 3 s 没回应就显示 --）
        live_cells = st["conn"] and (now - st["cells_ts"] < 3.0)
        if st["cellmap"] is not None:
            self._update_cells(st, live_cells)

        # EMCY（连接后 1.5s 内的报错 = 板子独占总线期间攒的账单，折叠成一行）
        if len(st["emcy"]) != self._emcy_seen:
            self._emcy_seen = len(st["emcy"])
            backlog = [e for e in st["emcy"] if e[0] <= self._conn_t + 1.5]
            live_evs = [e for e in st["emcy"] if e[0] > self._conn_t + 1.5]
            self.emcy_txt.configure(state="normal")
            self.emcy_txt.delete("1.0", "end")
            if backlog:
                self.emcy_txt.insert("end",
                    f"连接瞬间积压错误 ×{len(backlog)}（板子独占总线期间的无 ACK 错误，已自愈）\n",
                    "muted")
            for _e, ts, text, level in live_evs[-12:]:
                self.emcy_txt.insert("end", f"{ts}  ", "ts")
                self.emcy_txt.insert("end", text + "\n", level)
            if not backlog and not live_evs:
                self.emcy_txt.insert("end", "暂无事件。\n", "muted")
            self.emcy_txt.configure(state="disabled")
            self.emcy_txt.see("end")

        self.lb_stat.configure(text=f"RX {st['rx']}   TX {st['tx']}")
        self.root.after(60, self._tick)


# ================================================================ 自检
def probe(seconds):
    link = Link(lambda t, lv="muted": print(f"[{lv}] {t}"))
    if not link.open():
        return 1
    end = time.time() + seconds
    while time.time() < end:
        time.sleep(0.5)
        st = link.snapshot()
        vals = [c for c in st["cells"] if c]
        print(f"总压={st['total_mv']}mV 电流={st['current_ma']}mA SOC={st['soc']} "
              f"温度={st['temp']} 故障=0x{(st['fault'] or 0):02X} MOS={st['mos']} "
              f"电芯{len(vals)}节 节点={st['node']} RX={st['rx']} TX={st['tx']}")
    link.close()
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--probe", type=float, default=0, help="无界面自检秒数")
    ap.add_argument("--auto", action="store_true", help="启动后自动连接总线")
    args = ap.parse_args()
    if args.probe:
        raise SystemExit(probe(args.probe))

    try:
        from ctypes import windll
        windll.shcore.SetProcessDpiAwareness(1)
    except Exception:                                           # noqa: BLE001
        pass

    root = tk.Tk()
    global S
    try:
        S = max(1.0, root.winfo_fpixels("1i") / 96.0)
    except Exception:                                           # noqa: BLE001
        S = 1.0

    app = App(root)
    if args.auto:
        root.after(300, app.auto_connect)
    root.mainloop()


if __name__ == "__main__":
    main()