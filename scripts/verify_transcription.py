import socket
import struct
import time
import sys
import math

# Protocol Constants
MESSAGE_TYPE_AUDIO = 1
MESSAGE_TYPE_RESULT = 2
MESSAGE_TYPE_HANDSHAKE = 3
MESSAGE_TYPE_HANDSHAKE_RESPONSE = 4
PROTOCOL_VERSION = 1

HOST = '127.0.0.1'
PORT = 7483

def complete_handshake(sock):
    sock.sendall(struct.pack('<II', MESSAGE_TYPE_HANDSHAKE, 4))
    sock.sendall(struct.pack('<I', PROTOCOL_VERSION))
    header = sock.recv(8)
    if len(header) != 8:
        raise RuntimeError('handshake: no response header')
    msg_type, length = struct.unpack('<II', header)
    payload = sock.recv(length)
    if msg_type != MESSAGE_TYPE_HANDSHAKE_RESPONSE or len(payload) < 8:
        raise RuntimeError(f'handshake: unexpected type {msg_type}')
    version, accepted = struct.unpack_from('<II', payload)
    if accepted != 1 or version != PROTOCOL_VERSION:
        raise RuntimeError(
            f'handshake rejected (version={version} accepted={accepted})')

def create_audio_chunk(duration_sec=1.0, sample_rate=16000.0):
    """Generates a simple sine wave audio chunk."""
    num_samples = int(duration_sec * sample_rate)
    samples = []
    frequency = 440.0
    
    # Generate 1 second of audio (Sine wave) - Silence might be filtered by VAD if we had one
    # But Whisper works on silence too (mostly hallucinations or empty)
    # Let's simple generate valid float data
    for i in range(num_samples):
        t = float(i) / sample_rate
        sample = 0.5 * math.sin(2 * math.pi * frequency * t)
        samples.append(sample)
        
    return samples, sample_rate

def send_audio(sock, samples, sample_rate, daw_sample_time=0.0, capture_epoch=0):
    num_samples = len(samples)
    # AudioChunkHeader: sampleRate d, numSamples I, dawSampleTime d, captureEpoch I
    payload_size = 24 + (num_samples * 4)

    sock.sendall(struct.pack('<II', MESSAGE_TYPE_AUDIO, payload_size))
    sock.sendall(struct.pack('<dIdI', sample_rate, num_samples, daw_sample_time,
                             capture_epoch))

    packed_samples = struct.pack('<%df' % num_samples, *samples)
    sock.sendall(packed_samples)

    print(f"Sent {num_samples} samples ({num_samples/sample_rate:.2f}s)")

def main():
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((HOST, PORT))
        print(f"Connected to Engine at {HOST}:{PORT}")
        complete_handshake(sock)

        # 1. Send 3 seconds of audio to trigger simple VAD/Transcription thresholds
        # The engine logic waits for >2-3 seconds typically in the simple loop we wrote
        # Transcriber.cpp: if (audioBuffer.size() < WHISPER_SAMPLE_RATE * 3)
        duration = 3.5 
        samples, sr = create_audio_chunk(duration)
        send_audio(sock, samples, sr)
        
        print("Waiting for response...")
        sock.settimeout(10.0) # Wait up to 10s for transcription
        
        while True:
            # Read Header
            header_data = sock.recv(8) 
            if not header_data:
                break
                
            msg_type, length = struct.unpack('<II', header_data)
            
            if msg_type == MESSAGE_TYPE_RESULT:
                # textLength(u32), start(d), end(d), captureEpoch(u32)
                result_header = sock.recv(24)
                text_len, start, end, capture_epoch = struct.unpack('<IddI', result_header)
                
                # Read Text
                text_bytes = sock.recv(text_len)
                text = text_bytes.decode('utf-8')
                
                print(f"✅ Received Transcription: '{text}' ({start}-{end} epoch {capture_epoch})")
                break
            else:
                print(f"Received unknown message type: {msg_type}")
                # Skip payload if any
                if length > 0:
                   sock.recv(length)

    except ConnectionRefusedError:
        print("❌ Could not connect to Engine. Is it running?")
    except socket.timeout:
        print("❌ Timed out waiting for response.")
    except Exception as e:
        print(f"❌ Error: {e}")
    finally:
        sock.close()

if __name__ == "__main__":
    main()
