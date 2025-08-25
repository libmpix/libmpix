/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

// ARM Helium MVE debayer function declarations
void mpix_convert_rggb8_to_rgb24_2x2(const uint8_t *src0, const uint8_t *src1, uint8_t *dst, uint16_t width);
void mpix_convert_bggr8_to_rgb24_2x2(const uint8_t *src0, const uint8_t *src1, uint8_t *dst, uint16_t width);
void mpix_convert_gbrg8_to_rgb24_2x2(const uint8_t *src0, const uint8_t *src1, uint8_t *dst, uint16_t width);
void mpix_convert_grbg8_to_rgb24_2x2(const uint8_t *src0, const uint8_t *src1, uint8_t *dst, uint16_t width);

// Test data
#define WIDTH 16
static uint8_t test_row0[WIDTH] = {
    0x80, 0x40, 0x80, 0x40, 0x80, 0x40, 0x80, 0x40,
    0x80, 0x40, 0x80, 0x40, 0x80, 0x40, 0x80, 0x40
};

static uint8_t test_row1[WIDTH] = {
    0x20, 0x60, 0x20, 0x60, 0x20, 0x60, 0x20, 0x60,
    0x20, 0x60, 0x20, 0x60, 0x20, 0x60, 0x20, 0x60
};

static uint8_t output[WIDTH * 3 / 2];

void test_helium_debayer(void)
{
    printf("ARM Helium MVE Debayer Test Starting...\n");
    printf("=====================================\n");
    
    // Test RGGB to RGB24
    printf("\nTesting RGGB8 to RGB24 conversion:\n");
    memset(output, 0, sizeof(output));
    mpix_convert_rggb8_to_rgb24_2x2(test_row0, test_row1, output, WIDTH);
    
    printf("First pixel: R=0x%02X, G=0x%02X, B=0x%02X\n", 
           output[0], output[1], output[2]);
    printf("Second pixel: R=0x%02X, G=0x%02X, B=0x%02X\n", 
           output[3], output[4], output[5]);
    
    // Test BGGR to RGB24
    printf("\nTesting BGGR8 to RGB24 conversion:\n");
    memset(output, 0, sizeof(output));
    mpix_convert_bggr8_to_rgb24_2x2(test_row0, test_row1, output, WIDTH);
    
    printf("First pixel: R=0x%02X, G=0x%02X, B=0x%02X\n", 
           output[0], output[1], output[2]);
    printf("Second pixel: R=0x%02X, G=0x%02X, B=0x%02X\n", 
           output[3], output[4], output[5]);
    
    // Test GBRG to RGB24
    printf("\nTesting GBRG8 to RGB24 conversion:\n");
    memset(output, 0, sizeof(output));
    mpix_convert_gbrg8_to_rgb24_2x2(test_row0, test_row1, output, WIDTH);
    
    printf("First pixel: R=0x%02X, G=0x%02X, B=0x%02X\n", 
           output[0], output[1], output[2]);
    
    // Test GRBG to RGB24
    printf("\nTesting GRBG8 to RGB24 conversion:\n");
    memset(output, 0, sizeof(output));
    mpix_convert_grbg8_to_rgb24_2x2(test_row0, test_row1, output, WIDTH);
    
    printf("First pixel: R=0x%02X, G=0x%02X, B=0x%02X\n", 
           output[0], output[1], output[2]);
    
    printf("\n=====================================\n");
    printf("ARM Helium MVE Debayer Test Completed!\n");
    printf("All conversions executed successfully.\n");
}

int main(void)
{
    printf("Cortex-M55 ARM Helium MVE Test Program\n");
    printf("======================================\n");
    
    #ifdef __ARM_FEATURE_MVE
    printf("ARM Helium MVE support: ENABLED\n");
    #else
    printf("ARM Helium MVE support: DISABLED\n");
    #endif
    
    #ifdef CONFIG_MPIX_SIMD_ARM_HELIUM
    printf("libmpix ARM Helium support: ENABLED\n");
    #else
    printf("libmpix ARM Helium support: DISABLED\n");
    #endif
    
    printf("Starting debayer tests...\n\n");
    
    test_helium_debayer();
    
    printf("\nTest program finished. Exiting...\n");
    
    // Exit cleanly for QEMU
    while(1) {
        __asm("wfi"); // Wait for interrupt
    }
    
    return 0;
}
