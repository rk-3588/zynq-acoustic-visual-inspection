"""Offline behavioral tests; never opens a serial device or controller socket."""
import copy
import hashlib
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from uart_rx import Decoder, analyze, load_trace, replay, validate_trace

CASE = load_trace(ROOT / "fixtures/fifo_config.json")
LENGTHS = {int(k, 16): v for k, v in CASE["protocol"]["type_lengths"].items()}


def frame(kind=0x26, task=8, payload=None):
    if payload is None:
        payload = bytes([0x5A]) * (LENGTHS[kind] - 7)
    return b"\xaa\x55" + bytes([kind]) + task.to_bytes(2, "little") + payload + b"\r\n"


def trace(events, request=True):
    result = copy.deepcopy(CASE)
    result["events"] = [{"at_ms": now, "data_hex": data.hex()} for now, data in events]
    if not request:
        result.pop("request")
    return result


def c_replay(executable, item):
    data = "".join(f"{e['at_ms']} {bytes.fromhex(e['data_hex']).hex()}\n" for e in item["events"])
    process = subprocess.run([executable], input=data, capture_output=True, text=True,
                             timeout=10, check=True)
    rows = [json.loads(line) for line in process.stdout.splitlines()]
    return rows[:-1], {k: v for k, v in rows[-1].items() if k != "kind"}


class TimingTests(unittest.TestCase):
    def test_9600_8n1_fifo_estimate(self):
        result = analyze(CASE)
        self.assertEqual(result["bits_per_character"], 10)
        self.assertAlmostEqual(result["fifo_fill_estimate_ms"], 58.3333333333)
        self.assertAlmostEqual(result["frame_wire_time_ms"]["26"], 135.4166666667)
        self.assertEqual(result["delivery_lengths"], [56, 56, 18])
        self.assertEqual(result["delivery_gaps_ms"], [59, 22])

    def test_second_configuration_has_its_own_timing(self):
        alternate = load_trace(ROOT / "fixtures/alternate_protocol.json")
        self.assertEqual(analyze(alternate)["bits_per_character"], 11)
        self.assertEqual(len(replay(alternate)["matched_frames"]), 1)
        self.assertAlmostEqual(analyze(alternate)["fifo_fill_estimate_ms"], 16 * 11 / 115.2)

    def test_case_negative_control_and_repair(self):
        old, new = replay(CASE, 50), replay(CASE, 200)
        self.assertEqual(old["summary"]["frame_count"], 0)
        self.assertEqual(old["summary"]["expired"], 1)
        self.assertEqual(new["summary"]["frame_count"], 1)
        self.assertEqual(new["summary"]["expired"], 0)
        self.assertEqual(new["frames"][0]["type"], "26")
        self.assertEqual(len(new["matched_frames"]), 1)

    def test_gap_equal_to_limit_is_retained(self):
        raw = frame()
        self.assertEqual(replay(trace([(0, raw[:56]), (200, raw[56:])]))["summary"]["frame_count"], 1)

    def test_real_host_idle_expires_and_next_frame_recovers(self):
        raw = frame()
        result = replay(trace([(0, raw[:56]), (201, raw[56:]), (300, raw)]))
        self.assertEqual(result["summary"]["expired"], 1)
        self.assertEqual(result["summary"]["frame_count"], 1)

    def test_clock_regression_discards_partial_candidate(self):
        result = replay(trace([(100, frame()[:20]), (90, frame())]))
        self.assertEqual(result["summary"]["clock_regressions"], 1)
        self.assertEqual(result["summary"]["expired"], 1)
        self.assertEqual(len(result["frames"]), 1)


