#!/usr/bin/env python3
"""Protocol probe: handshake + AudioChunk.

Default payload is a 440 Hz sine. That is NOT a transcription proof.
Use scripts/verify_engine.py vocals with fixtures/vocals/dry-vocal.wav.
"""

from __future__ import annotations

import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import p2p_protocol as proto


def sine_chunk(duration_sec: float = 3.5, sample_rate: float = 16000.0):
    n = int(duration_sec * sample_rate)
    freq = 440.0
    samples = [
        0.5 * math.sin(2.0 * math.pi * freq * (i / sample_rate)) for i in range(n)
    ]
    return samples, sample_rate


def main() -> int:
    print(
        "verify_transcription: sine-wave protocol probe only; "
        "not a vocal / Living Transcript proof. "
        "Use scripts/verify_engine.py vocals.",
        file=sys.stderr,
    )
    sock = None
    try:
        sock = proto.connect()
        print(f"Connected to Engine at {proto.HOST}:{proto.PORT}")
        proto.complete_handshake(sock)
        samples, sr = sine_chunk()
        proto.send_audio_chunk(sock, samples, sr)
        print(f"Sent {len(samples)} samples ({len(samples) / sr:.2f}s)")
        proto.send_transport_stop(sock, 0)
        try:
            results = proto.wait_for_results(sock, timeout=15.0)
        except TimeoutError:
            results = []
        for text, start, end, epoch in results:
            print(
                f"Received Transcription: {text!r} ({start}-{end} epoch {epoch})"
            )
        if not results:
            print(
                "No TranscriptionResult (expected for silence/sine on whisper).",
                file=sys.stderr,
            )
        print("verify_transcription: PASS protocol (not STT quality)")
        return 0
    except ConnectionRefusedError:
        print("FAIL: could not connect to Engine. Is it running?", file=sys.stderr)
        return 1
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    finally:
        if sock is not None:
            sock.close()


if __name__ == "__main__":
    raise SystemExit(main())
