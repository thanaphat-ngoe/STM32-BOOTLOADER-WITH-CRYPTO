#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "data-link-layer.h"
#include "transport-layer.h"

// AL Commands
#define AL_MSG_BOOTLOADER          0x01
#define AL_MSG_SYNC                0x02
#define AL_MSG_SENT_HEADER         0x03
#define AL_MSG_VERIFIED_HEADER     0x05
#define AL_MSG_ERASED_FLASH        0x06
#define AL_MSG_WROTE_HEADER        0x07
#define AL_MSG_RX_FW_DATA          0x08
#define AL_MSG_SUCCESS             0x09
#define AL_MSG_ABORT               0x0A

bool debug_mode = false; // ตัวแปร Global กำหนด Debug Mode

void log_msg(const char* msg) {
    if (debug_mode) printf("%s\n", msg);
}

void draw_progress_bar(int current, int total) {
    if (debug_mode) return; 
    int width = 50;
    float ratio = (float)current / (float)total;
    if (ratio > 1.0f) ratio = 1.0f;
    int pos = (int)(width * ratio);

    printf("\r\033[K[AL] Progress: ["); 
    for (int i = 0; i < width; i++) {
        if (i < pos) printf("█"); 
        else printf(" ");
    }
    printf("] %3d%%", (int)(ratio * 100));
    fflush(stdout);
}

// Menu Functions
void select_file(char* out_file) {
    DIR *dir = opendir(".");
    struct dirent *ent;
    char files[50][256];
    int count = 0;
    
    printf("\n=== Available Firmware (.bin) ===\n");
    while ((ent = readdir(dir)) != NULL) {
        if (strstr(ent->d_name, ".bin") != NULL) {
            strcpy(files[count], ent->d_name);
            printf(" [%d] %s\n", count + 1, files[count]);
            count++;
        }
    }
    closedir(dir);
    if (count == 0) { printf("Error: No .bin files found!\n"); exit(1); }
    
    int choice = 0;
    printf("Select file number (1-%d): ", count);
    if(scanf("%d", &choice) != 1 || choice < 1 || choice > count) { printf("Invalid choice!\n"); exit(1); }
    strcpy(out_file, files[choice - 1]);
}

void select_port(char* out_port) {
    DIR *dir = opendir("/dev");
    struct dirent *ent;
    char ports[50][256];
    int count = 0;
    
    printf("\n=== Available Serial Ports ===\n");
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "cu.usb", 6) == 0 || strncmp(ent->d_name, "tty.usb", 7) == 0) {
            sprintf(ports[count], "/dev/%s", ent->d_name);
            printf(" [%d] %s\n", count + 1, ports[count]);
            count++;
        }
    }
    closedir(dir);
    if (count == 0) { printf("Error: No STM32/USB Serial device found!\n"); exit(1); }
    
    int choice = 0;
    printf("Select port number (1-%d): ", count);
    if(scanf("%d", &choice) != 1 || choice < 1 || choice > count) { printf("Invalid choice!\n"); exit(1); }
    strcpy(out_port, ports[choice - 1]);
}

