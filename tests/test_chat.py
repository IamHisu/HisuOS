import queue
from pathlib import Path
import sys
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import hisu_chat as chat

TEMP_ROOT = Path(__file__).resolve().parents[1] / "build" / "chat-tests"
TEMP_ROOT.mkdir(parents=True, exist_ok=True)


class ProtocolTests(unittest.TestCase):
    def test_fragmented_unicode_and_reply_spaces(self):
        protocol = chat.Protocol()
        wire = ("I (1) HISU_REPLY: [2:3] Rồi, \r\n"
                "I (2) HISU_REPLY: [2:3] rõ ràng.\r\n").encode()
        text, events = "", []
        for byte in wire:
            decoded, batch = protocol.feed(bytes([byte]))
            text += decoded
            events.extend(batch)
        self.assertEqual(text, wire.decode())
        self.assertEqual("".join(event.text for event in events), "Rồi, rõ ràng.")
        self.assertTrue(all(event.turn == (2, 3) for event in events))

    def test_technical_logs_stay_out_of_chat(self):
        protocol = chat.Protocol()
        logs = ("I (17359) VOICE: VAD silence, volume -47.8 dBFS\n"
                "I (18749) VOICE: Mic mono slot=L mean=17\n"
                "W (1) wifi:m f null\nI (2) LIVE: RAM internal free=12623\n")
        decoded, events = protocol.feed(logs.encode())
        self.assertEqual(decoded, logs)
        self.assertEqual(events, [])

    def test_microphone_transcript_and_control_ack(self):
        _, events = chat.Protocol().feed(
            b"I (1) HISU_HEARD: [2:3] ambient speech\nI (2) HISU_MIC: disabled\n")
        self.assertEqual(events, [chat.Event("heard", "ambient speech", (2, 3)), chat.Event("mic", "disabled")])

    def test_board_state_is_allowlisted(self):
        _, events = chat.Protocol().feed(
            b"I (1) HISU_STATE: processing\nI (2) HISU_STATE: waiting\nI (3) HISU_STATE: unexpected\n")
        self.assertEqual(events,[chat.Event("state","processing"),chat.Event("state","waiting")])

    def test_ansi_crlf_input_ack_and_rejection(self):
        _, events = chat.Protocol().feed(
            "\x1b[32mI (20) HISU_INPUT: Chiều nay có mưa không?\x1b[0m\r\n"
            "W (30) HISU_CHAT: Chưa gửi: nhấn đúp BOOT\n".encode())
        self.assertEqual([e.kind for e in events], ["input", "rejected"])
        self.assertEqual(events[0].text, "Chiều nay có mưa không?")

    def test_oversized_and_invalid_lines_recover(self):
        protocol = chat.Protocol()
        for _ in range(100):
            protocol.feed(b"x" * 1024)
            self.assertLessEqual(len(protocol.pending), chat.MAX_LINE)
        _, events = protocol.feed(b"\nI (1) HISU_INPUT: ok\n")
        self.assertEqual(events, [chat.Event("input", "ok")])
        _, events = protocol.feed(b"I (2) VOICE: \xff\nI (3) HISU_REPLY: invalid\n")
        self.assertEqual(events, [])

    def test_encode_exact_limit_controls_and_multibyte(self):
        self.assertEqual(chat.encode_message("a" * 255), b"a" * 255 + b"\n")
        self.assertEqual(chat.encode_message("Rồi"), "Rồi\n".encode())
        for invalid in ("", "   ", "a" * 256, "ừ" * 86, "a\nb", "a\rb", "a\x00b", "a\x7fb", "\ud800"):
            with self.subTest(invalid=repr(invalid)), self.assertRaises(ValueError):
                chat.encode_message(invalid)


class FakeSerial:
    def __init__(self, **kwargs):
        self.options = kwargs
        self.incoming = queue.Queue()
        self.writes = []
        self.closed = False
        self.opened = threading.Event()
        self.short_write = False

    def open(self):
        self.open_lines = self.dtr, self.rts
        self.opened.set()

    def write(self, data):
        self.writes.append(data)
        if data == chat.RESYNC:
            self.incoming.put("W (1) HISU_CHAT: Bỏ dòng không hợp lệ\n".encode())
            return len(data)
        if self.short_write:
            return 1
        if data in (b"\x1eM0\n", b"\x1eM1\n"):
            self.incoming.put(b"I (1) HISU_MIC: " + (b"disabled\n" if data==b"\x1eM0\n" else b"enabled\n"))
            return len(data)
        self.incoming.put(b"I (1) HISU_INPUT: " + data)
        return len(data)

    def read(self, _size):
        try:
            data = self.incoming.get(timeout=0.02)
            if isinstance(data, BaseException):
                raise data
            return data
        except queue.Empty:
            return b""

    def close(self):
        self.closed = True


