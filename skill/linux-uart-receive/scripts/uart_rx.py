#!/usr/bin/env python3
"""Offline UART delivery analysis and fixed-TYPE binary-frame replay."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

MAX_INPUT_BYTES = 8 * 1024 * 1024
MAX_TRACE_BYTES = 1024 * 1024
MAX_EVENTS = 10000


def integer(value, label, low=0, high=(1 << 64) - 1):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"{label} must be an integer in [{low}, {high}]")
    return value


def type_byte(value):
    if not isinstance(value, str) or len(value) != 2 or any(c not in "0123456789abcdefABCDEF" for c in value):
        raise ValueError("TYPE keys must be two hexadecimal digits")
    try:
        return int(value, 16)
    except ValueError as exc:
        raise ValueError("TYPE keys must be two hexadecimal digits") from exc


def validate_trace(trace):
    if not isinstance(trace, dict) or type(trace.get("schema_version")) is not int or trace["schema_version"] != 1:
        raise ValueError("trace requires schema_version=1")
    serial = trace.get("serial", {})
    if not isinstance(serial, dict):
        raise ValueError("serial must be an object")
    integer(serial.get("baud"), "baud", 1, 100000000)
    integer(serial.get("data_bits"), "data_bits", 5, 9)
    if serial.get("parity") not in ("none", "even", "odd", "mark", "space"):
        raise ValueError("unsupported parity")
    stop = serial.get("stop_bits")
    if type(stop) not in (int, float) or stop not in (1, 1.5, 2):
        raise ValueError("stop_bits must be 1, 1.5 or 2")
    if "fifo_trigger_bytes" in serial:
        integer(serial["fifo_trigger_bytes"], "fifo_trigger_bytes", 1, MAX_TRACE_BYTES)
    protocol = trace.get("protocol", {})
    if not isinstance(protocol, dict):
        raise ValueError("protocol must be an object")
    maximum = integer(protocol.get("max_frame_bytes"), "max_frame_bytes", 7, 65536)
    integer(protocol.get("host_idle_ms"), "host_idle_ms", 1, 60000)
    lengths = protocol.get("type_lengths")
    if not isinstance(lengths, dict) or not lengths:
        raise ValueError("type_lengths must be a nonempty TYPE-to-total-length object")
    seen = set()
    for key, length in lengths.items():
        kind = type_byte(key)
        if kind in seen:
            raise ValueError("duplicate TYPE after hexadecimal normalization")
        seen.add(kind)
        integer(length, "frame length", 7, maximum)
    events = trace.get("events")
    if not isinstance(events, list) or not events or len(events) > MAX_EVENTS:
        raise ValueError("events must be a nonempty bounded list")
    total = 0
    for event in events:
        if not isinstance(event, dict):
            raise ValueError("each event must be an object")
        integer(event.get("at_ms"), "at_ms")
        raw = event.get("data_hex")
        if not isinstance(raw, str):
            raise ValueError("data_hex must be hexadecimal text")
        try:
            data = bytes.fromhex(raw)
        except ValueError as exc:
            raise ValueError("invalid data_hex") from exc
        if not data:
            raise ValueError("empty deliveries are not receive events")
        total += len(data)
        if total > MAX_TRACE_BYTES:
            raise ValueError("trace exceeds byte budget")
    request = trace.get("request")
    if request is not None:
        if not isinstance(request, dict):
            raise ValueError("request must be an object")
        type_byte(request.get("response_type"))
        integer(request.get("task_id"), "task_id", 0, 65535)
        integer(request.get("sent_at_ms"), "sent_at_ms")
        integer(request.get("deadline_ms"), "deadline_ms", 1, 600000)
    return trace


def load_trace(path):
    path = Path(path)
    if path.stat().st_size > MAX_INPUT_BYTES:
        raise ValueError("input exceeds 8 MiB")
    return validate_trace(json.loads(path.read_text(encoding="utf-8")))


class Decoder:
    """Framing only: fixed AA55, TYPE, little-endian 16-bit ID, payload, CRLF."""

    def __init__(self, type_lengths, host_idle_ms, max_frame_bytes):
        self.lengths = dict(type_lengths)
        self.idle_ms = integer(host_idle_ms, "host_idle_ms", 1, 60000)
        self.maximum = integer(max_frame_bytes, "max_frame_bytes", 7, 65536)
        for kind, length in self.lengths.items():
            integer(kind, "TYPE", 0, 255)
            integer(length, "length", 7, self.maximum)
        self.buffer = bytearray()
        self.last_ms = None
        self.rejected = 0
        self.expired = 0
        self.clock_regressions = 0

    def feed(self, data, now):
        integer(now, "delivery time")
        frames = []
        for byte in data:
            if self.last_ms is not None and now < self.last_ms:
                self.clock_regressions += 1
            if self.buffer and self.last_ms is not None and (
                now < self.last_ms or now - self.last_ms > self.idle_ms
            ):
                self.buffer.clear()
                self.expired += 1
            self.last_ms = now
            if len(self.buffer) == self.maximum:
                del self.buffer[0]
                self.rejected += 1
            self.buffer.append(byte)
            while self.buffer:
                if self.buffer[0] != 0xAA:
                    del self.buffer[0]
                    continue
                if len(self.buffer) < 2:
                    break
                if self.buffer[1] != 0x55:
                    del self.buffer[0]
                    continue
                if len(self.buffer) < 3:
                    break
                size = self.lengths.get(self.buffer[2])
                if size is None:
                    self.rejected += 1
                    del self.buffer[0]
                    continue
                if len(self.buffer) < size:
                    break
                if self.buffer[size - 2:size] != b"\r\n":
                    self.rejected += 1
                    del self.buffer[0]
                    continue
                raw = bytes(self.buffer[:size])
                del self.buffer[:size]
                frames.append({
                    "type": f"{raw[2]:02X}",
                    "task_id": int.from_bytes(raw[3:5], "little"),
                    "length": size,
                    "delivery_ms": now,
                    "payload_hex": raw[5:-2].hex(),
                })
        return frames


def replay(trace, host_idle_ms=None):
    validate_trace(trace)
    protocol = trace["protocol"]
    idle = protocol["host_idle_ms"] if host_idle_ms is None else host_idle_ms
    decoder = Decoder({type_byte(k): v for k, v in protocol["type_lengths"].items()},
                      idle, protocol["max_frame_bytes"])
    frames = []
    for event in trace["events"]:
        frames.extend(decoder.feed(bytes.fromhex(event["data_hex"]), event["at_ms"]))
    matched, ignored, late = [], [], []
    request = trace.get("request")
    if request:
        for frame in frames:
            pair = frame["task_id"] == request["task_id"] and type_byte(frame["type"]) == type_byte(request["response_type"])
            if not pair or frame["delivery_ms"] < request["sent_at_ms"]:
                ignored.append(frame)
            elif frame["delivery_ms"] - request["sent_at_ms"] > request["deadline_ms"]:
                late.append(frame)
            else:
                matched.append(frame)
    return {
        "validation_scope": "framing_and_optional_type_id_deadline_only",
        "host_idle_ms": idle,
        "frames": frames,
        "matched_frames": matched,
        "ignored_frames": ignored,
        "late_frames": late,
        "summary": {"frame_count": len(frames), "rejected": decoder.rejected,
                    "expired": decoder.expired, "buffered_bytes": len(decoder.buffer),
                    "clock_regressions": decoder.clock_regressions},
    }


def analyze(trace):
    validate_trace(trace)
    serial = trace["serial"]
    bits = 1 + serial["data_bits"] + (serial["parity"] != "none") + serial["stop_bits"]
    byte_ms = 1000 * bits / serial["baud"]
    times = [event["at_ms"] for event in trace["events"]]
    gaps = [b - a for a, b in zip(times, times[1:])]
    lengths = [len(bytes.fromhex(event["data_hex"])) for event in trace["events"]]
    fifo = serial.get("fifo_trigger_bytes")
    protocol = trace["protocol"]
    return {
        "bits_per_character": bits,
        "character_wire_time_ms": byte_ms,
        "fifo_fill_estimate_ms": None if fifo is None else fifo * byte_ms,
        "frame_wire_time_ms": {k: v * byte_ms for k, v in protocol["type_lengths"].items()},
        "delivery_lengths": lengths,
        "delivery_gaps_ms": gaps,
        "max_delivery_gap_ms": max(gaps) if gaps else None,
        "host_idle_ms": protocol["host_idle_ms"],
        "delivery_gaps_exceeding_host_idle": sum(g > protocol["host_idle_ms"] for g in gaps),
        "timing_basis": "host delivery timestamps; FIFO fill is an estimate, not proof of wire gaps",
        "origin": trace.get("origin", "unspecified"),
    }


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    for name in ("analyze", "replay"):
        command = sub.add_parser(name)
        command.add_argument("trace", type=Path)
        command.add_argument("--output", type=Path)
        if name == "replay":
            command.add_argument("--host-idle-ms", type=int)
    args = parser.parse_args(argv)
    try:
        trace = load_trace(args.trace)
        result = analyze(trace) if args.command == "analyze" else replay(trace, args.host_idle_ms)
        result["input_sha256"] = hashlib.sha256(args.trace.read_bytes()).hexdigest()
        text = json.dumps(result, ensure_ascii=False, indent=2, allow_nan=False) + "\n"
        if args.output:
            if args.output.resolve() == args.trace.resolve():
                raise ValueError("output must differ from the input trace")
            args.output.write_text(text, encoding="utf-8")
        else:
            print(text, end="")
        return 0
    except (OSError, ValueError, TypeError, KeyError) as exc:
        parser.exit(2, f"uart_rx: {exc}\n")


if __name__ == "__main__":
    raise SystemExit(main())
