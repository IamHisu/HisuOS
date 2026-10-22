"""Desktop USB chat for HisuOS. Opening the window never opens a serial port."""
from __future__ import annotations

import argparse
import codecs
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import queue
import re
import threading
import time
import tkinter as tk
from tkinter import messagebox, ttk
from dataclasses import dataclass
from datetime import datetime

import serial
from serial.tools import list_ports

MAX_INPUT = 255
MAX_LINE = 8192
RESYNC = b"\x15\n"  # Firmware rejects the entire stale line; this cannot submit a partial command.
ANSI = re.compile(r"\x1b\[[0-?]*[ -/]*[@-~]")
TAG = re.compile(r"^[IWE] \(\d+\) ([A-Za-z_]+): ?(.*)$")
REPLY = re.compile(r"^\[(\d+):(\d+)\] (.*)$")
MIC_LEASE_SECONDS = 2
VOICE_STATES = frozenset(("idle", "waiting", "capturing", "validating", "processing", "speaking"))


@dataclass(frozen=True)
class Event:
    kind: str
    text: str = ""
    turn: tuple[int, int] | None = None


def encode_message(text: str) -> bytes:
    if not text.strip() or any(ord(c) < 32 or ord(c) == 127 for c in text):
        raise ValueError("Tin nhắn phải là một dòng, không chứa ký tự điều khiển.")
    try:
        data = text.encode("utf-8")
    except UnicodeError as exc:
        raise ValueError("Tin nhắn không phải UTF-8 hợp lệ.") from exc
    if len(data) > MAX_INPUT:
        raise ValueError("Tin nhắn dài quá 255 byte UTF-8.")
    return data + b"\n"


def parse_line(line: str) -> Event | None:
    match = TAG.match(ANSI.sub("", line.rstrip("\r")))
    if not match:
        return None
    tag, text = match.groups()
    if tag in ("HISU_REPLY", "HISU_HEARD"):
        reply = REPLY.match(text)
        if reply:
            epoch, generation, fragment = reply.groups()
            return Event("reply" if tag == "HISU_REPLY" else "heard", fragment, (int(epoch), int(generation)))
    elif tag == "HISU_MIC" and text in ("enabled", "disabled"):
        return Event("mic", text)
    elif tag == "HISU_STATE" and text in VOICE_STATES:
        return Event("state", text)
    elif tag == "HISU_INPUT":
        return Event("input", text)
    elif tag == "HISU_CHAT":
        if text.startswith("Chưa gửi:"):
            return Event("rejected", "Hi Su chưa sẵn sàng nhận tin.")
        if text.startswith("Bỏ dòng"):
            return Event("invalid", "Board từ chối định dạng tin nhắn.")
        if text.startswith("Không mở được"):
            return Event("notice", "Chat USB trên board chưa sẵn sàng.")
    elif tag == "LIVE":
        if text == "Gemini Live setup ready":
            return Event("notice", "Gemini đã kết nối.")
        if text.startswith("Live credential profile="):
            return Event("notice", "Đang kết nối Gemini…")
        if text.startswith("WSS error:"):
            return Event("notice", "Kết nối Gemini gặp lỗi.")
    return None


class Protocol:
    """Bounded line framing, with an incremental UTF-8 decoder for split characters."""
    def __init__(self):
        self.decoder = codecs.getincrementaldecoder("utf-8")("replace")
        self.pending = ""
        self.dropping = False

    def feed(self, data: bytes) -> tuple[str, list[Event]]:
        decoded = self.decoder.decode(data)
        lines = (self.pending + decoded).split("\n")
        self.pending = ""
        events = []
        for line in lines[:-1]:
            if not self.dropping and len(line) <= MAX_LINE:
                event = parse_line(line)
                if event:
                    events.append(event)
            self.dropping = False
        tail = lines[-1]
        if not self.dropping and len(tail) <= MAX_LINE:
            self.pending = tail
        else:
            self.dropping = True
        return decoded, events


