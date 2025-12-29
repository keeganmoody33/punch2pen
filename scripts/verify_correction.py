import socket
import struct
import sys
import time

MESSAGE_TYPE_CORRECTION = 5
HOST = '127.0.0.1'
PORT = 7483

def send_correction(sock, original, corrected):
    orig_bytes = original.encode('utf-8')
    corr_bytes = corrected.encode('utf-8')
    orig_len = len(orig_bytes)
    corr_len = len(corr_bytes)
    
    # Payload logic:
    # 1. Main Header: Type(4) + Length(4)
    # Length = CorrectionHeader(8) + orig_str + corr_str
    
    payload_size = 8 + orig_len + corr_len
    
    # Send Main Header
    sock.sendall(struct.pack('<II', MESSAGE_TYPE_CORRECTION, payload_size))
    
    # Send Correction Header
    sock.sendall(struct.pack('<II', orig_len, corr_len))
    
    # Send Strings
    sock.sendall(orig_bytes)
    sock.sendall(corr_bytes)
    
    print(f"Sent Correction: '{original}' -> '{corrected}'")

def main():
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((HOST, PORT))
        print(f"Connected to Engine at {HOST}:{PORT}")
        
        # Send a test correction
        send_correction(sock, "punch 2 pen", "Punch2Pen")
        
        print("✅ Correction sent successfully. Check engine logs for 'Applied correction'.")
        
    except ConnectionRefusedError:
        print("❌ Could not connect to Engine. Is it running?")
    except Exception as e:
        print(f"❌ Error: {e}")
    finally:
        sock.close()

if __name__ == "__main__":
    main()
