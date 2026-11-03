#!/usr/bin/env python3
"""Synthesise the five built-in notification sounds (brief §46.3).

Every sample is generated from sine/square oscillators here, so the
repository never contains third-party audio. Output is 16-bit mono PCM
WAV at 44.1 kHz, which QSoundEffect plays without resampling.

    py -3.12 client/tools/gen_sounds.py
    py -3.12 client/tools/gen_sounds.py --out client/resources/sounds
"""

from __future__ import annotations

import argparse
import math
import struct
import wave
from pathlib import Path

RATE = 44100


def _envelope(index: int, total: int, attack: float = 0.012, release: float = 0.060) -> float:
    """Linear attack/release so every note starts and ends at silence."""
    t = index / RATE
    duration = total / RATE
    if t < attack:
        return t / attack
    remaining = duration - t
    if remaining < release:
        return max(0.0, remaining / release)
    return 1.0


def _mix(buffer: list[float], start: float, duration: float, frequency: float,
         amplitude: float, kind: str = "sine") -> None:
    """Adds one enveloped note to the buffer at `start` seconds."""
    first = int(start * RATE)
    count = int(duration * RATE)
    for i in range(count):
        pos = first + i
        if pos >= len(buffer):
            break
        phase = 2.0 * math.pi * frequency * (i / RATE)
        if kind == "square":
            sample = 1.0 if math.sin(phase) >= 0.0 else -1.0
        elif kind == "triangle":
            sample = 2.0 / math.pi * math.asin(math.sin(phase))
        else:
            sample = math.sin(phase)
        buffer[pos] += sample * amplitude * _envelope(i, count)


def _write(path: Path, samples: list[float]) -> None:
    peak = max((abs(value) for value in samples), default=0.0)
    scale = (0.85 / peak) if peak > 0.0 else 0.0
    frames = bytearray()
    for value in samples:
        clipped = max(-1.0, min(1.0, value * scale))
        frames += struct.pack("<h", int(round(clipped * 32767)))
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(RATE)
        handle.writeframes(bytes(frames))


def msg_in() -> list[float]:
    """Incoming message: two quick rising notes."""
    buffer = [0.0] * int(0.24 * RATE)
    _mix(buffer, 0.000, 0.095, 784.0, 0.55)
    _mix(buffer, 0.105, 0.130, 1174.7, 0.55)
    return buffer


def msg_out() -> list[float]:
    """Outgoing message: one short, softer falling blip."""
    buffer = [0.0] * int(0.16 * RATE)
    _mix(buffer, 0.000, 0.070, 659.3, 0.40)
    _mix(buffer, 0.060, 0.095, 523.3, 0.35)
    return buffer


def mention() -> list[float]:
    """Mention: three-note motif, clearly different from msg_in."""
    buffer = [0.0] * int(0.46 * RATE)
    _mix(buffer, 0.000, 0.110, 1046.5, 0.50, "triangle")
    _mix(buffer, 0.115, 0.110, 1318.5, 0.50, "triangle")
    _mix(buffer, 0.230, 0.170, 1568.0, 0.55, "triangle")
    return buffer


def ring() -> list[float]:
    """Call ringtone: classic dual-tone ring cadence."""
    buffer = [0.0] * int(2.0 * RATE)
    for burst in (0.0, 1.0):
        first = int(burst * RATE)
        count = int(0.40 * RATE)
        for i in range(count):
            pos = first + i
            if pos >= len(buffer):
                break
            envelope = _envelope(i, count, attack=0.020, release=0.060)
            phase_a = 2.0 * math.pi * 440.0 * (i / RATE)
            phase_b = 2.0 * math.pi * 480.0 * (i / RATE)
            buffer[pos] += (math.sin(phase_a) + math.sin(phase_b)) * 0.28 * envelope
    return buffer


def error() -> list[float]:
    """Error: two descending square beeps."""
    buffer = [0.0] * int(0.40 * RATE)
    _mix(buffer, 0.000, 0.130, 233.1, 0.35, "square")
    _mix(buffer, 0.160, 0.180, 174.6, 0.35, "square")
    return buffer


SOUNDS = {
    "msg_in.wav": msg_in,
    "msg_out.wav": msg_out,
    "mention.wav": mention,
    "ring.wav": ring,
    "error.wav": error,
}


def main() -> int:
    default_out = Path(__file__).resolve().parent.parent / "resources" / "sounds"
    parser = argparse.ArgumentParser(description="Generate MINI notification WAVs")
    parser.add_argument("--out", type=Path, default=default_out,
                        help=f"output directory (default: {default_out})")
    args = parser.parse_args()

    args.out.mkdir(parents=True, exist_ok=True)
    for name, builder in SOUNDS.items():
        target = args.out / name
        _write(target, builder())
        print(f"{target} ({target.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
