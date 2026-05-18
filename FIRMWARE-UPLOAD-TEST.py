import serial
import serial.tools.list_ports
import struct
import time
import sys

# ==========================================================
# CONSTANTS & CONFIGURATION (Matches main.h & transport-layer.h)
# ==========================================================
PACKET_DATA_BYTE_SIZE = 32
PACKET_LENGTH = 36

PACKET_RETX = 0xFF
PACKET_ACK  = 0xEF
PACKET_NONE = 0xDF

AL_MESSAGE_SEQUENCE_OBSERVED                 = 0x01
AL_MESSAGE_FIRMWARE_UPDATE_REQUEST           = 0x02
AL_MESSAGE_SENT_CURRENT_FIRMWARE_VERSION     = 0x03
AL_MESSAGE_SENT_NEW_FIRMWARE_HEADER_DATA     = 0x04
AL_MESSAGE_RECEIVED_NEW_FIRMWARE_HEADER_DATA = 0x05
AL_MESSAGE_FIRMWARE_HEADER_WRITTEN           = 0x06
AL_MESSAGE_RECEIVED_NEW_FIRMWARE_DATA        = 0x07
AL_MESSAGE_UPDATE_SUCCESSFUL                 = 0x08
AL_MESSAGE_NACK                              = 0x09

class Colors:
    TL_TX = '\033[94m' # Blue
    TL_RX = '\033[96m' # Cyan
    AL    = '\033[92m' # Green
    ERR   = '\033[91m' # Red
    RST   = '\033[0m'  # Reset