class FramingTests(unittest.TestCase):
    def test_split_header_and_single_byte_delivery(self):
        raw = frame(0x02, 0, b"\x00")
        result = replay(trace([(i, bytes([b])) for i, b in enumerate(raw)], False))
        self.assertEqual(result["frames"][0]["task_id"], 0)
        self.assertEqual(result["frames"][0]["payload_hex"], "00")

    def test_concatenated_frames(self):
        result = replay(trace([(0, frame(0x02, 1) + frame(0x28, 2) + frame(0x26, 3))], False))
        self.assertEqual([r["type"] for r in result["frames"]], ["02", "28", "26"])

    def test_embedded_valid_short_frame_is_only_payload(self):
        payload = bytearray([0x5A] * 123)
        payload[20:28] = frame(0x02, 8, b"\x00")
        raw = frame(payload=payload)
        result = replay(trace([(0, raw[:56]), (59, raw[56:])]))
        self.assertEqual([r["type"] for r in result["frames"]], ["26"])

    def test_noise_unknown_type_and_header_resynchronization(self):
        raw = b"\x00\x55\xaa\xaa\x55\xff\x00" + frame(0x02, 65535)
        result = replay(trace([(0, raw)], False))
        self.assertEqual(result["frames"][0]["task_id"], 65535)
        self.assertGreaterEqual(result["summary"]["rejected"], 1)

    def test_bad_tail_is_rejected_then_valid_frame_recovers(self):
        bad = bytearray(frame(0x02, 2)); bad[-1] = 0xFF
        result = replay(trace([(0, bytes(bad) + frame(0x02, 3))], False))
        self.assertEqual([r["task_id"] for r in result["frames"]], [3])
        self.assertGreaterEqual(result["summary"]["rejected"], 1)

    def test_truncation_is_not_a_packet(self):
        result = replay(trace([(0, frame()[:64])]))
        self.assertEqual(result["frames"], [])
        self.assertEqual(result["summary"]["buffered_bytes"], 64)

    def test_payload_mutation_is_not_detectable_without_integrity_field(self):
        raw = bytearray(frame()); raw[17] ^= 1
        self.assertEqual(replay(trace([(0, raw)]))["summary"]["frame_count"], 1)

    def test_random_partitioning_preserves_content(self):
        rng = random.Random(711)
        for _ in range(50):
            raw = frame(0x26, rng.randrange(65536), bytes(rng.randrange(256) for _ in range(123)))
            events, now, pos = [], 0, 0
            while pos < len(raw):
                size = rng.randrange(1, 40)
                events.append((now, raw[pos:pos + size])); pos += size; now += rng.randrange(0, 60)
            result = replay(trace(events, False))
            self.assertEqual(len(result["frames"]), 1)
            self.assertEqual(result["frames"][0]["payload_hex"], raw[5:-2].hex())

    def test_fuzzed_input_keeps_buffer_bounded(self):
        rng = random.Random(19)
        decoder = Decoder(LENGTHS, 200, 130)
        for i in range(5000):
            decoder.feed(bytes([rng.randrange(256)]), i)
            self.assertLessEqual(len(decoder.buffer), 130)


class TransactionTests(unittest.TestCase):
    def test_wrong_id_and_type_are_not_matched(self):
        result = replay(trace([(0, frame(0x26, 9) + frame(0x02, 8))]))
        self.assertEqual(len(result["frames"]), 2)
        self.assertEqual(len(result["matched_frames"]), 0)
        self.assertEqual(len(result["ignored_frames"]), 2)

    def test_total_deadline_is_independent_of_host_idle(self):
        raw = frame()
        result = replay(trace([(649, raw[:56]), (651, raw[56:])]))
        self.assertEqual(len(result["frames"]), 1)
        self.assertEqual(len(result["matched_frames"]), 0)
        self.assertEqual(len(result["late_frames"]), 1)

    def test_exact_deadline_is_accepted(self):
        self.assertEqual(len(replay(trace([(650, frame())]))["matched_frames"]), 1)

    def test_response_before_request_is_ignored(self):
        item = trace([(10, frame())]); item["request"]["sent_at_ms"] = 20
        self.assertEqual(len(replay(item)["ignored_frames"]), 1)


