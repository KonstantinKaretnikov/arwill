.text
.global _start
.extern netserve_main
_start:
    call netserve_main
    movq %rax, %rdi
    movl $2, %eax
    int $0x80
    ud2
