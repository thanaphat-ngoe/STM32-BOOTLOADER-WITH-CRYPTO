import serial
import serial.tools.list_ports
import time
import struct
import sys
import os
import glob

# ==========================================
# CONFIGURATION & DEFINES
# ==========================================
AL_MSG_BOOTLOADER          = 0x01
AL_MSG_SYNC                = 0x02
AL_MSG_SENT_HEADER         = 0x03
AL_MSG_VERIFIED_HEADER     = 0x05
AL_MSG_ERASED_FLASH        = 0x06
AL_MSG_WROTE_HEADER        = 0x07
AL_MSG_RX_FW_DATA          = 0x08
AL_MSG_SUCCESS             = 0x09
AL_MSG_ABORT               = 0x0A

PACKET_CMD = 0x00
PACKET_ACK = 0xEF
PACKET_RTX = 0xFF

debug_mode = False

# ==========================================
# UTILITY FUNCTIONS
# ==========================================
def crc8(data: bytes) -> int:
    crc = 0x00
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = (crc << 1) ^ 0x07
            else:
                crc = (crc << 1)
            crc &= 0xFF
    return crc

def print_hex(prefix, data):
    if not debug_mode: return
    hex_str = ' '.join([f"{b:02X}" for b in data])
    print(f"     {prefix} {hex_str}")

def log_msg(msg):
    if debug_mode:
        print(msg)

def draw_progress_bar(current, total):
    if debug_mode: return
    width = 50
    ratio = min(1.0, current / total)
    pos = int(width * ratio)
    bar = '█' * pos + ' ' * (width - pos)
    sys.stdout.write(f"\r\033[K[AL] Progress: [{bar}] {int(ratio * 100):3d}%")
    sys.stdout.flush()

# ==========================================
# TRANSPORT LAYER & DATA LINK LAYER
# ==========================================
class TransportLayer:
    def __init__(self, port, baudrate=115200):
        try:
            self.ser = serial.Serial(port, baudrate, timeout=0.05)
            
            self.ser.dtr = True
            self.ser.rts = True
            time.sleep(0.1)
            
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            self.ser.read_all()
            
        except serial.SerialException as e:
            print(f"\n❌ Error: Cannot open serial port '{port}'.\n({e})")
            sys.exit(1)
            
        self.rx_buffer = bytearray()
        self.last_tx_packet = None

    def close(self):
        if self.ser and self.ser.is_open:
            self.ser.close()

    def build_packet(self, ptype, cmd, data=b'\xFF\xFF\xFF\xFF'):
        if len(data) < 4:
            data += b'\xFF' * (4 - len(data))
        pkt = struct.pack('<BB4s', ptype, cmd, data)
        crc = crc8(pkt)
        return pkt + bytes([crc])

    def send_packet(self, ptype, cmd, data=b'\xFF\xFF\xFF\xFF'):
        pkt = self.build_packet(ptype, cmd, data)
        if ptype == PACKET_CMD:
            self.last_tx_packet = pkt
            print_hex("[TX CMD]", pkt)
        elif ptype == PACKET_ACK:
            print_hex("[TX ACK]", pkt)
        elif ptype == PACKET_RTX:
            print_hex("[TX RTX]", pkt)
            
        self.ser.write(pkt)

    def wait_for_ack(self, timeout=1.0):
        start_time = time.time()
        while time.time() - start_time < timeout:
            if self.ser.in_waiting:
                self.rx_buffer.extend(self.ser.read(self.ser.in_waiting))
                
            while len(self.rx_buffer) >= 7:
                raw = self.rx_buffer[:7]
                
                # Sliding Window CRC Check
                if crc8(raw[:6]) != raw[6]:
                    self.rx_buffer.pop(0)
                    continue
                
                ptype = raw[0]
                print_hex("[RX RES]", raw)
                self.rx_buffer = self.rx_buffer[7:]
                
                if ptype == PACKET_ACK:
                    return 1 # OK
                if ptype == PACKET_RTX:
                    return 2 # RETX
        return 0 # Timeout

    def send_command(self, cmd, data=b'\xFF\xFF\xFF\xFF'):
        for attempt in range(5):
            self.send_packet(PACKET_CMD, cmd, data)
            res = self.wait_for_ack(timeout=1.0)
            if res == 1:
                return True
            if res == 2:
                continue
            if debug_mode:
                print(f"[TL] Timeout waiting for ACK (Attempt {attempt+1}/5)")
        if debug_mode:
            print("[TL] Error: Failed to receive ACK.")
        return False

    def wait_for_command(self, timeout=5.0):
        start_time = time.time()
        while time.time() - start_time < timeout:
            if self.ser.in_waiting:
                self.rx_buffer.extend(self.ser.read(self.ser.in_waiting))
                
            while len(self.rx_buffer) >= 7:
                raw = self.rx_buffer[:7]
                
                # Sliding Window CRC Check
                if crc8(raw[:6]) != raw[6]:
                    self.rx_buffer.pop(0)
                    continue
                
                ptype = raw[0]
                cmd = raw[1]
                data = raw[2:6]
                print_hex("[RX CMD]", raw)
                self.rx_buffer = self.rx_buffer[7:]
                
                if ptype == PACKET_CMD:
                    self.send_packet(PACKET_ACK, 0xFF)
                    return cmd, data
                elif ptype == PACKET_RTX:
                    log_msg("[TL] Device requested RETX, resending...")
                    if self.last_tx_packet:
                        self.ser.write(self.last_tx_packet)
                        print_hex("[TX RTX]", self.last_tx_packet)
        return None, None

