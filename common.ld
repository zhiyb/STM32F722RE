/* Top of stack at end of RAM space */
__stack_end = ORIGIN(RAM) + LENGTH(RAM);

/* Firmware data region */
__firmware_start = ORIGIN(FW_AXIM);
__firmware_end   = __firmware_start + LENGTH(FW_AXIM);

/* Bootloader request address */
__bootloader_req = ORIGIN(ITCM);

/* Define output sections */
SECTIONS
{
  /* Firmware header and interrupt vector table goes first into EXEC */
  . = ALIGN(4);
  .header :
  {
    KEEP(*(.fw_header))
  } >EXEC AT> LOAD

  /* The program code and other data goes into EXEC */
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
  } >EXEC AT> LOAD

  /* Constant data goes into EXEC */
  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)         /* .rodata sections (constants, strings, etc.) */
    *(.rodata*)        /* .rodata* sections (constants, strings, etc.) */
    . = ALIGN(4);
  } >EXEC AT> LOAD

  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >EXEC AT> LOAD

  .ARM :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >EXEC AT> LOAD

  .preinit_array :
  {
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
  } >EXEC AT> LOAD

  .init_array :
  {
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
  } >EXEC AT> LOAD

  .fini_array :
  {
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
  } >EXEC AT> LOAD


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
  } >RAM AT> LOAD

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
