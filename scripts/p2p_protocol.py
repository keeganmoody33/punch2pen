#!/usr/bin/env python3
"""Little-endian Punch2Pen IPC helpers matching shared/Protocol.h (pack 1)."""

from __future__ import annotations

import json
import socket
import struct
import threading
import time
from typing import Callable, List, Optional, Tuple

MESSAGE_AUDIO = 1
MESSAGE_RESULT = 2
MESSAGE_HANDSHAKE = 3
MESSAGE_HANDSHAKE_RESPONSE = 4
MESSAGE_CORRECTION = 5
MESSAGE_TRANSPORT_STOP = 6
MESSAGE_PROFILE_COMMAND = 7
MESSAGE_PROFILE_STATUS = 8
PROTOCOL_VERSION = 1
MAX_JSON_PAYLOAD = 256 * 1024

HOST = "127.0.0.1"
PORT = 7483

# AudioChunkHeader: double, uint32, double, uint32
AUDIO_CHUNK_HEADER = struct.Struct("<dIdI")
RESULT_HEADER = struct.Struct("<IddI")
TRANSPORT_STOP_HEADER = struct.Struct("<I")
HANDSHAKE = struct.Struct("<I")
HANDSHAKE_RESPONSE = struct.Struct("<II")
CORRECTION_HEADER = struct.Struct("<II")
MESSAGE_HEADER = struct.Struct("<II")


def recv_exact(sock: socket.socket, nbytes: int) -> bytes:
    buf = bytearray()
    while len(buf) < nbytes:
        chunk = sock.recv(nbytes - len(buf))
        if not chunk:
            raise RuntimeError(
                f"connection closed after {len(buf)}/{nbytes} bytes"
            )
        buf.extend(chunk)
    return bytes(buf)


def connect(host: str = HOST, port: int = PORT, timeout: float = 5.0) -> socket.socket:
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(timeout)
    sock.connect((host, port))
    return sock


def complete_handshake(sock: socket.socket) -> None:
    sock.sendall(MESSAGE_HEADER.pack(MESSAGE_HANDSHAKE, HANDSHAKE.size))
    sock.sendall(HANDSHAKE.pack(PROTOCOL_VERSION))
    header = recv_exact(sock, MESSAGE_HEADER.size)
    msg_type, length = MESSAGE_HEADER.unpack(header)
    if msg_type != MESSAGE_HANDSHAKE_RESPONSE or length != HANDSHAKE_RESPONSE.size:
        raise RuntimeError(
            f"handshake: unexpected type {msg_type} length {length}"
        )
    version, accepted = HANDSHAKE_RESPONSE.unpack(recv_exact(sock, length))
    if accepted != 1 or version != PROTOCOL_VERSION:
        raise RuntimeError(
            f"handshake rejected (version={version} accepted={accepted})"
        )


def send_correction(sock: socket.socket, original: str, corrected: str) -> None:
    orig_b = original.encode("utf-8")
    corr_b = corrected.encode("utf-8")
    payload = CORRECTION_HEADER.size + len(orig_b) + len(corr_b)
    sock.sendall(MESSAGE_HEADER.pack(MESSAGE_CORRECTION, payload))
    sock.sendall(CORRECTION_HEADER.pack(len(orig_b), len(corr_b)))
    sock.sendall(orig_b + corr_b)


def send_audio_chunk(
    sock: socket.socket,
    samples: List[float],
    sample_rate: float,
    daw_sample_time: float = 0.0,
    capture_epoch: int = 0,
) -> None:
    num = len(samples)
    payload = AUDIO_CHUNK_HEADER.size + (num * 4)
    sock.sendall(MESSAGE_HEADER.pack(MESSAGE_AUDIO, payload))
    sock.sendall(
        AUDIO_CHUNK_HEADER.pack(sample_rate, num, daw_sample_time, capture_epoch)
    )
    sock.sendall(struct.pack("<%df" % num, *samples))


def send_transport_stop(sock: socket.socket, capture_epoch: int = 0) -> None:
    sock.sendall(
        MESSAGE_HEADER.pack(MESSAGE_TRANSPORT_STOP, TRANSPORT_STOP_HEADER.size)
    )
    sock.sendall(TRANSPORT_STOP_HEADER.pack(capture_epoch))


