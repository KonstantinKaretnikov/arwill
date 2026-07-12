.text
.global _start
.extern calculator_main
_start:
    call calculator_main
    movq %rax, %rdi
    movl $2, %eax
    int $0x80
    ud2
