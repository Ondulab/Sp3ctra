#!/usr/bin/env python3
"""
UDP Listener Test Tool
Tests UDP reception on a specific address:port to debug Sp3ctra connectivity
"""

import socket
import struct
import sys
import time

def listen_udp(address, port, timeout=30):
    """Listen for UDP packets on specified address:port"""
    
    print(f"UDP Listener Test Tool")
    print(f"=" * 60)
    print(f"Target: {address}:{port}")
    print(f"Timeout: {timeout}s")
    print(f"=" * 60)
    
    # Create UDP socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    
    # Bind to port
    sock.bind(('', port))
    print(f"✓ Socket bound to port {port}")
    
    # Check if multicast
    if address.startswith('239.'):
        # Join multicast group
        mreq = struct.pack('4sl', socket.inet_aton(address), socket.INADDR_ANY)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
        print(f"✓ Joined multicast group {address}")
    else:
        print(f"ℹ Unicast mode - listening for packets to {address}")
    
    # Set timeout
    sock.settimeout(timeout)
    
    print(f"\n🎧 Listening for UDP packets...")
    print(f"Press Ctrl+C to stop\n")
    
    packet_count = 0
    start_time = time.time()
    
    try:
        while True:
            try:
                data, addr = sock.recvfrom(65536)
                packet_count += 1
                elapsed = time.time() - start_time
                
                # Parse header if IMAGE_DATA packet
                if len(data) >= 8:
                    header = struct.unpack('BBHBBH', data[:8])
                    packet_type = header[0]
                    
                    if packet_type == 0x12:  # IMAGE_DATA_HEADER
                        color = header[1]  # 0=R, 1=G, 2=B
                        fragment = header[3]
                        color_names = ['R', 'G', 'B']
                        color_str = color_names[color] if color < 3 else '?'
                        
                        print(f"[{elapsed:6.2f}s] Packet #{packet_count:4d} from {addr[0]}:{addr[1]} - "
                              f"IMAGE_DATA ({color_str}, frag {fragment:2d}) - {len(data):5d} bytes")
                    else:
                        print(f"[{elapsed:6.2f}s] Packet #{packet_count:4d} from {addr[0]}:{addr[1]} - "
                              f"Type 0x{packet_type:02X} - {len(data):5d} bytes")
                else:
                    print(f"[{elapsed:6.2f}s] Packet #{packet_count:4d} from {addr[0]}:{addr[1]} - "
                          f"{len(data):5d} bytes")
                
            except socket.timeout:
                elapsed = time.time() - start_time
                print(f"\n⏱ Timeout after {elapsed:.1f}s - No packets received")
                if packet_count == 0:
                    print(f"\n❌ NO PACKETS RECEIVED!")
                    print(f"\nPossible issues:")
                    print(f"  1. Sp3ctra hardware not sending to {address}:{port}")
                    print(f"  2. Firewall blocking UDP port {port}")
                    print(f"  3. Wrong network interface")
                    print(f"  4. Sp3ctra hardware network configuration mismatch")
                break
                
    except KeyboardInterrupt:
        elapsed = time.time() - start_time
        print(f"\n\n⏹ Stopped by user")
    
    finally:
        sock.close()
        print(f"\n" + "=" * 60)
        print(f"📊 Statistics:")
        print(f"  Total packets: {packet_count}")
        print(f"  Duration: {elapsed:.1f}s")
        if elapsed > 0:
            print(f"  Rate: {packet_count/elapsed:.1f} packets/s")
        print(f"=" * 60)
        
        if packet_count > 0:
            print(f"✅ SUCCESS - UDP reception working!")
        else:
            print(f"❌ FAILURE - No packets received")
            return 1
    
    return 0

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 udp_listener_test.py <address> <port> [timeout]")
        print("\nExamples:")
        print("  python3 udp_listener_test.py 239.100.100.100 55151")
        print("  python3 udp_listener_test.py 192.168.100.10 55151 60")
        sys.exit(1)
    
    address = sys.argv[1]
    port = int(sys.argv[2])
    timeout = int(sys.argv[3]) if len(sys.argv) > 3 else 30
    
    sys.exit(listen_udp(address, port, timeout))
