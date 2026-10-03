.global _start
.type _start, @function

_start:
    andl $-16, %esp
    call main

    # main returned its exit status in EAX
    push %eax
    call exit

1:
    jmp 1b