class LinkTests(unittest.TestCase):
    def test_old_firmware_rejects_mic_control_without_a_chat_turn(self):
        class OlderSerial(FakeSerial):
            def write(self, data):
                if data.startswith(b"\x1e"):
                    self.writes.append(data)
                    self.incoming.put("W (2) HISU_CHAT: Bỏ dòng không hợp lệ\n".encode())
                    return len(data)
                return super().write(data)
        with tempfile.TemporaryDirectory(dir=TEMP_ROOT) as directory:
            events=queue.Queue()
            device=OlderSerial()
            link=chat.Link("COM8",Path(directory),events,7,lambda **_:device)
            link.thread.start()
            try:
                self.wait_event(events,"connected")
                link.set_mic(False)
                self.wait_event(events,"mic_unavailable")
                self.assertFalse(link.mic_disabled)
                self.assertEqual(device.writes,[chat.RESYNC,b"\x1eM0\n"])
            finally:
                link.stop.set(); link.thread.join(2)

    def test_mic_control_is_not_sent_as_chat_and_lease_stops_on_enable(self):
        with tempfile.TemporaryDirectory(dir=TEMP_ROOT) as directory:
            events=queue.Queue()
            device=FakeSerial()
            link=chat.Link("COM8",Path(directory),events,7,lambda **_:device)
            old_period=chat.MIC_LEASE_SECONDS
            chat.MIC_LEASE_SECONDS=0.03
            link.thread.start()
            try:
                self.wait_event(events,"connected")
                self.assertEqual(device.writes,[chat.RESYNC])
                link.set_mic(False)
                self.assertEqual(self.wait_event(events,"mic").text,"disabled")
                self.assertEqual(self.wait_event(events,"mic").text,"disabled")  # Renewal.
                link.set_mic(True)
                deadline=time.monotonic()+1
                while self.wait_event(events,"mic").text!="enabled":
                    self.assertLess(time.monotonic(),deadline)
                time.sleep(0.1)
                self.assertEqual(device.writes[-1],b"\x1eM1\n")
            finally:
                link.stop.set(); link.thread.join(2)
                chat.MIC_LEASE_SECONDS=old_period

    def wait_event(self, events, kind):
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            token, event = events.get(timeout=2)
            self.assertEqual(token, 7)
            if event.kind == kind:
                return event
        self.fail(f"No {kind} event")

    def test_no_reset_ack_log_and_clean_disconnect(self):
        with tempfile.TemporaryDirectory(dir=TEMP_ROOT) as directory:
            events = queue.Queue(maxsize=512)
            device = FakeSerial()
            link = chat.Link("COM8", Path(directory), events, 7, lambda **_: device)
            self.assertFalse(device.opened.is_set())
            link.thread.start()
            try:
                connected = self.wait_event(events, "connected")
                self.assertEqual(device.open_lines, (False, False))
                device.incoming.put(b"I (1) VOICE: VAD speech\n")
                link.send("Rồi, rõ ràng")
                ack = self.wait_event(events, "input")
                self.assertEqual(ack.text, "Rồi, rõ ràng")
                self.assertEqual(device.writes, [chat.RESYNC, "Rồi, rõ ràng\n".encode()])
            finally:
                link.stop.set()
                link.thread.join(2)
            self.assertFalse(link.thread.is_alive())
            self.assertTrue(device.closed)
            self.wait_event(events, "closed")
            self.assertIn("VAD speech", Path(connected.text).read_text(encoding="utf-8"))

    def test_busy_port_is_reported_without_retry(self):
        def denied(**_):
            raise chat.serial.SerialException("Access denied")
        with tempfile.TemporaryDirectory(dir=TEMP_ROOT) as directory:
            events = queue.Queue()
            link = chat.Link("COM8", Path(directory), events, 7, denied)
            link.thread.start()
            self.wait_event(events, "error")
            link.thread.join(2)
            self.wait_event(events, "closed")
            self.assertTrue(link.stop.is_set())

    def test_partial_write_is_not_replayed(self):
        with tempfile.TemporaryDirectory(dir=TEMP_ROOT) as directory:
            events = queue.Queue()
            device = FakeSerial()
            device.short_write = True
            link = chat.Link("COM8", Path(directory), events, 7, lambda **_: device)
            link.thread.start()
            self.wait_event(events, "connected")
            link.send("test")
            self.wait_event(events, "error")
            link.thread.join(2)
            self.assertEqual(device.writes, [chat.RESYNC, b"test\n"])
            self.assertTrue(device.closed)

    def test_unplug_reports_error_and_closes(self):
        with tempfile.TemporaryDirectory(dir=TEMP_ROOT) as directory:
            events = queue.Queue()
            device = FakeSerial()
            link = chat.Link("COM8", Path(directory), events, 7, lambda **_: device)
            link.thread.start()
            self.wait_event(events, "connected")
            device.incoming.put(chat.serial.SerialException("Device removed"))
            self.wait_event(events, "error")
            link.thread.join(2)
            self.wait_event(events, "closed")
            self.assertTrue(device.closed)
            self.assertEqual(device.writes, [chat.RESYNC])

    def test_stopped_link_will_not_queue_messages(self):
        link = chat.Link("COM8", Path("."), queue.Queue(), 7)
        link.stop.set()
        with self.assertRaises(ValueError):
            link.send("test")
        self.assertTrue(link.outgoing.empty())

    def test_raw_log_rotation(self):
        with tempfile.TemporaryDirectory(dir=TEMP_ROOT) as directory:
            log = chat.RawLog(Path(directory), "COM8")
            log.handler.maxBytes = 100
            for _ in range(8):
                log.write("x" * 80 + "\n")
            log.close()
            self.assertEqual(len(list(Path(directory).iterdir())), 3)


