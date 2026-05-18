import sys

# --- Configuration ---
BOOTLOADER_TARGET_SIZE = 0x5000 # 20 KB
PAD_BYTE               = 0xFF   # Pad bootloader with 0xFF for each byte

def combine_and_pad(bootloader_path, app_path, output_path):
    try:
        # PROCESS BOOTLOADER
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

        # READ FILLED APPLICATION FIRMWARE
        with open(app_path, "rb") as f:
            app_data = f.read()

        print(f"Application Firmware Size: {len(app_data)} bytes")

        # COMBINE AND WRITE OUTPUT
        final_firmware = padded_bootloader + app_data
        
        with open(output_path, "wb") as f:
            f.write(final_firmware)

        print("\n" + "="*50)
        print(f"SUCCESS! Ready to flash: '{output_path}'")
        print(f"Total Combined Size: {len(final_firmware)} bytes")
        print("="*50)

    except Exception as e:
        print(f"An unexpected error occurred: {e}")
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) == 4:
        combine_and_pad(sys.argv[1], sys.argv[2], sys.argv[3])
    else:
        print("Usage: python combine_firmware.py <bootloader.bin> <filled_app.bin> <output_full.bin>")
        sys.exit(1)
