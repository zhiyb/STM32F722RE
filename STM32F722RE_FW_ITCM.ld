INCLUDE STM32F722RE.ld

/* Choose AXIM interface for loading application */
REGION_ALIAS("LOAD", FW_AXIM);
/* Choose AXIM interface for running application */
REGION_ALIAS("EXEC", FW_ITCM);

REGION_ALIAS("RAM", DTCM);
REGION_ALIAS("DMARAM", SRAM_2);

INCLUDE common.ld
