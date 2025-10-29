.section .text.init
.global _start

_start:
    la sp, _stack_top

    la t0, __bss_start
    la t1, __bss_end

zero_bss:
    bgeu t0, t1, main_start
    sb zero, 0(t0)
    addi t0, t0, 1
    j zero_bss

main_start:
    call main

1: j 1b


