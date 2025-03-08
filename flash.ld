/* Specify the memory areas */
MEMORY
{
  FLASH (rx)    : ORIGIN = 0x08000000, LENGTH = 128K
  /* CODE (rx)     : ORIGIN = 0x00000000, LENGTH = 128K */
  RAM (xrw)     : ORIGIN = 0x20000000, LENGTH = 24K
  USBRAM (xrw)  : ORIGIN = 0x40009800, LENGTH = 2K
}

/* Top of stack at end of RAM space */
__stack_end = ORIGIN(RAM) + LENGTH(RAM);

/* Define output sections */
SECTIONS
{
  /* The startup code goes first into FLASH */
  . = ALIGN(4);
  .isr_vector :
  {
    __isr_vector_start = .;
    KEEP(*(.isr_vector)) /* Startup code */
    . = ALIGN(4);
  } >FLASH

  /* The program code and other data goes into FLASH */
  . = ALIGN(4);
  .text :
  {
    *(.text)           /* .text sections (code) */
    *(.text*)          /* .text* sections (code) */
    *(.glue_7)         /* glue arm to thumb code */
    *(.glue_7t)        /* glue thumb to arm code */
    *(.eh_frame)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
  } >FLASH

  /* Constant data goes into FLASH */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >FLASH

  .ARM.extab   : { *(.ARM.extab* .gnu.linkonce.armextab.*) } >FLASH
  .ARM : {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  .preinit_array     :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >FLASH
  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >FLASH
  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >FLASH


  /* Used by the startup to initialize data */
  __data_load = LOADADDR(.data);

  /* Initialized data sections goes into RAM, load LMA copy after code */
  . = ALIGN(4);
  .data :
  {
    __data_start = .;
    *(.data)           /* .data sections */
    *(.data*)          /* .data* sections */
    *(.got)
    *(.got*)

    . = ALIGN(4);
    __data_end = .;
  } >RAM AT> FLASH

  /* Uninitialized data section */
  . = ALIGN(4);
  .bss :
  {
    __bss_start = .;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    __bss_end = .;
  } >RAM


  /* Remove information from the standard libraries */
  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}