// MAIN
int main(int argc, char *argv[]) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-debug") == 0 && i + 1 < argc) {
            if (strcmp(argv[i+1], "on") == 0) debug_mode = true;
            i++;
        }
    }

    printf("==========================================\n");
    printf("      STM32 FIRMWARE UPDATER (HOST)       \n");
    printf("==========================================\n");
    printf("Debug Mode: %s\n", debug_mode ? "ON" : "OFF");

    char target_file[256];
    char target_port[256];

    // เลือกออปชันต่างๆ (พอร์ตยังไม่ถูกเปิดในขั้นตอนนี้)
    select_file(target_file);
    select_port(target_port);

    char confirm[10];
    printf("\nReady to flash '%s' to '%s'.\n", target_file, target_port);
    printf("Continue? [Y/n]: ");
    if(scanf("%s", confirm) != 1) return 0;
    if (confirm[0] != 'Y' && confirm[0] != 'y') {
        printf("Update cancelled.\n");
        return 0;
    }

    // โหลดไฟล์
    FILE *f = fopen(target_file, "rb");
    if (!f) { printf("Error opening file.\n"); return 1; }
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size <= 256) { printf("Error: Firmware too small!\n"); return 1; }
    
    uint8_t *fw_data = malloc(file_size);
    fread(fw_data, 1, file_size, f);
    fclose(f);

    // 🛑 เปิดและเคลียร์พอร์ต
    if (!DLL_Open(target_port)) { printf("Error: Cannot open serial port.\n"); return 1; }
    TL_Init(); // เริ่มต้นคลังอาวุธ
    
    printf("\n[SYSTEM] Connected! Starting process...\n");
    uint8_t cmd;

    // 1. BOOTLOADER_MODE
    if (!debug_mode) {
        printf("\r\033[K[SYSTEM] Waiting for Bootloader (Please press RESET on board)...");
        fflush(stdout);
    }
    log_msg("[AL] Waiting for device to enter Bootloader mode...");
    while (1) {
        if (TL_Wait_For_Command(1000, &cmd, NULL)) {
            if (cmd == AL_MSG_BOOTLOADER) {
                if(!debug_mode) printf("\r\033[K"); 
                log_msg("[AL] >>> Received: BOOTLOADER_MODE");
                break;
            }
        }
    }

    // 2. SYNC
    log_msg("[AL] <<< Sending: SYNCHRONIZE_MODE");
    if (!TL_Send_Command(AL_MSG_SYNC, NULL, 1000)) goto Abort;

    // 3. HEADER
    log_msg("[AL] <<< Sending Firmware Header (256 Bytes)...");
    for (int i = 0; i < 256; i += 4) {
        if (!TL_Send_Command(AL_MSG_SENT_HEADER, &fw_data[i], 1000)) goto Abort;
    }

    // 4. VERIFY HEADER
    log_msg("[AL] Waiting for Header Verification...");
    if (TL_Wait_For_Command(5000, &cmd, NULL)) {
        if (cmd == AL_MSG_VERIFIED_HEADER) log_msg("[AL] >>> Received: VERIFIED_NEW_FIRMWARE_HEADER");
        else if (cmd == AL_MSG_ABORT) { printf("\n\n❌ HEADER REJECTED!\n"); goto Abort; }
        else goto Abort;
    } else goto Abort;

    // 5. ERASE FLASH
    if(!debug_mode) {
        printf("\r\033[K[AL] Erasing Flash (This may take a few seconds)...");
        fflush(stdout);
    }
    log_msg("\n[AL] Waiting for Device to Erase Flash...");
    if (TL_Wait_For_Command(15000, &cmd, NULL) && cmd == AL_MSG_ERASED_FLASH) {
        log_msg("[AL] >>> Received: ERASED_FLASH");
    } else goto Abort;

    // 6. WRITE HEADER
    log_msg("[AL] Waiting for Header Write confirmation...");
    if (TL_Wait_For_Command(5000, &cmd, NULL) && cmd == AL_MSG_WROTE_HEADER) {
        log_msg("[AL] >>> Received: WROTE_NEW_FIRMWARE_HEADER");
    } else goto Abort;

    // 7. FW BODY
    int body_size = file_size - 256;
    if (debug_mode) printf("\n[AL] <<< Sending Firmware Body (%d Bytes)...\n", body_size);
    else printf("\r\033[K");

    int last_percent = -1;
    for (int i = 256; i < file_size; i += 4) {
        uint8_t chunk[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        int chunk_len = (file_size - i >= 4) ? 4 : (file_size - i);
        memcpy(chunk, &fw_data[i], chunk_len);

        if (!TL_Send_Command(AL_MSG_RX_FW_DATA, chunk, 1000)) goto Abort;
        
        int progress = ((i - 256 + 4) * 100) / body_size;
        if (progress > 100) progress = 100;
        
        if (debug_mode) {
            if (progress % 10 == 0 && progress != last_percent) {
                printf("[AL] Progress: %d%%\n", progress);
                last_percent = progress;
            }
        } else {
            draw_progress_bar(i - 256 + chunk_len, body_size);
        }
    }

    // 8. VERIFY SIGNATURE
    if (!debug_mode) {
        printf("\n[AL] Verifying ECDSA Signature...\n");
        fflush(stdout);
    }
    log_msg("\n\n[AL] Waiting for ECDSA Signature Verification...");
    
    if (TL_Wait_For_Command(60000, &cmd, NULL)) {
        if (cmd == AL_MSG_SUCCESS) {
            printf("\n==========================================\n");
            printf("🎉 UPDATE SUCCESSFUL! Restarting The Device...\n");
            printf("==========================================\n");
        } else if (cmd == AL_MSG_ABORT) {
            printf("\n==========================================\n");
            printf("❌ UPDATE ABORTED! Signature Verification Failed.\n");
            printf("==========================================\n");
        } else {
            printf("\n❌ Error: Unexpected response (0x%02X)\n", cmd);
        }
    } else {
        printf("\n❌ Error: Timeout waiting for ECDSA Verification.\n");
    }

    free(fw_data);
    DLL_Close();
    return 0;

Abort:
    printf("\n\n❌ COMMUNICATION FAILED OR ABORTED.\n");
    free(fw_data);
    DLL_Close();
    return 1;
}