def send_profile_command(sock: socket.socket, command: dict) -> None:
    """ProfileCommand: header + raw UTF-8 JSON, no sub-header."""
    payload = json.dumps(command, separators=(",", ":")).encode("utf-8")
    if len(payload) > MAX_JSON_PAYLOAD:
        raise ValueError("profile command too large")
    sock.sendall(MESSAGE_HEADER.pack(MESSAGE_PROFILE_COMMAND, len(payload)))
    sock.sendall(payload)


def wait_for_profile_status(sock: socket.socket, timeout: float) -> dict:
    """Return the next ProfileStatus JSON document, skipping other types."""
    sock.settimeout(timeout)
    for _ in range(64):
        msg_type, payload = read_message(sock)
        if msg_type == MESSAGE_PROFILE_STATUS:
            doc = json.loads(payload.decode("utf-8"))
            if not isinstance(doc, dict) or doc.get("type") != "profileStatus":
                raise RuntimeError("ProfileStatus payload is not a status document")
            return doc
    raise TimeoutError("no ProfileStatus among the last 64 messages")


def read_message(
    sock: socket.socket,
) -> Tuple[int, bytes]:
    header = recv_exact(sock, MESSAGE_HEADER.size)
    msg_type, length = MESSAGE_HEADER.unpack(header)
    payload = recv_exact(sock, length) if length else b""
    return msg_type, payload


def parse_transcription_result(payload: bytes) -> Tuple[str, float, float, int]:
    if len(payload) < RESULT_HEADER.size:
        raise RuntimeError("transcription result shorter than header")
    text_len, start, end, epoch = RESULT_HEADER.unpack(
        payload[: RESULT_HEADER.size]
    )
    text = payload[RESULT_HEADER.size : RESULT_HEADER.size + text_len]
    if len(text) != text_len:
        raise RuntimeError("transcription result truncated text")
    return text.decode("utf-8", errors="replace"), start, end, epoch


def wait_for_results(
    sock: socket.socket, timeout: float, min_results: int = 1
) -> List[Tuple[str, float, float, int]]:
    """Wait until `timeout` seconds of wall clock elapse or results arrive.

    `timeout` is a single deadline (time.monotonic), not a per-recv budget.
    A silent engine must fail in ~timeout seconds, not N serial recvs.
    """
    deadline = time.monotonic() + max(float(timeout), 0.0)
    results: List[Tuple[str, float, float, int]] = []
    draining = False
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            break
        if draining:
            remaining = min(remaining, 1.0)
        sock.settimeout(remaining)
        try:
            msg_type, payload = read_message(sock)
        except (TimeoutError, socket.timeout):
            break
        except RuntimeError:
            if results:
                break
            raise
        if msg_type != MESSAGE_RESULT:
            continue
        results.append(parse_transcription_result(payload))
        if len(results) >= min_results:
            # Drain trailing segments, still capped by the same deadline.
            draining = True
    if not results:
        raise TimeoutError("timed out waiting for TranscriptionResult")
    return results


def _bind_listener() -> socket.socket:
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((HOST, 0))
    server.listen(5)
    return server


def _reply_handshake(conn: socket.socket, version: int) -> None:
    accepted = 1 if version == PROTOCOL_VERSION else 0
    body = HANDSHAKE_RESPONSE.pack(PROTOCOL_VERSION, accepted)
    conn.sendall(MESSAGE_HEADER.pack(MESSAGE_HANDSHAKE_RESPONSE, len(body)) + body)


def _read_client_handshake(conn: socket.socket) -> int:
    """Read frames until Handshake. Earlier frames stay on this socket."""
    conn.settimeout(2.0)
    while True:
        msg_type, payload = read_message(conn)
        if msg_type == MESSAGE_HANDSHAKE:
            if len(payload) != HANDSHAKE.size:
                raise RuntimeError("handshake payload size")
            (version,) = HANDSHAKE.unpack(payload)
            return int(version)


