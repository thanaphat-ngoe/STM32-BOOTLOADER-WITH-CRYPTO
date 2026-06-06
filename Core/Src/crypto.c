#include "crypto.h"
#include "cmox_crypto.h"

#define ECC_WORK_BUFFER_SIZE 2048

static uint8_t EccWorkBuffer[ECC_WORK_BUFFER_SIZE];

const uint8_t FIRMWARE_PUBLIC_KEY[CMOX_ECC_SECP256R1_PUBKEY_LEN] = {
    // PUBLIC KEY X (32 Bytes)
    0x0c, 0x25, 0x66, 0x59, 0xb6, 0xcf, 0x9b, 0xd2, 
	0x2b, 0x46, 0xd5, 0x5a, 0x56, 0x20, 0x3e, 0x5b, 
	0x90, 0xcb, 0x60, 0x54, 0x3c, 0xc1, 0xcc, 0xbe, 
	0x39, 0x44, 0x7c, 0xf7, 0x04, 0x12, 0x0d, 0x2e,
	
    // PUBLIC KEY Y (32 Bytes)
	0xd2, 0x5a, 0x20, 0x2b, 0x02, 0x18, 0xb4, 0xab, 
	0xca, 0x29, 0x36, 0x37, 0x00, 0x42, 0xb1, 0x73, 
	0xb6, 0x31, 0xb2, 0x7a, 0x64, 0x94, 0x5d, 0x3a, 
	0xae, 0x4a, 0x9d, 0xa2, 0xb8, 0x9a, 0x8a, 0xcd
};

bool Verify_Firmware_Signature(uint32_t firmware_start_address) {
    FirmwareHeader_TypeDef* header = (FirmwareHeader_TypeDef*)firmware_start_address;

    cmox_initialize(NULL);

    uint8_t hash_result[CMOX_SHA256_SIZE];
    uint32_t payload_address = firmware_start_address + sizeof(FirmwareHeader_TypeDef);

    cmox_hash_retval_t hash_ret = cmox_hash_compute(
        CMOX_SHA256_ALGO,                  
        (const uint8_t*)payload_address, 
        header->Size,                     
        hash_result,                   
        CMOX_SHA256_SIZE,                  
        NULL                             
    );

    if (hash_ret != CMOX_HASH_SUCCESS) {
        return false;
    }

    uint8_t signature[CMOX_ECC_SECP256R1_SIG_LEN];
    memcpy(&signature[0],  header->Signature_R, 32);
    memcpy(&signature[32], header->Signature_S, 32);

    cmox_ecc_handle_t EccCtx;
    uint32_t fault_check = 0;
    
    cmox_ecc_construct(&EccCtx, CMOX_MATH_FUNCS_SMALL, EccWorkBuffer, ECC_WORK_BUFFER_SIZE);

    cmox_ecc_retval_t ecc_ret = cmox_ecdsa_verify(
        &EccCtx,
        CMOX_ECC_SECP256R1_LOWMEM,
        FIRMWARE_PUBLIC_KEY,
        CMOX_ECC_SECP256R1_PUBKEY_LEN,  
        hash_result,
        CMOX_SHA256_SIZE,            
        signature,
        CMOX_ECC_SECP256R1_SIG_LEN,     
        &fault_check 
    );

    cmox_ecc_cleanup(&EccCtx);

    if (ecc_ret == CMOX_ECC_AUTH_SUCCESS && fault_check == CMOX_ECC_AUTH_SUCCESS) {
        return true;
    }

    return false;
}
