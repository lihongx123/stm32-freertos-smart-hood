ENTRY(Reset_Handler)

MEMORY
{
  ROM (rx) : ORIGIN = 0x08000000, LENGTH = 64K
  RAM (xrw) : ORIGIN = 0x20000000, LENGTH = 128K
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
_Min_Heap_Size = 0;
_Min_Stack_Size = 0x400;

SECTIONS
{
  .isr_vector : { . = ALIGN(4); KEEP(*(.isr_vector)); . = ALIGN(4); } > ROM
  .text : { . = ALIGN(4); *(.text*) *(.rodata*) KEEP(*(.init))
            KEEP(*(.fini)); . = ALIGN(4); } > ROM
  .ARM.extab : { *(.ARM.extab*) } > ROM
  .ARM.exidx : { __exidx_start = .; *(.ARM.exidx*); __exidx_end = .; } > ROM
  .preinit_array : { KEEP(*(.preinit_array*)) } > ROM
  .init_array : { KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array*)) } > ROM
  .fini_array : { KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array*)) } > ROM
  _sidata = LOADADDR(.data);
  .data : { . = ALIGN(4); _sdata = .; *(.data*) . = ALIGN(4); _edata = .; } > RAM AT > ROM
  .bss : { . = ALIGN(4); _sbss = .; *(.bss*) *(COMMON);
           . = ALIGN(4); _ebss = .; } > RAM
  ._user_heap_stack : { . = ALIGN(8); . += _Min_Heap_Size;
                        . += _Min_Stack_Size; . = ALIGN(8); } > RAM
  /DISCARD/ : { *(.note.GNU-stack) }
}
