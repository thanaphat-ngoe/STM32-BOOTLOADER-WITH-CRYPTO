import sys

BOOTLOADER_TARGET_SIZE = 0x7800 # 30 KB
PAD_BYTE               = 0xFF   # PAD BOOTLOADER WITH 0xFF

def combine_and_pad(bootloader_path, bootloader_padded_path):
    try:
        with open(bootloader_path, "rb") as f:
            bootloader_data = f.read()

        current_bl_size = len(bootloader_data)
        if current_bl_size > BOOTLOADER_TARGET_SIZE:
            print(f"FATAL ERROR: Bootloader size ({current_bl_size} bytes) exceeds limit!")
            sys.exit(1)

        padding_size = BOOTLOADER_TARGET_SIZE - current_bl_size
        padded_bootloader = bootloader_data + (bytes([PAD_BYTE]) * padding_size)
        
        print(f"Bootloader Padding:")
        print(f" - Original Size: {current_bl_size} bytes")
        print(f" - Padded Size:   {len(padded_bootloader)} bytes (0x{len(padded_bootloader):04X})")
        
        with open(bootloader_padded_path, "wb") as f:
            f.write(padded_bootloader)

        print("\n" + "="*50)
        print(f"SUCCESS! Ready to flash: '{bootloader_padded_path}'")
        print(f"Total Combined Size: {len(padded_bootloader)} bytes")
        print("="*50)

    except Exception as e:
        print(f"An unexpected error occurred: {e}")
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) == 3:
        combine_and_pad(sys.argv[1], sys.argv[2])
    else:
        sys.exit(1)