# ==========================================================
# CRC-8 
# ==========================================================
def calc_crc8(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc

# ==========================================================
# RING BUFFER 
# ==========================================================
class RingBuffer:
    def __init__(self, size=1024):
        assert (size & (size - 1)) == 0, "Size must be a power of 2"
        self.buffer = bytearray(size)
        self.mask = size - 1
        self.read_index = 0
        self.write_index = 0

    def is_empty(self):
        return self.read_index == self.write_index

    def write(self, byte):
        local_write_index = self.write_index
        local_read_index = self.read_index
        next_write_index = (local_write_index + 1) & self.mask
        
        if next_write_index == local_read_index:
            return False # Buffer Full

        self.buffer[local_write_index] = byte
        self.write_index = next_write_index
        return True

    def read(self):
        local_read_index = self.read_index
        local_write_index = self.write_index

        if local_read_index == local_write_index:
            return None # Buffer Empty

        byte = self.buffer[local_read_index]
        self.read_index = (local_read_index + 1) & self.mask
        return byte

# ==========================================================
# TRANSPORT LAYER PACKET DEFINITION
# ==========================================================
class TL_Packet:
    def __init__(self):
        self.packet_data_size = 0
        self.packet_type = 0
        self.packet_message_type = 0
        self.data = bytearray(PACKET_DATA_BYTE_SIZE)
        self.packet_crc = 0

    def pack_to_bytes(self):
        buf = struct.pack('<BBB', self.packet_data_size, self.packet_type, self.packet_message_type)
        buf += self.data
        return buf
        
    def compute_crc(self):
        return calc_crc8(self.pack_to_bytes())

    def to_raw_packet(self):
        return self.pack_to_bytes() + bytes([self.packet_crc])

    def clone(self):
        new_pkt = TL_Packet()
        new_pkt.packet_data_size = self.packet_data_size
        new_pkt.packet_type = self.packet_type
        new_pkt.packet_message_type = self.packet_message_type
        new_pkt.data = bytearray(self.data)
        new_pkt.packet_crc = self.packet_crc
        return new_pkt

    def is_retx(self):
        return (self.packet_data_size == 0 and 
                self.packet_type == PACKET_RETX and 
                self.packet_message_type == 0)

    def is_ack(self):
        return (self.packet_data_size == 0 and 
                self.packet_type == PACKET_ACK and 
                self.packet_message_type == 0)

# ==========================================================
# TRANSPORT LAYER 
# ==========================================================
class TL_State:
    Packet_Data_Size    = 0
    Packet_Type         = 1
    Packet_Message_Type = 2
    Data                = 3
    Packet_CRC          = 4

class TransportLayer:
    def __init__(self, port, baudrate=115200):
        self.ser = serial.Serial(port, baudrate, timeout=0)
        self.ring_buffer = RingBuffer(size=2048)
        
        self.state = TL_State.Packet_Data_Size
        self.data_byte_count = 0
        self.temp_packet = TL_Packet()
        self.last_transmitted_packet = TL_Packet()
        self.packet_buffer = []
        
        self.retx_packet = TL_Packet()
        self._create_retx_packet(self.retx_packet)
        self.ack_packet = TL_Packet()
        self._create_ack_packet(self.ack_packet)

    def _create_retx_packet(self, pkt):
        pkt.packet_data_size = 0
        pkt.packet_type = PACKET_RETX
        pkt.packet_message_type = 0
        for i in range(PACKET_DATA_BYTE_SIZE): pkt.data[i] = 0xFF
        pkt.packet_crc = pkt.compute_crc()

    def _create_ack_packet(self, pkt):
        pkt.packet_data_size = 0
        pkt.packet_type = PACKET_ACK
        pkt.packet_message_type = 0
        for i in range(PACKET_DATA_BYTE_SIZE): pkt.data[i] = 0xFF
        pkt.packet_crc = pkt.compute_crc()

    def HAL_UART_RxCpltCallback(self):
        if self.ser.in_waiting:
            raw_data = self.ser.read(self.ser.in_waiting)
            for byte in raw_data:
                self.ring_buffer.write(byte)

    def TL_Write(self, pkt: TL_Packet):
        raw_bytes = pkt.to_raw_packet()
        self.ser.write(raw_bytes)
        self.ser.flush()
        
        self.last_transmitted_packet = pkt.clone()

        hex_str = ' '.join(f'{b:02X}' for b in raw_bytes)
        if pkt.packet_type == PACKET_NONE:
            print(f"{Colors.TL_TX}[TL TX] DATA (MsgType: 0x{pkt.packet_message_type:02X}) | PKT: {hex_str}{Colors.RST}")
        elif pkt.packet_type == PACKET_RETX:
            print(f"{Colors.ERR}[TL TX] RETX | PKT: {hex_str}{Colors.RST}")
            
        # CRITICAL BACKOFF: Gives STM32 time to re-enable IT interrupts between packets to prevent ORE.
        time.sleep(0.01) 

    def TL_Update(self):
        while not self.ring_buffer.is_empty():
            byte = self.ring_buffer.read()
            
            if self.state == TL_State.Packet_Data_Size:
                self.temp_packet.packet_data_size = byte
                self.state = TL_State.Packet_Type
                
            elif self.state == TL_State.Packet_Type:
                self.temp_packet.packet_type = byte
                self.state = TL_State.Packet_Message_Type
                
            elif self.state == TL_State.Packet_Message_Type:
                self.temp_packet.packet_message_type = byte
                self.data_byte_count = 0
                self.state = TL_State.Data
                
            elif self.state == TL_State.Data:
                self.temp_packet.data[self.data_byte_count] = byte
                self.data_byte_count += 1
                if self.data_byte_count >= PACKET_DATA_BYTE_SIZE:
                    self.data_byte_count = 0
                    self.state = TL_State.Packet_CRC
                    
            elif self.state == TL_State.Packet_CRC:
                self.temp_packet.packet_crc = byte
                
                # Verify CRC
                calc_crc = self.temp_packet.compute_crc()
                if calc_crc != self.temp_packet.packet_crc:
                    print(f"{Colors.ERR}[TL RX] CRC FAILED! Sending RETX...{Colors.RST}")
                    self.TL_Write(self.retx_packet)
                    self.state = TL_State.Packet_Data_Size
                    continue

                hex_str = ' '.join(f'{b:02X}' for b in self.temp_packet.to_raw_packet())

                # Validate RETX
                if self.temp_packet.is_retx():
                    print(f"{Colors.ERR}[TL RX] RETX | PKT: {hex_str}{Colors.RST}")
                    print(f"{Colors.ERR}[TL ERR] STM32 Requested RETX, flushing and resending...{Colors.RST}")
                    
                    # CRITICAL RECOVERY: Wait for STM32 to digest garbage, then flush buffers
                    time.sleep(0.05) 
                    self.ser.reset_input_buffer()
                    self.ring_buffer = RingBuffer(size=2048)
                    
                    self.TL_Write(self.last_transmitted_packet)
                    self.state = TL_State.Packet_Data_Size
                    return # Exit the parsing loop to process fresh stream next tick

                # Validate ACK
                if self.temp_packet.is_ack():
                    self.state = TL_State.Packet_Data_Size
                    continue

                # Valid Application Data Packet
                print(f"{Colors.TL_RX}[TL RX] DATA (MsgType: 0x{self.temp_packet.packet_message_type:02X}, Size: {self.temp_packet.packet_data_size:02d}) | PKT: {hex_str}{Colors.RST}")
                
                self.packet_buffer.append(self.temp_packet.clone())
                
                # Automatically send ACK back to STM32
                self.TL_Write(self.ack_packet)
                self.state = TL_State.Packet_Data_Size

    def TL_IS_Packet_Available(self):
        return len(self.packet_buffer) > 0

    def TL_Read(self):
        if self.TL_IS_Packet_Available():
            return self.packet_buffer.pop(0)
        return None

    def send_sync(self):
        print(f"{Colors.TL_TX}[TL TX] RAW SYNC: 01 02 03 04{Colors.RST}")
        self.ser.write(b'\x01\x02\x03\x04')
        self.ser.flush()
        time.sleep(0.01)

    def send_application_packet(self, msg_type, data_bytes, data_size):
        pkt = TL_Packet()
        pkt.packet_data_size = data_size
        pkt.packet_type = PACKET_NONE
        pkt.packet_message_type = msg_type
        
        for i in range(PACKET_DATA_BYTE_SIZE):
            pkt.data[i] = data_bytes[i] if i < len(data_bytes) else 0xFF
            
        pkt.packet_crc = pkt.compute_crc()
        self.TL_Write(pkt)

    def wait_for_al_message(self, timeout=5.0):
        start = time.time()
        while time.time() - start < timeout:
            self.HAL_UART_RxCpltCallback()
            self.TL_Update()
            if self.TL_IS_Packet_Available():
                return self.TL_Read()
            time.sleep(0.001)
        return None

# ==========================================================
# APPLICATION LAYER (AL)
# ==========================================================
class HostState:
    SYNC          = "SYNC"
    REQ_UPDATE    = "REQ_UPDATE"
    SEND_HEADER   = "SEND_HEADER"
    WAIT_ERASE    = "WAIT_ERASE"
    SEND_FIRMWARE = "SEND_FIRMWARE"
    DONE          = "DONE"

def flash_firmware(port, fw_path):
    print(f"\n{Colors.AL}Reading Firmware Image: {fw_path}{Colors.RST}")
    with open(fw_path, 'rb') as f:
        fw_data = f.read()
        
    if len(fw_data) % 4 != 0:
        fw_data += b'\xFF' * (4 - (len(fw_data) % 4))
        
    tl = TransportLayer(port)
    
    fw_header = fw_data[:256]
    fw_payload = fw_data[256:]
    
    header_bytes_sent = 0
    fw_bytes_sent = 0
    total_fw_chunks = (len(fw_payload) + 31) // 32
    
    host_state = HostState.SYNC
    
    while host_state != HostState.DONE:
        
        if host_state == HostState.SYNC:
            tl.send_sync()
            pkt = tl.wait_for_al_message(timeout=1.0)
            if pkt and pkt.packet_message_type == AL_MESSAGE_SEQUENCE_OBSERVED:
                print(f"{Colors.AL}[AL] SYNC Accepted! STM32 is awake.{Colors.RST}")
                host_state = HostState.REQ_UPDATE
            else:
                print(f"{Colors.ERR}[AL ERR] Failed to synchronize. Retrying...{Colors.RST}")
                time.sleep(0.5)
                
        elif host_state == HostState.REQ_UPDATE:
            print(f"\n{Colors.AL}=== REQUESTING UPDATE ==={Colors.RST}")
            tl.send_application_packet(AL_MESSAGE_FIRMWARE_UPDATE_REQUEST, b'', 0)
            
            pkt = tl.wait_for_al_message(timeout=2.0)
            if pkt and pkt.packet_message_type == AL_MESSAGE_SENT_CURRENT_FIRMWARE_VERSION:
                dev_id = struct.unpack('<I', pkt.data[0:4])[0]
                version = struct.unpack('<I', pkt.data[4:8])[0]
                print(f"{Colors.AL}[AL] Target Info -> DeviceID: 0x{dev_id:X}, Current Version: 0x{version:X}{Colors.RST}")
                print(f"\n{Colors.AL}=== SENDING FIRMWARE HEADER ==={Colors.RST}")
                host_state = HostState.SEND_HEADER
            else:
                print(f"{Colors.ERR}[AL ERR] Expected Device Info (0x03). Aborting...{Colors.RST}")
                return
                
        elif host_state == HostState.SEND_HEADER:
            if header_bytes_sent < 256:
                chunk = fw_header[header_bytes_sent:header_bytes_sent+32]
                tl.send_application_packet(AL_MESSAGE_SENT_NEW_FIRMWARE_HEADER_DATA, chunk, len(chunk))
                
                pkt = tl.wait_for_al_message(timeout=2.0)
                if pkt and pkt.packet_message_type == AL_MESSAGE_RECEIVED_NEW_FIRMWARE_HEADER_DATA:
                    header_bytes_sent += len(chunk)
                    print(f"{Colors.AL}[AL] Header chunk {header_bytes_sent//32}/8 confirmed...{Colors.RST}")
                    
                    if header_bytes_sent == 256:
                        print(f"{Colors.AL}[AL] All headers sent. Waiting for STM32 to erase Flash...{Colors.RST}")
                        host_state = HostState.WAIT_ERASE
                else:
                     print(f"{Colors.ERR}[AL ERR] Did not receive Header confirmation (0x05).{Colors.RST}")
                     return
                     
        elif host_state == HostState.WAIT_ERASE:
            pkt = tl.wait_for_al_message(timeout=15.0)
            if pkt and pkt.packet_message_type == AL_MESSAGE_FIRMWARE_HEADER_WRITTEN:
                print(f"{Colors.AL}[AL] Flash memory successfully erased and header written!{Colors.RST}")
                print(f"\n{Colors.AL}=== SENDING FIRMWARE PAYLOAD ==={Colors.RST}")
                host_state = HostState.SEND_FIRMWARE
            else:
                print(f"{Colors.ERR}[AL ERR] Did not receive Erase/Header confirmation (0x06).{Colors.RST}")
                return

        elif host_state == HostState.SEND_FIRMWARE:
            if fw_bytes_sent < len(fw_payload):
                chunk_size = min(32, len(fw_payload) - fw_bytes_sent)
                chunk = fw_payload[fw_bytes_sent:fw_bytes_sent+chunk_size]
                
                tl.send_application_packet(AL_MESSAGE_RECEIVED_NEW_FIRMWARE_DATA, chunk, chunk_size)
                
                pkt = tl.wait_for_al_message(timeout=30)
                if pkt:
                    if pkt.packet_message_type == AL_MESSAGE_RECEIVED_NEW_FIRMWARE_DATA:
                        fw_bytes_sent += chunk_size
                        progress = fw_bytes_sent // 32
                        print(f"\r{Colors.AL}[AL] Flashing: [{progress}/{total_fw_chunks}] chunks written...{Colors.RST}", end="", flush=True)
                        
                    elif pkt.packet_message_type == AL_MESSAGE_UPDATE_SUCCESSFUL:
                        fw_bytes_sent += chunk_size
                        print(f"\r{Colors.AL}[AL] Flashing: [{total_fw_chunks}/{total_fw_chunks}] chunks written...{Colors.RST}")
                        print(f"\n{Colors.AL}============================================{Colors.RST}")
                        print(f"{Colors.AL}   !!! FIRMWARE UPDATE SUCCESSFUL !!!       {Colors.RST}")
                        print(f"{Colors.AL}============================================{Colors.RST}")
                        host_state = HostState.DONE
                    else:
                        print(f"{Colors.ERR}\n[AL ERR] Unexpected packet: 0x{pkt.packet_message_type:02X}{Colors.RST}")
                        return
                else:
                    print(f"{Colors.ERR}\n[AL ERR] Timeout waiting for firmware write state!{Colors.RST}")
                    return

# ==========================================================
# CLI ENTRY POINT
# ==========================================================
def select_port():
    print("Scanning for STMicroelectronics Virtual COM Ports...")
    ports = serial.tools.list_ports.comports()
    
    st_ports = []
    for p in ports:
        desc = p.description or ""
        manuf = p.manufacturer or ""
        if "STLink" in desc or "STM32" in desc or "STMicroelectronics" in manuf:
            st_ports.append(p)
    
    if st_ports:
        for i, p in enumerate(st_ports):
            print(f"[{i}] {p.device} - {p.description}")
        sel = input("Select Port [0]: ")
        return st_ports[int(sel) if sel else 0].device
    else:
        print("No ST ports automatically found. Here are all ports:")
        for i, p in enumerate(ports):
            print(f"[{i}] {p.device} - {p.description}")
        sel = input("Select Port: ")
        return ports[int(sel)].device

if __name__ == "__main__":
    print("STM32 Bootloader Update Tool\n")
    try:
        target_port = select_port()
        flash_firmware(target_port, "STM32-FW-IMAGE-SIGNED.bin")
    except KeyboardInterrupt:
        print("\nAborted.")
    except Exception as e:
        print(f"\n{Colors.ERR}Fatal Error: {e}{Colors.RST}")
