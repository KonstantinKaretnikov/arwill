SECTIONS
{
    . = 0x0000008000000000;
    .text : {
        *(.text)
        *(.text.*)
        *(.ltext)
        *(.ltext.*)
    }
    .rodata : {
        *(.rodata)
        *(.rodata.*)
        *(.lrodata)
        *(.lrodata.*)
    }
    .data : {
        *(.data)
        *(.data.*)
        *(.ldata)
        *(.ldata.*)
    }
    .bss : {
        *(.bss)
        *(.bss.*)
        *(.lbss)
        *(.lbss.*)
        *(COMMON)
    }
}
