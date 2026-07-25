.text
.global _start
.extern curl_main
_start:
    call curl_main
    movq %rax, %rdi
    movl $2, %eax
    int $0x80
    ud2