class WindowTests(unittest.TestCase):
    def test_mic_switch_requires_ack_and_does_not_change_draft(self):
        controls=[]
        self.app.connected=True
        self.app.link=SimpleNamespace(set_mic=controls.append)
        self.app.draft.set("Chiều nay có mưa không?")
        self.assertTrue(self.app.mic_value.get())
        self.app.mic_value.set(False)
        self.app.change_mic()
        self.assertEqual(controls,[False])
        self.assertTrue(self.app.mic_current)
        self.assertIn("disabled",self.app.send_button.state())
        self.app.handle(chat.Event("mic","disabled"))
        self.assertFalse(self.app.mic_current)
        self.assertNotIn("disabled",self.app.send_button.state())
        self.assertEqual(self.app.draft.get(),"Chiều nay có mưa không?")
        self.app.link=None

    def test_mic_timeout_rolls_back_without_replaying(self):
        self.app.connected=True
        self.app.mic_pending=False
        self.app.mic_value.set(False)
        self.app.mic_until=time.monotonic()-1
        self.root.after_cancel(self.app.poll_id)
        self.app.poll()
        self.assertIsNone(self.app.mic_pending)
        self.assertTrue(self.app.mic_value.get())
        self.assertIn("Chưa có xác nhận",self.app.status.cget("text"))

    def test_heard_transcript_is_not_a_typed_ack(self):
        self.app.draft.set("test")
        self.app.pending="test"
        self.app.handle(chat.Event("heard","test",(1,3)))
        self.app.handle(chat.Event("heard"," ambient",(1,3)))
        self.assertEqual(self.app.pending,"test")
        self.assertEqual(self.app.draft.get(),"test")
        self.assertEqual(self.app.history.get("1.0","end-1c"),"Mic nghe được\ntest ambient")

    def test_typing_animates_without_polluting_history_and_stops_when_done(self):
        self.app.handle(chat.Event("state","processing"))
        now=time.monotonic()
        self.app.animate_typing(now)
        self.assertEqual(self.app.typing_label.cget("text"),"Hi Su .")
        self.app.animate_typing(now+0.5)
        self.assertEqual(self.app.typing_label.cget("text"),"Hi Su ..")
        self.app.handle(chat.Event("reply","Rồi nè.",(1,2)))
        self.app.handle(chat.Event("state","speaking"))
        self.assertTrue(self.app.typing)
        self.app.handle(chat.Event("state","waiting"))
        self.assertFalse(self.app.typing)
        self.assertEqual(self.app.typing_label.cget("text")," ")
        self.assertEqual(self.app.history.get("1.0","end-1c"),"Hi Su\nRồi nè.")

    def test_typing_old_firmware_timeout_and_disconnect(self):
        self.app.handle(chat.Event("input","test"))
        self.assertTrue(self.app.typing)
        self.app.handle(chat.Event("reply","ok",(1,2)))
        self.assertFalse(self.app.typing)
        self.app.handle(chat.Event("state","processing"))
        self.app.animate_typing(time.monotonic()+61)
        self.assertFalse(self.app.typing)
        self.app.handle(chat.Event("state","speaking"))
        self.app.handle(chat.Event("closed"))
        self.assertFalse(self.app.typing)

    def test_typing_stops_on_connection_error(self):
        self.app.handle(chat.Event("state","processing"))
        self.app.handle(chat.Event("error","USB disconnected"))
        self.assertFalse(self.app.typing)

    def setUp(self):
        self.root = chat.tk.Tk()
        self.root.withdraw()
        self.app = chat.ChatWindow(self.root, Path("."), "COM8", lambda: [
            SimpleNamespace(device="COM8", description="USB UART")])
        self.root.update_idletasks()

    def tearDown(self):
        self.app.close()

    def test_no_automatic_serial_open_and_fixed_input(self):
        self.assertIsNone(self.app.link)
        self.assertFalse(self.app.connected)
        self.assertIn("disabled", self.app.send_button.state())
        self.assertEqual(self.app.entry.grid_info()["row"], 1)
        self.assertEqual(self.app.root.grid_rowconfigure(2)["weight"], 1)
        self.assertEqual(self.app.ports[self.app.port_box.get()], "COM8")

    def test_streaming_reply_is_one_message_per_turn(self):
        self.app.handle(chat.Event("reply", "Rồi, ", (1, 2)))
        self.app.handle(chat.Event("reply", "rõ ràng.", (1, 2)))
        history = self.app.history.get("1.0", "end-1c")
        self.assertEqual(history, "Hi Su\nRồi, rõ ràng.")
        self.app.handle(chat.Event("input", "Cảm ơn"))
        self.app.handle(chat.Event("reply", "Ừ nè.", (1, 3)))
        self.assertEqual(self.app.history.get("1.0", "end-1c").count("Hi Su\n"), 2)

    def test_rejected_message_is_kept_in_input(self):
        self.app.draft.set("Chiều nay có mưa không?")
        self.app.pending = self.app.draft.get()
        self.app.entry.configure(state="disabled")
        self.app.handle(chat.Event("rejected", "Chưa sẵn sàng"))
        self.assertIsNone(self.app.pending)
        self.assertEqual(self.app.draft.get(), "Chiều nay có mưa không?")
        self.assertNotIn("disabled", self.app.entry.state())
        self.assertEqual(self.app.history.get("1.0", "end-1c"), "")

    def test_only_acknowledged_input_clears_draft(self):
        self.app.draft.set("Rồi")
        self.app.pending = "Rồi"
        self.app.handle(chat.Event("input", "Rồi"))
        self.assertEqual(self.app.draft.get(), "")
        self.assertIsNone(self.app.pending)
        self.assertEqual(self.app.history.get("1.0", "end-1c"), "Bạn\nRồi")

    def test_send_waits_for_ack_and_never_sends_twice(self):
        sent = []
        self.app.connected = True
        self.app.link = SimpleNamespace(send=sent.append)
        self.app.draft.set("Rồi")
        self.app.send()
        self.app.send()
        self.assertEqual(sent, ["Rồi"])
        self.assertEqual(self.app.pending, "Rồi")
        self.assertEqual(self.app.draft.get(), "Rồi")
        self.assertIn("disabled", self.app.entry.state())
        self.assertEqual(self.app.history.get("1.0", "end-1c"), "")
        self.app.link = None

    def test_scrolling_history_is_bounded(self):
        self.app.append("reply", "a" * 110000, (1, 1))
        self.assertLess(len(self.app.history.get("1.0", "end-1c")), 100000)

    def test_ack_timeout_preserves_text_without_resend(self):
        self.app.pending = "test"
        self.app.pending_until = time.monotonic() - 1
        self.app.draft.set("test")
        self.root.after_cancel(self.app.poll_id)
        self.app.poll()
        self.assertIsNone(self.app.pending)
        self.assertEqual(self.app.draft.get(), "test")
        self.assertEqual(self.app.status.cget("text"), "Chưa có xác nhận từ board.")

    def test_old_connection_events_are_ignored(self):
        self.app.token = 4
        self.app.link = SimpleNamespace(thread=SimpleNamespace(is_alive=lambda: True))
        self.app.events.put((3, chat.Event("input", "stale")))
        self.root.after_cancel(self.app.poll_id)
        self.app.poll()
        self.assertEqual(self.app.history.get("1.0", "end-1c"), "")
        self.app.link = None

    def test_disconnect_completion_restores_controls(self):
        self.app.link = SimpleNamespace(thread=SimpleNamespace(is_alive=lambda: False))
        self.app.connected = True
        self.root.after_cancel(self.app.poll_id)
        self.app.poll()
        self.assertIsNone(self.app.link)
        self.assertFalse(self.app.connected)
        self.assertNotIn("disabled", self.app.connect_button.state())


if __name__ == "__main__":
    unittest.main()