class RawLog:
    def __init__(self, directory: Path, port: str):
        directory.mkdir(parents=True, exist_ok=True)
        name = re.sub(r"[^A-Za-z0-9_-]", "_", port)[:32]
        self.path = directory / f"chat-{datetime.now():%Y%m%d-%H%M%S-%f}-{name}.log"
        self.handler = RotatingFileHandler(self.path, maxBytes=5 * 1024 * 1024,
                                           backupCount=2, encoding="utf-8")
        self.handler.terminator = ""
        self.logger = logging.Logger(str(self.path))
        self.logger.addHandler(self.handler)

    def write(self, text: str):
        if text:
            self.logger.info("%s", text)

    def close(self):
        self.handler.close()
        self.logger.removeHandler(self.handler)


class Link:
    def __init__(self, port: str, directory: Path, events: queue.Queue, token: int,
                 factory=serial.Serial):
        self.port, self.directory, self.events, self.token = port, directory, events, token
        self.factory = factory
        self.stop = threading.Event()
        self.outgoing = queue.Queue(maxsize=1)
        self.mic_disabled = False
        self.thread = threading.Thread(target=self.run, name="hisu-usb-chat", daemon=True)

    def publish(self, event: Event):
        while not self.stop.is_set():
            try:
                self.events.put((self.token, event), timeout=0.1)
                return
            except queue.Full:
                pass

    def send(self, text: str):
        data = encode_message(text)
        if self.stop.is_set():
            raise ValueError("USB đã ngắt kết nối.")
        try:
            self.outgoing.put_nowait(data)
        except queue.Full as exc:
            raise ValueError("Đang có một tin chờ gửi.") from exc

    def set_mic(self, enabled: bool):
        if self.stop.is_set():
            raise ValueError("USB đã ngắt kết nối.")
        try:
            self.outgoing.put_nowait(b"\x1eM1\n" if enabled else b"\x1eM0\n")
        except queue.Full as exc:
            raise ValueError("Đang có một tin chờ gửi.") from exc

    def run(self):
        device = log = None
        protocol = Protocol()
        stage = "log"
        try:
            log = RawLog(self.directory, self.port)
            stage = "usb"
            # Set control lines before open; do not use the ESP-IDF reset sequence.
            device = self.factory(baudrate=115200, timeout=0.1, write_timeout=1)
            device.dtr = False
            device.rts = False
            device.port = self.port
            device.open()
            if device.write(RESYNC) != len(RESYNC):
                raise OSError("UART synchronization short write")
            sync_pending = True
            sync_until = time.monotonic() + 2
            control_until = next_mic_lease = 0.0
            requested_mic = None
            self.publish(Event("connected", str(log.path)))
            while not self.stop.is_set():
                if requested_mic is not None and time.monotonic() > control_until:
                    requested_mic = None
                try:
                    outgoing = self.outgoing.get_nowait()
                except queue.Empty:
                    outgoing = None
                if outgoing is not None and not self.stop.is_set():
                    if outgoing.startswith(b"\x1e"):
                        control_until = time.monotonic() + 3
                        requested_mic = outgoing == b"\x1eM1\n"
                    if device.write(outgoing) != len(outgoing):
                        raise OSError("UART short write; message was not retransmitted")
                    if outgoing == b"\x1eM1\n":
                        self.mic_disabled = False
                if outgoing is None and self.mic_disabled and time.monotonic() >= next_mic_lease:
                    renewal=b"\x1eM0\n"
                    if device.write(renewal) != len(renewal):
                        raise OSError("UART mic lease short write")
                    next_mic_lease=time.monotonic()+MIC_LEASE_SECONDS
                data = device.read(1024)
                if data:
                    decoded, events = protocol.feed(data)
                    log.write(decoded)
                    for event in events:
                        if event.kind == "invalid" and sync_pending and time.monotonic() <= sync_until:
                            sync_pending = False
                            continue
                        if event.kind == "invalid" and time.monotonic() <= control_until:
                            control_until=0
                            requested_mic=None
                            self.publish(Event("mic_unavailable", "Firmware chưa hỗ trợ điều khiển nhận giọng nói."))
                            continue
                        if event.kind == "mic":
                            if requested_mic is not None and (event.text=="enabled") != requested_mic:
                                continue  # An older renewal acknowledgement is not this command's result.
                            self.mic_disabled=event.text=="disabled"
                            next_mic_lease=time.monotonic()+MIC_LEASE_SECONDS
                            control_until=0
                            requested_mic=None
                        self.publish(event)
        except (OSError, serial.SerialException) as exc:
            if log:
                log.write(f"\n[Chat Hi Su] {type(exc).__name__}: {exc}\n")
            text = "Không tạo được file log trên máy tính." if stage == "log" else (
                f"Không truy cập được {self.port}. Cổng đang bận hoặc USB đã ngắt.")
            self.publish(Event("error", text))
        finally:
            if device is not None:
                try:
                    device.close()
                except (OSError, serial.SerialException):
                    pass
            if log:
                log.write(protocol.decoder.decode(b"", final=True))
                log.close()
            self.stop.set()
            try:
                self.events.put_nowait((self.token, Event("closed")))
            except queue.Full:
                pass  # The UI also observes thread completion once queued events are drained.


