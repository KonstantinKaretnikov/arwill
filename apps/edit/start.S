.text
.global _start
.extern editor_main
_start:
    call editor_main
    movq %rax, %rdi
    movl $2, %eax
    int $0x80
    ud2