def handshake_response_required_ok() -> Optional[str]:
    """Client is live only after a HandshakeResponse frame.

    TCP accept with no HandshakeResponse is the v1.0.3 WAIT outcome
    (engine log: client connected, HandshakeResponse absent). That must
    fail. An early non-handshake frame must not close the socket. A
    rejected handshake must leave the same listener up — no second bind.
    """
    held = threading.Event()

    def accept_and_hold(server: socket.socket) -> None:
        conn, _addr = server.accept()
        try:
            held.wait(timeout=2.0)
        finally:
            conn.close()

    silent = _bind_listener()
    silent_port = silent.getsockname()[1]
    silent_thread = threading.Thread(
        target=accept_and_hold, args=(silent,), daemon=True
    )
    silent_thread.start()
    try:
        sock = connect(port=silent_port, timeout=0.4)
        try:
            try:
                complete_handshake(sock)
            except Exception:
                pass
            else:
                return (
                    "TCP accept without HandshakeResponse was treated as a "
                    "live client; the plugin stays on WAIT"
                )
        finally:
            sock.close()
    finally:
        held.set()
        silent.close()

    def transcript_first(server: socket.socket) -> None:
        conn, _addr = server.accept()
        try:
            text = b"lyric"
            payload = RESULT_HEADER.pack(len(text), 0.0, 1.0, 1) + text
            conn.sendall(MESSAGE_HEADER.pack(MESSAGE_RESULT, len(payload)) + payload)
            time.sleep(0.3)
        except OSError:
            pass
        finally:
            conn.close()

    early_tx = _bind_listener()
    early_port = early_tx.getsockname()[1]
    threading.Thread(target=transcript_first, args=(early_tx,), daemon=True).start()
    try:
        sock = connect(port=early_port, timeout=1.0)
        try:
            try:
                complete_handshake(sock)
            except Exception:
                pass
            else:
                return "transcript frame before HandshakeResponse was accepted"
        finally:
            sock.close()
    finally:
        early_tx.close()

    def same_listener(server: socket.socket) -> None:
        handled = 0
        while handled < 2:
            conn, _addr = server.accept()
            try:
                conn.settimeout(2.0)
                while True:
                    msg_type, payload = read_message(conn)
                    if msg_type != MESSAGE_HANDSHAKE:
                        continue
                    if len(payload) != HANDSHAKE.size:
                        return
                    (version,) = HANDSHAKE.unpack(payload)
                    _reply_handshake(conn, int(version))
                    if int(version) == PROTOCOL_VERSION:
                        handled += 1
                        break
            except (OSError, RuntimeError, TimeoutError):
                pass
            finally:
                conn.close()

    listener = _bind_listener()
    listener_port = listener.getsockname()[1]
    threading.Thread(target=same_listener, args=(listener,), daemon=True).start()
    try:
        first = connect(port=listener_port, timeout=1.0)
        try:
            send_profile_command(first, {"op": "status"})
            first.sendall(MESSAGE_HEADER.pack(MESSAGE_HANDSHAKE, HANDSHAKE.size))
            first.sendall(HANDSHAKE.pack(0))
            msg_type, payload = read_message(first)
            if msg_type != MESSAGE_HANDSHAKE_RESPONSE or len(payload) != HANDSHAKE_RESPONSE.size:
                return (
                    "early non-handshake frame closed the socket or skipped "
                    "HandshakeResponse"
                )
            _version, accepted = HANDSHAKE_RESPONSE.unpack(payload)
            if accepted != 0:
                return "rejected handshake was treated as a live client"
            first.sendall(MESSAGE_HEADER.pack(MESSAGE_HANDSHAKE, HANDSHAKE.size))
            first.sendall(HANDSHAKE.pack(PROTOCOL_VERSION))
            msg_type, payload = read_message(first)
            if msg_type != MESSAGE_HANDSHAKE_RESPONSE:
                return "HandshakeResponse was not the frame that completed handshake"
            _version, accepted = HANDSHAKE_RESPONSE.unpack(payload)
            if accepted != 1:
                return "completed handshake was not accepted"
        finally:
            first.close()

        # Do not bind again. The retry uses the listener from the failure.
        second = connect(port=listener_port, timeout=1.0)
        try:
            complete_handshake(second)
        except Exception as exc:
            return (
                "failed handshake required a second bind; retry on the same "
                f"listener failed: {exc}"
            )
        finally:
            second.close()
    finally:
        listener.close()
    return None