class ChatWindow:
    def __init__(self, root: tk.Tk, directory: Path, preferred_port: str = "",
                 ports_provider=list_ports.comports):
        self.root, self.directory, self.ports_provider = root, directory, ports_provider
        self.events = queue.Queue(maxsize=512)
        self.link = None
        self.token = 0
        self.connected = False
        self.closing = False
        self.pending = None
        self.pending_until = 0.0
        self.mic_pending = None
        self.mic_until = 0.0
        self.mic_current = True
        self.board_state_supported = False
        self.typing = False
        self.typing_until = self.typing_next = 0.0
        self.typing_frame = 0
        self.last_role = self.reply_turn = None
        self.log_path = None
        self.ports = {}
        self.preferred_port = preferred_port
        self.poll_id = None
        root.title("Chat Hi Su")
        root.geometry("820x580")
        root.minsize(620, 420)
        root.configure(bg="#191b1e")
        root.columnconfigure(0, weight=1)
        root.rowconfigure(2, weight=1)
        style = ttk.Style(root)
        style.theme_use("clam")
        style.configure(".", font=("Segoe UI", 10), background="#191b1e", foreground="#eceeef")
        style.configure("TButton", padding=(10, 7), background="#303439", borderwidth=0)
        style.map("TButton", background=[("active", "#424950"), ("disabled", "#25282c")],
                  foreground=[("disabled", "#757d85")])
        style.configure("TEntry", fieldbackground="#24282d", insertcolor="#eceeef", padding=8)
        style.configure("TCombobox", fieldbackground="#24282d", background="#303439", padding=5)
        style.map("TCombobox", fieldbackground=[("readonly", "#24282d")],
                  foreground=[("readonly", "#eceeef")])
        root.option_add("*TCombobox*Listbox.background", "#24282d")
        root.option_add("*TCombobox*Listbox.foreground", "#eceeef")
        title = ttk.Frame(root, padding=(20, 16, 20, 10))
        title.grid(row=0, column=0, sticky="ew")
        title.columnconfigure(0, weight=1)
        ttk.Label(title, text="Chat Hi Su", font=("Segoe UI", 18, "bold")).grid(row=0, column=0, sticky="w")
        self.connection_label = ttk.Label(title, text="Chưa kết nối", foreground="#9ba4ad")
        self.connection_label.grid(row=0, column=1, sticky="e")
        toolbar = ttk.Frame(root, padding=(20, 0, 20, 14))
        toolbar.grid(row=1, column=0, sticky="ew")
        toolbar.columnconfigure(1, weight=1)
        ttk.Label(toolbar, text="USB").grid(row=0, column=0, padx=(0, 10))
        self.port_box = ttk.Combobox(toolbar, state="readonly", width=30)
        self.port_box.grid(row=0, column=1, sticky="ew", padx=(0, 8))
        self.refresh_button = ttk.Button(toolbar, text="Quét cổng", command=self.scan)
        self.refresh_button.grid(row=0, column=2, padx=(0, 8))
        self.connect_button = ttk.Button(toolbar, text="Kết nối", width=11, command=self.toggle_connection)
        self.connect_button.grid(row=0, column=3)
        transcript = ttk.Frame(root, padding=(20, 0))
        transcript.grid(row=2, column=0, sticky="nsew")
        transcript.columnconfigure(0, weight=1)
        transcript.rowconfigure(0, weight=1)
        self.history = tk.Text(transcript, state="disabled", wrap="word", bg="#191b1e",
                               fg="#eceeef", relief="flat", borderwidth=0, padx=0, pady=6,
                               font=("Segoe UI", 11), insertbackground="#eceeef")
        self.history.grid(row=0, column=0, sticky="nsew")
        scroll = ttk.Scrollbar(transcript, command=self.history.yview)
        scroll.grid(row=0, column=1, sticky="ns")
        self.history.configure(yscrollcommand=scroll.set)
        self.typing_label = ttk.Label(transcript, text=" ", foreground="#83dab8")
        self.typing_label.grid(row=1, column=0, sticky="w", pady=(4, 0))
        self.history.tag_configure("user_label", foreground="#91baff", font=("Segoe UI", 10, "bold"))
        self.history.tag_configure("reply_label", foreground="#83dab8", font=("Segoe UI", 10, "bold"))
        self.history.tag_configure("body", spacing3=4)
        composer = ttk.Frame(root, padding=(20, 10, 20, 16))
        composer.grid(row=3, column=0, sticky="ew")
        composer.columnconfigure(0, weight=1)
        self.status = ttk.Label(composer, text="", foreground="#9ba4ad")
        self.status.grid(row=0, column=0, columnspan=2, sticky="w", pady=(0, 8))
        self.draft = tk.StringVar()
        self.entry = ttk.Entry(composer, textvariable=self.draft, font=("Segoe UI", 11))
        self.entry.grid(row=1, column=0, sticky="ew", padx=(0, 10))
        self.entry.bind("<Return>", lambda _: self.send())
        self.send_button = ttk.Button(composer, text="Gửi", width=9, command=self.send, state="disabled")
        self.send_button.grid(row=1, column=1)
        self.count = ttk.Label(composer, text="0 / 255 byte", foreground="#9ba4ad")
        self.count.grid(row=2, column=0, sticky="w", pady=(10, 0))
        self.log_button = ttk.Button(composer, text="Mở log", command=self.open_log, state="disabled")
        self.log_button.grid(row=2, column=1, sticky="e", pady=(10, 0))
        self.mic_value = tk.BooleanVar(value=True)
        self.mic_button = ttk.Checkbutton(composer, text="Nhận giọng nói", variable=self.mic_value,
                                         command=self.change_mic, state="disabled")
        self.mic_button.grid(row=2, column=0, sticky="e", padx=(0, 12), pady=(10, 0))
        self.draft.trace_add("write", self.update_count)
        self.scan()
        root.protocol("WM_DELETE_WINDOW", self.close)
        self.poll_id = root.after(50, self.poll)

    def scan(self):
        selected = self.ports.get(self.port_box.get(), self.preferred_port)
        try:
            devices = sorted(self.ports_provider(), key=lambda p: p.device)
        except OSError:
            devices = []
        self.ports = {f"{p.device} · {p.description[:48]}": p.device for p in devices}
        self.port_box.configure(values=list(self.ports))
        label = next((label for label, device in self.ports.items() if device == selected), "")
        if not label and len(self.ports) == 1:
            label = next(iter(self.ports))
        self.port_box.set(label)
        self.connect_button.configure(state="normal" if label else "disabled")

    def update_count(self, *_):
        length = len(self.draft.get().encode("utf-8", errors="replace"))
        self.count.configure(text=f"{length} / 255 byte", foreground="#ff9b91" if length > MAX_INPUT else "#9ba4ad")

    def show_status(self, text: str, error=False):
        # Keep the status row bounded even for a long platform error.
        self.status.configure(text=text, foreground="#ff9b91" if error else "#9ba4ad", wraplength=500)

    def toggle_connection(self):
        if self.link is not None:
            self.link.stop.set()
            self.connected = False
            self.send_button.configure(state="disabled")
            self.connect_button.configure(state="disabled")
            self.connection_label.configure(text="Đang ngắt USB…")
            return
        port = self.ports.get(self.port_box.get())
        if not port:
            return
        self.token += 1
        self.link = Link(port, self.directory, self.events, self.token)
        self.port_box.configure(state="disabled")
        self.refresh_button.configure(state="disabled")
        self.connect_button.configure(state="disabled")
        self.connection_label.configure(text=f"Đang mở {port}…")
        self.show_status("")
        self.last_role = self.reply_turn = None
        self.link.thread.start()

    def send(self):
        if not self.connected or self.pending is not None or self.mic_pending is not None:
            return "break"
        text = self.draft.get().strip()
        try:
            self.link.send(text)
        except ValueError as exc:
            self.show_status(str(exc), True)
            return "break"
        self.pending = text
        self.pending_until = time.monotonic() + 5
        self.entry.configure(state="disabled")
        self.send_button.configure(state="disabled")
        self.show_status("Đang gửi…")
        return "break"

    def release_pending(self):
        self.pending = None
        self.entry.configure(state="normal")
        self.send_button.configure(state="normal" if self.connected and self.mic_pending is None else "disabled")

    def change_mic(self):
        if not self.connected or self.pending is not None or self.mic_pending is not None:
            self.mic_value.set(self.mic_current)
            return
        enabled=self.mic_value.get()
        try:
            self.link.set_mic(enabled)
        except ValueError as exc:
            self.mic_value.set(self.mic_current)
            self.show_status(str(exc), True)
            return
        self.mic_pending=enabled
        self.mic_until=time.monotonic()+3
        self.mic_button.configure(state="disabled")
        self.send_button.configure(state="disabled")

    def mic_result(self, enabled: bool, error=""):
        changed=enabled!=self.mic_current or self.mic_pending is not None
        self.mic_current=enabled
        self.mic_value.set(enabled)
        self.mic_pending=None
        self.mic_button.configure(state="normal" if self.connected else "disabled")
        self.send_button.configure(state="normal" if self.connected and self.pending is None else "disabled")
        if error or changed:
            self.show_status(error or ("Đã bật nhận giọng nói." if enabled else "Đã tắt nhận giọng nói."), bool(error))

    def set_typing(self, active: bool):
        self.typing = active
        if active:
            self.typing_until = time.monotonic() + 60
            if self.typing_label.cget("text") == " ":
                self.typing_frame = 0
                self.typing_next = 0
        else:
            self.typing_label.configure(text=" ")

    def animate_typing(self, now: float):
        if self.typing and now >= self.typing_until:
            self.set_typing(False)
        if self.typing and now >= self.typing_next:
            self.typing_label.configure(text="Hi Su " + "." * (self.typing_frame + 1))
            self.typing_frame = (self.typing_frame + 1) % 3
            self.typing_next = now + 0.4

    def append(self, role: str, text: str, turn=None):
        following = self.history.yview()[1] >= 0.98
        self.history.configure(state="normal")
        if role not in ("reply", "heard") or self.last_role != role or self.reply_turn != turn:
            if self.history.index("end-1c") != "1.0":
                self.history.insert("end", "\n\n")
            label = "Hi Su\n" if role == "reply" else "Mic nghe được\n" if role == "heard" else "Bạn\n"
            self.history.insert("end", label,
                                "reply_label" if role == "reply" else "user_label")
        self.history.insert("end", text, "body")
        self.last_role, self.reply_turn = role, turn
        size = self.history.count("1.0", "end-1c", "chars")
        if size and size[0] > 100000:
            self.history.delete("1.0", "end-75000c")
        self.history.configure(state="disabled")
        if following or role == "user":
            self.history.see("end")

    def handle(self, event: Event):
        if event.kind == "connected":
            if self.link.stop.is_set():
                return
            self.connected = True
            self.board_state_supported = False
            self.set_typing(False)
            self.log_path = Path(event.text)
            self.log_button.configure(state="normal")
            self.connection_label.configure(text=f"Đã kết nối {self.link.port}", foreground="#83dab8")
            self.connect_button.configure(text="Ngắt kết nối", state="normal")
            self.send_button.configure(state="normal")
            self.mic_result(True)
            self.entry.focus_set()
        elif event.kind == "input":
            self.append("user", event.text)
            self.set_typing(True)
            if self.pending == event.text or (self.pending is None and self.draft.get().strip() == event.text):
                self.draft.set("")
                self.release_pending()
            self.show_status("Hi Su đã nhận tin.")
        elif event.kind in ("reply", "heard"):
            self.append(event.kind, event.text, event.turn)
            if event.kind == "heard":
                # A transcript can be background speech; firmware validates it first.
                pass
            elif self.board_state_supported:
                self.set_typing(True)
            else:
                self.set_typing(False)  # Older firmware has no explicit end-of-reply event.
        elif event.kind == "state":
            self.board_state_supported = True
            self.set_typing(event.text in ("processing", "speaking"))
        elif event.kind == "mic":
            self.mic_result(event.text=="enabled")
        elif event.kind == "mic_unavailable":
            self.mic_result(True, event.text)
        elif event.kind in ("error", "rejected", "invalid"):
            if event.kind == "error" or self.pending is not None:
                self.set_typing(False)
            self.release_pending()
            self.show_status(event.text, True)
        elif event.kind == "notice":
            self.show_status(event.text)
            if event.text == "Kết nối Gemini gặp lỗi.":
                self.set_typing(False)
        elif event.kind == "closed":
            self.set_typing(False)
            self.connected = False
            self.link = None
            self.mic_result(True)
            self.release_pending()
            self.connection_label.configure(text="Chưa kết nối", foreground="#9ba4ad")
            self.connect_button.configure(text="Kết nối", state="normal" if self.port_box.get() else "disabled")
            self.port_box.configure(state="readonly")
            self.refresh_button.configure(state="normal")

    def poll(self):
        if self.closing:
            return
        for _ in range(64):
            try:
                token, event = self.events.get_nowait()
            except queue.Empty:
                break
            if token == self.token and self.link is not None:
                self.handle(event)
        if self.link is not None and not self.link.thread.is_alive() and self.events.empty():
            self.handle(Event("closed"))
        if self.pending is not None and time.monotonic() >= self.pending_until:
            self.set_typing(False)
            self.release_pending()
            self.show_status("Chưa có xác nhận từ board.", True)
        if self.mic_pending is not None and time.monotonic() >= self.mic_until:
            self.mic_result(self.mic_current,"Chưa có xác nhận điều khiển nhận giọng nói.")
        self.animate_typing(time.monotonic())
        self.poll_id = self.root.after(50, self.poll)

    def open_log(self):
        if self.log_path is not None:
            try:
                os.startfile(self.log_path)
            except OSError:
                messagebox.showerror("Chat Hi Su", "Không mở được file log.", parent=self.root)

    def close(self):
        self.closing = True
        if self.poll_id is not None:
            self.root.after_cancel(self.poll_id)
        self.root.withdraw()
        if self.link:
            self.link.stop.set()
            self.link.thread.join(timeout=1.5)
        self.root.destroy()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default="")
    parser.add_argument("--log-dir", type=Path, default=Path(__file__).resolve().parents[1] / ".cache" / "chat-logs")
    args = parser.parse_args()
    root = tk.Tk()
    ChatWindow(root, args.log_dir, args.port)
    root.mainloop()


if __name__ == "__main__":
    main()
