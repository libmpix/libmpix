/* SPDX-License-Identifier: Apache-2.0 */

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

/** QEMU 16550A UART */
#define UART_BASE 0x10000000
#define UART_THR  (*(volatile char *)(UART_BASE + 0x00))
#define UART_LSR  (*(volatile char *)(UART_BASE + 0x05))

#define UART_LSR_TX_IDLE  (1 << 5) // Transmitter idle

void uart_putc(char c) {
    while((UART_LSR & UART_LSR_TX_IDLE) == 0);
    UART_THR = c;

    if (c == '\n') {
        while((UART_LSR & UART_LSR_TX_IDLE) == 0);
        UART_THR = '\r';
    }
}

int _write(int file, char *ptr, int len)
{
	(void)file;

	for (int i = 0; i < len; i++) {
		uart_putc(ptr[i]);
	}
	return len;
}

int _read(int file, char *ptr, int len)
{
	(void)file;
	(void)ptr;
	return len;
}

int _open(const char *name, int flags, int mode)
{
	(void)name;
	(void)flags;
	(void)mode;
	return -1;
}

int _close(int file)
{
	(void)file;
	return -1;
}

int _lseek(int file, int ptr, int dir)
{
	(void)file;
	(void)ptr;
	(void)dir;
	return 0;
}

int _fstat(int file, struct stat *st)
{
	(void)file;
	(void)st;
	return 0;
}

int _isatty(int file)
{
	(void)file;
	return 1;
}

void _exit(int code)
{
	(void)code;
	while (1)
		;
}

int _kill(int pid, int sig)
{
	(void)pid;
	(void)sig;
	return -1;
}

int _getpid(void)
{
	return 1;
}

void *_sbrk(int incr)
{
	extern char _end;
	extern char _stack_bottom;

	static char *heap_end = &_end;
	char *prev_heap_end = heap_end;

	char *stack_limit = &_stack_bottom;

	if (heap_end + incr > stack_limit) {
		return (void *)-1;
	}

	heap_end += incr;
	return (void *)prev_heap_end;
}