def packed_sizes_ok() -> Optional[str]:
    if AUDIO_CHUNK_HEADER.size != 24:
        return f"AudioChunkHeader size {AUDIO_CHUNK_HEADER.size} != 24"
    if RESULT_HEADER.size != 24:
        return f"TranscriptionResultHeader size {RESULT_HEADER.size} != 24"
    if TRANSPORT_STOP_HEADER.size != 4:
        return f"TransportStopHeader size {TRANSPORT_STOP_HEADER.size} != 4"
    return None


def _serve_once(handler: Callable[[socket.socket], None]) -> socket.socket:
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((HOST, 0))
    server.listen(1)
    thread = threading.Thread(target=handler, args=(server,), daemon=True)
    thread.start()
    return server


def wait_deadline_ok(bound: float = 0.35) -> Optional[str]:
    """Fail if wait_for_results can run many serial recvs instead of one deadline."""

    def silent(server: socket.socket) -> None:
        conn, _unused = server.accept()
        try:
            time.sleep(bound + 1.0)
        except OSError:
            pass
        finally:
            conn.close()

    stop_flood = threading.Event()

    def flood_ignored(server: socket.socket) -> None:
        conn, _unused = server.accept()
        payload = HANDSHAKE.pack(PROTOCOL_VERSION)
        packet = MESSAGE_HEADER.pack(MESSAGE_HANDSHAKE, len(payload)) + payload
        try:
            while not stop_flood.is_set():
                conn.sendall(packet)
                time.sleep(0.04)
        except OSError:
            pass
        finally:
            conn.close()

    def timed_wait(server: socket.socket) -> tuple[float, Optional[int], Optional[Exception]]:
        sock = connect(port=server.getsockname()[1], timeout=1.0)
        start = time.monotonic()
        count: Optional[int] = None
        err: Optional[Exception] = None
        try:
            count = len(wait_for_results(sock, timeout=bound))
        except Exception as exc:
            err = exc
        elapsed = time.monotonic() - start
        sock.close()
        return elapsed, count, err

    silent_server = _serve_once(silent)
    try:
        elapsed, count, err = timed_wait(silent_server)
    finally:
        silent_server.close()
    if not isinstance(err, TimeoutError):
        return (
            f"silent peer: expected TimeoutError within {bound:.2f}s, "
            f"got results={count} err={err!r} after {elapsed:.2f}s"
        )
    if elapsed > bound + 0.8:
        return (
            f"silent peer: wait_for_results took {elapsed:.2f}s for timeout={bound:.2f}s "
            "(must be one wall-clock deadline, not 32 serial recvs)"
        )

    flood_server = _serve_once(flood_ignored)
    try:
        elapsed, count, err = timed_wait(flood_server)
    finally:
        stop_flood.set()
        flood_server.close()
    if not isinstance(err, TimeoutError):
        return (
            "ignored-message flood: expected TimeoutError at the deadline, "
            f"got results={count} err={err!r} after {elapsed:.2f}s"
        )
    if elapsed > bound + 0.8:
        return (
            f"ignored-message flood: wait_for_results took {elapsed:.2f}s "
            f"for timeout={bound:.2f}s"
        )

    stop_result = threading.Event()

    def one_result(server: socket.socket) -> None:
        conn, _unused = server.accept()
        text = b"check"
        payload = RESULT_HEADER.pack(len(text), 0.0, 1.0, 0) + text
        try:
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            conn.sendall(MESSAGE_HEADER.pack(MESSAGE_RESULT, len(payload)) + payload)
            stop_result.wait(timeout=2.0)
        except OSError:
            pass
        finally:
            conn.close()

    result_server = _serve_once(one_result)
    try:
        elapsed, count, err = timed_wait(result_server)
    finally:
        stop_result.set()
        result_server.close()
    if err is not None or count != 1:
        return (
            f"one TranscriptionResult: expected 1 result, "
            f"got results={count} err={err!r} after {elapsed:.2f}s"
        )
    return None
