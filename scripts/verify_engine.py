#!/usr/bin/env python3
"""Engine-side IPC checks that do not need Logic, auval, or a DAW.

Subcommands:
  smoke     Handshake + one correction (engine must already be running)
  profile   Request a ProfileStatus and assert tier / dictionary counts
  vocals    Send a real-vocal WAV and score against expected-words.txt
  selftest  Protocol struct sizes only

Vocals skip with exit 0 when the WAV is missing unless --require is set.
A 440 Hz sine is not vocals; do not treat smoke as a transcription proof.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
import wave
from pathlib import Path
from typing import List, Sequence

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))

import p2p_protocol as proto  # noqa: E402

DEFAULT_WAV = ROOT / "fixtures" / "vocals" / "dry-vocal.wav"
DEFAULT_EXPECTED = ROOT / "fixtures" / "vocals" / "expected-words.txt"
SKIP_EXIT = 0


def die(message: str, code: int = 1) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    raise SystemExit(code)


def tokens(text: str) -> List[str]:
    return [t for t in re.findall(r"[a-z0-9']+", text.lower()) if t]


def overlap_score(expected: str, actual: str) -> float:
    exp = tokens(expected)
    if not exp:
        return 0.0
    act = set(tokens(actual))
    return sum(1 for t in exp if t in act) / float(len(exp))


def cmd_selftest(_args: argparse.Namespace) -> int:
    err = proto.packed_sizes_ok()
    if err:
        die(err)
    print("selftest: PASS protocol header sizes")
    err = proto.handshake_response_required_ok()
    if err:
        die(err)
    print("selftest: PASS HandshakeResponse required before live client")
    err = proto.wait_deadline_ok()
    if err:
        die(err)
    print("selftest: PASS STT wait wall-clock deadline")
    return 0


def cmd_smoke(args: argparse.Namespace) -> int:
    original = args.original
    corrected = args.corrected
    sock = None
    try:
        sock = proto.connect(port=args.port, timeout=args.timeout)
        proto.complete_handshake(sock)
        proto.send_correction(sock, original, corrected)
    except ConnectionRefusedError:
        die(f"could not connect to 127.0.0.1:{args.port} — is punch2penEngine running?")
    except Exception as exc:
        die(str(exc))
    finally:
        if sock is not None:
            sock.close()
    print(f"Sent Correction: '{original}' -> '{corrected}'")
    print("smoke: PASS handshake + correction send")
    return 0


def cmd_profile(args: argparse.Namespace) -> int:
    """Ask the engine for its ProfileStatus over IPC.

    Free/lite must report tier=free with a session-scoped dictionary and no
    sign-in. After `smoke`, the session dictionary holds that correction.
    """
    sock = None
    try:
        sock = proto.connect(port=args.port, timeout=args.timeout)
        proto.complete_handshake(sock)
        proto.send_profile_command(sock, {"op": "status"})
        status = proto.wait_for_profile_status(sock, timeout=args.timeout)
    except ConnectionRefusedError:
        die(f"could not connect to 127.0.0.1:{args.port} — is punch2penEngine running?")
    except Exception as exc:
        die(str(exc))
    finally:
        if sock is not None:
            sock.close()

    print(json.dumps(status, indent=2, sort_keys=True))
    tier = status.get("tier")
    dictionary = status.get("dictionary") or {}
    entries = int(dictionary.get("entries", 0))
    if args.expect_tier and tier != args.expect_tier:
        die(f"expected tier {args.expect_tier!r}, engine reports {tier!r}")
    if entries < args.min_entries:
        die(f"expected at least {args.min_entries} dictionary entries, got {entries}")
    if tier == "free":
        if status.get("signedIn"):
            die("free tier must not report signedIn")
        if dictionary.get("scope") != "session":
            die("free tier dictionary must be session-scoped")
        if status.get("sync") != "local":
            die("free tier must report sync=local (no cloud)")
    print(f"profile: PASS tier={tier} entries={entries} scope={dictionary.get('scope')}")
    return 0


def load_wav_mono_float(path: Path) -> tuple[List[float], float]:
    try:
        with wave.open(str(path), "rb") as wav:
            channels = wav.getnchannels()
            width = wav.getsampwidth()
            rate = wav.getframerate()
            nframes = wav.getnframes()
            raw = wav.readframes(nframes)
    except wave.Error as exc:
        die(f"{path} is not a PCM WAV ({exc})")

    if width not in (2, 4):
        die(f"{path}: need 16-bit or 32-bit PCM WAV, got {width * 8}-bit")
    if channels < 1:
        die(f"{path}: no audio channels")

    if width == 2:
        count = len(raw) // 2
        ints = list(struct.unpack("<%dh" % count, raw))
        samples = [s / 32768.0 for s in ints]
    else:
        count = len(raw) // 4
        # 32-bit PCM integer is common from a DAW bounce; IEEE float also exists.
        unpacked = list(struct.unpack("<%di" % count, raw))
        max_abs = max((abs(v) for v in unpacked), default=1)
        if max_abs > 8:
            samples = [v / 2147483648.0 for v in unpacked]
        else:
            samples = list(struct.unpack("<%df" % count, raw))

    if channels == 1:
        mono = samples
    else:
        mono = []
        for i in range(0, len(samples) - channels + 1, channels):
            frame = samples[i : i + channels]
            mono.append(sum(frame) / float(channels))

    duration = len(mono) / float(rate) if rate else 0.0
    if duration < 3.2:
        die(
            f"{path}: {duration:.2f}s is too short; whisper waits for ~3s "
            "before a forced TransportStop finalize. Record 5–12 seconds."
        )
    return mono, float(rate)


def cmd_vocals(args: argparse.Namespace) -> int:
    wav_path = Path(args.wav)
    expected_path = Path(args.expected)
    if not wav_path.is_file():
        msg = (
            f"SKIP vocals: missing {wav_path}. "
            "Drop an original dry-vocal PCM WAV (see fixtures/vocals/README.md)."
        )
        if args.require:
            die(msg.replace("SKIP vocals: ", ""), code=1)
        print(msg)
        return SKIP_EXIT
    if not expected_path.is_file():
        msg = (
            f"SKIP vocals: missing {expected_path}. "
            "Write the words you recorded, one line, no commercial lyrics."
        )
        if args.require:
            die(msg.replace("SKIP vocals: ", ""), code=1)
        print(msg)
        return SKIP_EXIT

    expected = expected_path.read_text(encoding="utf-8").strip()
    if not tokens(expected):
        die(f"{expected_path} has no words")

    samples, rate = load_wav_mono_float(wav_path)
    sock = None
    try:
        sock = proto.connect(port=args.port, timeout=args.timeout)
        proto.complete_handshake(sock)
        chunk = max(int(rate), 16000)
        daw = 0.0
        offset = 0
        while offset < len(samples):
            piece = samples[offset : offset + chunk]
            proto.send_audio_chunk(sock, piece, rate, daw_sample_time=daw)
            daw += float(len(piece))
            offset += len(piece)
        proto.send_transport_stop(sock, 0)
        results = proto.wait_for_results(sock, timeout=args.stt_timeout)
    except ConnectionRefusedError:
        die(f"could not connect to 127.0.0.1:{args.port}")
    except Exception as exc:
        die(str(exc))
    finally:
        if sock is not None:
            sock.close()

    if not results:
        die("engine returned no TranscriptionResult (not a vocal proof)")

    actual = " ".join(text for text, _s, _e, _e2 in results).strip()
    score = overlap_score(expected, actual)
    print(f"vocals: expected={expected!r}")
    print(f"vocals: actual={actual!r}")
    print(f"vocals: token-overlap={score:.2f} (min {args.min_overlap:.2f})")
    if score < args.min_overlap:
        die(
            "token overlap below threshold. Check the WAV is dry speech you own, "
            "not a mix, and that expected-words.txt matches what you said."
        )
    print("vocals: PASS")
    return 0


def build_parser() -> argparse.ArgumentParser:
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--port", type=int, default=proto.PORT)
    common.add_argument("--timeout", type=float, default=5.0)

    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)

    selftest = sub.add_parser(
        "selftest", parents=[common], help="Check packed header sizes"
    )
    selftest.set_defaults(func=cmd_selftest)

    smoke = sub.add_parser(
        "smoke", parents=[common], help="Handshake + correction send"
    )
    smoke.add_argument("--original", default="punch 2 pen")
    smoke.add_argument("--corrected", default="Punch2Pen")
    smoke.set_defaults(func=cmd_smoke)

    profile = sub.add_parser(
        "profile", parents=[common], help="Request and check a ProfileStatus"
    )
    profile.add_argument(
        "--expect-tier", choices=("free", "paid"), default=None
    )
    profile.add_argument("--min-entries", type=int, default=0)
    profile.set_defaults(func=cmd_profile)

    vocals = sub.add_parser(
        "vocals",
        parents=[common],
        help="Golden-file STT against a dry vocal WAV",
    )
    vocals.add_argument("--wav", type=Path, default=DEFAULT_WAV)
    vocals.add_argument("--expected", type=Path, default=DEFAULT_EXPECTED)
    vocals.add_argument(
        "--require",
        action="store_true",
        help="Fail instead of skip when the vocal fixture is missing",
    )
    vocals.add_argument("--min-overlap", type=float, default=0.5)
    vocals.add_argument("--stt-timeout", type=float, default=60.0)
    vocals.set_defaults(func=cmd_vocals)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return int(args.func(args))


if __name__ == "__main__":
    raise SystemExit(main())
