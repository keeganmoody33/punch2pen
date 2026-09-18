#!/usr/bin/env python3
"""Little-endian Punch2Pen IPC helpers matching shared/Protocol.h (pack 1)."""

from __future__ import annotations

import socket
import struct
from typing import List, Optional, Tuple

MESSAGE_AUDIO = 1
MESSAGE_RESULT = 2
MESSAGE_HANDSHAKE = 3
MESSAGE_HANDSHAKE_RESPONSE = 4
MESSAGE_CORRECTION = 5
MESSAGE_TRANSPORT_STOP = 6
PROTOCOL_VERSION = 1

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
    sock.settimeout(timeout)
    results = []
    deadline_chunks = 32
    for _ in range(deadline_chunks):
        try:
            msg_type, payload = read_message(sock)
        except socket.timeout as exc:
            if results:
                break
            raise TimeoutError("timed out waiting for TranscriptionResult") from exc
        if msg_type == MESSAGE_RESULT:
            results.append(parse_transcription_result(payload))
            if len(results) >= min_results:
                # Keep draining briefly in case more segments follow.
                sock.settimeout(1.0)
                min_results = 10**9
        # Ignore other post-handshake types.
    return results


def packed_sizes_ok() -> Optional[str]:
    if AUDIO_CHUNK_HEADER.size != 24:
        return f"AudioChunkHeader size {AUDIO_CHUNK_HEADER.size} != 24"
    if RESULT_HEADER.size != 24:
        return f"TranscriptionResultHeader size {RESULT_HEADER.size} != 24"
    if TRANSPORT_STOP_HEADER.size != 4:
        return f"TransportStopHeader size {TRANSPORT_STOP_HEADER.size} != 4"
    return None