class InputTests(unittest.TestCase):
    def reject(self, mutate):
        item = copy.deepcopy(CASE); mutate(item)
        with self.assertRaises(ValueError): validate_trace(item)

    def test_schema_version_and_empty_events(self):
        self.reject(lambda d: d.update(schema_version=2))
        self.reject(lambda d: d.update(schema_version=True))
        self.reject(lambda d: d.update(events=[]))
        self.reject(lambda d: d.update(serial=None))
        self.reject(lambda d: d.update(protocol=[]))

    def test_invalid_hex_and_type_lengths(self):
        self.reject(lambda d: d["events"][0].update(data_hex="not hex"))
        self.reject(lambda d: d["protocol"].update(type_lengths={"26": 131}))
        self.reject(lambda d: d["protocol"].update(type_lengths={"26": 6}))
        self.reject(lambda d: d["protocol"].update(type_lengths={"02": 8, "0x02": 8}))
        self.reject(lambda d: d["protocol"].update(type_lengths={"+A": 8}))
        self.reject(lambda d: d["protocol"].update(type_lengths={" A": 8}))
        self.reject(lambda d: d["protocol"].update(type_lengths={"2a": 8, "2A": 8}))

    def test_invalid_serial_and_idle_values(self):
        self.reject(lambda d: d["serial"].update(baud=0))
        self.reject(lambda d: d["serial"].update(baud=True))
        self.reject(lambda d: d["serial"].update(stop_bits=float("nan")))
        with self.assertRaises(ValueError): replay(CASE, 0)

    def test_invalid_time_and_request_fields(self):
        self.reject(lambda d: d["events"][0].update(at_ms=-1))
        self.reject(lambda d: d["request"].update(task_id=65536))
        self.reject(lambda d: d["request"].update(deadline_ms=0))

    def test_cli_json_output_and_same_file_protection(self):
        with tempfile.TemporaryDirectory(prefix="uart-skill-cli-") as tmp:
            source = Path(tmp) / "trace.json"
            source.write_text(json.dumps(CASE), encoding="utf-8")
            before = hashlib.sha256(source.read_bytes()).hexdigest()
            command = [sys.executable, "-B", str(ROOT / "scripts/uart_rx.py")]
            ok = subprocess.run(command + ["analyze", str(source)], capture_output=True, text=True)
            self.assertEqual(ok.returncode, 0, ok.stderr)
            self.assertEqual(json.loads(ok.stdout)["delivery_lengths"], [56, 56, 18])
            denied = subprocess.run(command + ["replay", str(source), "--output", str(source)],
                                    capture_output=True, text=True)
            self.assertEqual(denied.returncode, 2)
            self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), before)


class FrozenCParityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.before = os.environ.get("UART_RX_BEFORE_EXE")
        cls.after = os.environ.get("UART_RX_AFTER_EXE")
        if not cls.before or not cls.after:
            raise unittest.SkipTest("frozen C probes are supplied by scripts/verify.py")

    def parity(self, item, before=False):
        native_frames, native_summary = c_replay(self.before if before else self.after, item)
        model = replay(item, 50 if before else 200)
        self.assertEqual(native_frames, model["frames"])
        self.assertEqual(native_summary, {k: model["summary"][k] for k in native_summary})
        return native_frames

    def test_archived_bug_reproduced_in_real_c_versions(self):
        self.assertEqual(len(self.parity(CASE, True)), 0)
        self.assertEqual(len(self.parity(CASE)), 1)

    def test_valid_nested_markers_and_continuous_frames(self):
        payload = bytearray([0x5A] * 123); payload[20:28] = frame(0x02, 8)
        raw = frame(payload=payload) + frame(0x28, 9)
        for old in (True, False): self.parity(trace([(10, raw)], False), old)

    def test_idle_expiration_and_recovery(self):
        raw = frame()
        item = trace([(0, raw[:56]), (201, raw[56:]), (300, raw)], False)
        for old in (True, False): self.parity(item, old)

    def test_corrupt_input_then_recovery(self):
        bad = bytearray(frame(0x02, 2)); bad[-1] = 0xFF
        item = trace([(0, b"\x00\xaa\x55\xff" + bytes(bad) + frame(0x02, 0))], False)
        for old in (True, False): self.parity(item, old)

    def test_clock_regression(self):
        item = trace([(100, frame()[:20]), (90, frame())], False)
        for old in (True, False): self.parity(item, old)

    def test_deterministic_fragmentation_corpus(self):
        rng = random.Random(808)
        for _ in range(24):
            kind = rng.choice(list(LENGTHS))
            raw = frame(kind, rng.randrange(65536), bytes(rng.randrange(256) for _ in range(LENGTHS[kind] - 7)))
            events, now, pos = [], 0, 0
            while pos < len(raw):
                n = rng.randrange(1, 40); events.append((now, raw[pos:pos + n]))
                pos += n; now += rng.choice([0, 15, 49, 50, 51, 59, 200, 201])
            item = trace(events, False)
            for old in (True, False): self.parity(item, old)


if __name__ == "__main__":
    unittest.main(verbosity=2)
