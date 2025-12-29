import socket
import struct
import time
import sys
import math

# Protocol Constants
MESSAGE_TYPE_AUDIO = 1
MESSAGE_TYPE_RESULT = 2

HOST = '127.0.0.1'
PORT = 7483

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

def send_audio(sock, samples, sample_rate):
    num_samples = len(samples)
    payload_size = 12 + (num_samples * 4) # 8 bytes double + 4 bytes uint32 + samples
    
    # Send Main Header
    # MessageType (uint32), Length (uint32)
    sock.sendall(struct.pack('<II', MESSAGE_TYPE_AUDIO, payload_size))
    
    # Send AudioChunkHeader
    # sampleRate (double), numSamples (uint32)
    sock.sendall(struct.pack('<dI', sample_rate, num_samples))
    
    # Send Samples (float32 array)
    # We use 'f' for float which is 4 bytes
    packed_samples = struct.pack('<%df' % num_samples, *samples)
    sock.sendall(packed_samples)
    
    print(f"Sent {num_samples} samples ({num_samples/sample_rate:.2f}s)")

def main():
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((HOST, PORT))
        print(f"Connected to Engine at {HOST}:{PORT}")
        
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
                # Read Result Header
                # textLength(u32), start(d), end(d) = 4 + 8 + 8 = 20 bytes
                result_header = sock.recv(20)
                text_len, start, end = struct.unpack('<Idd', result_header)
                
                # Read Text
                text_bytes = sock.recv(text_len)
                text = text_bytes.decode('utf-8')
                
                print(f"✅ Received Transcription: '{text}'")
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