# ==========================================
# INTERACTIVE MENUS
# ==========================================
def select_file():
    print("\n=== Available Firmware (.bin) ===")
    files = glob.glob('*.bin')
    if not files:
        print("Error: No .bin files found in current directory!")
        sys.exit(1)
        
    for i, file in enumerate(files):
        print(f" [{i+1}] {file}")
        
    while True:
        try:
            choice = int(input(f"Select file number (1-{len(files)}): "))
            if 1 <= choice <= len(files):
                return files[choice-1]
        except ValueError:
            pass
        print("Invalid choice!")

def select_port():
    print("\n=== Available Serial Ports ===")
    ports = [p.device for p in serial.tools.list_ports.comports() if 'cu.' in p.device or 'tty.' in p.device or 'COM' in p.device]
    if not ports:
        print("Error: No Serial device found!")
        sys.exit(1)
        
    for i, port in enumerate(ports):
        print(f" [{i+1}] {port}")
        
    while True:
        try:
            choice = int(input(f"Select port number (1-{len(ports)}): "))
            if 1 <= choice <= len(ports):
                return ports[choice-1]
        except ValueError:
            pass
        print("Invalid choice!")

# ==========================================
# MAIN APPLICATION
# ==========================================
def main():
    global debug_mode
    if "-debug" in sys.argv and "on" in sys.argv:
        debug_mode = True

    print("==========================================")
    print("      STM32 FIRMWARE UPDATER (PYTHON)     ")
    print("==========================================")
    print(f"Debug Mode: {'ON' if debug_mode else 'OFF'}")

    target_file = select_file()
    target_port = select_port()

    confirm = input(f"\nReady to flash '{target_file}' to '{target_port}'.\nContinue? [Y/n]: ")
    if confirm.lower() not in ['y', 'yes', '']:
        print("Update cancelled.")
        return

    try:
        with open(target_file, 'rb') as f:
            fw_data = f.read()
    except Exception as e:
        print(f"Error reading file: {e}")
        return

    file_size = len(fw_data)
    if file_size <= 256:
        print("Error: Firmware too small!")
        return

    tl = TransportLayer(target_port)
    print("\n[SYSTEM] Connected! Starting process...\n")

    try:
        # 1. BOOTLOADER_MODE
        if not debug_mode:
            sys.stdout.write("\r\033[K[SYSTEM] Waiting for Bootloader (Please press RESET on board)...")
            sys.stdout.flush()
        log_msg("[AL] Waiting for device to enter Bootloader mode...")
        
        while True:
            cmd, _ = tl.wait_for_command(timeout=1.0)
            if cmd == AL_MSG_BOOTLOADER:
                if not debug_mode:
                    sys.stdout.write("\r\033[K")
                log_msg("[AL] >>> Received: BOOTLOADER_MODE")
                break

        # 2. SYNC
        log_msg("[AL] <<< Sending: SYNCHRONIZE_MODE")
        if not tl.send_command(AL_MSG_SYNC): raise Exception("SYNC Failed")

        # 3. HEADER
        log_msg("[AL] <<< Sending Firmware Header (256 Bytes)...")
        for i in range(0, 256, 4):
            chunk = fw_data[i:i+4]
            if not tl.send_command(AL_MSG_SENT_HEADER, chunk): raise Exception("Header Send Failed")

        # 4. VERIFY HEADER
        log_msg("[AL] Waiting for Header Verification...")
        cmd, _ = tl.wait_for_command(timeout=5.0)
        if cmd == AL_MSG_VERIFIED_HEADER:
            log_msg("[AL] >>> Received: VERIFIED_NEW_FIRMWARE_HEADER")
        elif cmd == AL_MSG_ABORT:
            raise Exception("HEADER REJECTED!")
        else:
            raise Exception("Header Verify Timeout")

        # 5. ERASE FLASH
        if not debug_mode:
            sys.stdout.write("\r\033[K[AL] Erasing Flash (This may take a few seconds)...")
            sys.stdout.flush()
        log_msg("\n[AL] Waiting for Device to Erase Flash...")
        cmd, _ = tl.wait_for_command(timeout=15.0)
        if cmd == AL_MSG_ERASED_FLASH:
            log_msg("[AL] >>> Received: ERASED_FLASH")
        else:
            raise Exception("Flash Erase Failed/Timeout")

        # 6. WRITE HEADER
        log_msg("[AL] Waiting for Header Write confirmation...")
        cmd, _ = tl.wait_for_command(timeout=5.0)
        if cmd == AL_MSG_WROTE_HEADER:
            log_msg("[AL] >>> Received: WROTE_NEW_FIRMWARE_HEADER")
        else:
            raise Exception("Header Write Failed/Timeout")

        # 7. FW BODY
        body_size = file_size - 256
        if debug_mode:
            print(f"\n[AL] <<< Sending Firmware Body ({body_size} Bytes)...")
        else:
            sys.stdout.write("\r\033[K")

        last_percent = -1
        for i in range(256, file_size, 4):
            chunk = fw_data[i:i+4]
            if not tl.send_command(AL_MSG_RX_FW_DATA, chunk): raise Exception("FW Body Send Failed")
            
            progress = min(100, int(((i - 256 + len(chunk)) / body_size) * 100))
            if debug_mode:
                if progress % 10 == 0 and progress != last_percent:
                    print(f"[AL] Progress: {progress}%")
                    last_percent = progress
            else:
                draw_progress_bar(i - 256 + len(chunk), body_size)

        # 8. VERIFY SIGNATURE
        if not debug_mode:
            sys.stdout.write("\n[AL] Verifying ECDSA Signature...\n")
            sys.stdout.flush()
        log_msg("\n\n[AL] Waiting for ECDSA Signature Verification...")
        
        cmd, _ = tl.wait_for_command(timeout=60.0)
        if cmd == AL_MSG_SUCCESS:
            print("\n==========================================")
            print("🎉 UPDATE SUCCESSFUL! Restarting The Device...")
            print("==========================================")
        elif cmd == AL_MSG_ABORT:
            print("\n==========================================")
            print("❌ UPDATE ABORTED! Signature Verification Failed.")
            print("==========================================")
        else:
            print(f"\n❌ Error: Unexpected response (0x{cmd:02X} if {cmd} else Timeout)")

    except Exception as e:
        print(f"\n\n❌ COMMUNICATION FAILED OR ABORTED.\n({e})")
    finally:
        tl.close()

if __name__ == "__main__":
    main()
