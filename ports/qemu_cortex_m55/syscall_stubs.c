/* SPDX-License-Identifier: Apache-2.0 */

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

// UART0 base address for QEMU MPS3-AN547
#define UART0_BASE 0x49303000
#define UART_DR_OFFSET 0x000
#define UART_FR_OFFSET 0x018
#define UART_FR_TXFF (1 << 5)  // Transmit FIFO full flag

// Simple UART output for QEMU
static void uart_putc(char c) {
    volatile uint32_t *uart_dr = (volatile uint32_t *)(UART0_BASE + UART_DR_OFFSET);
    volatile uint32_t *uart_fr = (volatile uint32_t *)(UART0_BASE + UART_FR_OFFSET);
    
    // Wait until TX FIFO is not full
    while (*uart_fr & UART_FR_TXFF);
    
    // Write character to data register
    *uart_dr = (uint32_t)c;
}

// Minimal system call stubs for embedded environment
int _close(int file) { (void)file; return -1; }
int _fstat(int file, struct stat *st) { (void)file; st->st_mode = S_IFCHR; return 0; }
int _isatty(int file) { (void)file; return 1; }
int _lseek(int file, int ptr, int dir) { (void)file; (void)ptr; (void)dir; return 0; }
int _read(int file, char *ptr, int len) { (void)file; (void)ptr; (void)len; return 0; }

int _write(int file, char *ptr, int len) { 
    (void)file;
    
    // Use semihosting for output in QEMU
    #ifdef __thumb2__
    // ARM semihosting call
    register uint32_t r0 asm("r0") = 0x04; // SYS_WRITE
    register uint32_t r1 asm("r1") = (uint32_t)ptr;
    asm volatile (
        "bkpt #0xAB"
        : 
        : "r" (r0), "r" (r1)
        : "memory"
    );
    #else
    // Fallback UART method for QEMU MPS3-AN547
    for (int i = 0; i < len; i++) {
        uart_putc(ptr[i]);
        // Convert \n to \r\n for proper line endings
        if (ptr[i] == '\n') {
            uart_putc('\r');
        }
    }
    #endif
    
    return len; 
}

void _exit(int status) { (void)status; while(1) __asm("wfi"); }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
int _getpid() { return 1; }

void *_sbrk(int incr) { 
    extern char _end; 
    static char *heap_end = &_end; 
    char *prev_heap_end = heap_end; 
    heap_end += incr; 
    return (void*)prev_heap_end; 
}
