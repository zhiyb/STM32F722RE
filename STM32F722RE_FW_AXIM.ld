INCLUDE STM32F722RE.ld

/* Choose AXIM interface for loading application */
REGION_ALIAS("FLASH", FW_AXIM);
/* Choose AXIM interface for running application */
REGION_ALIAS("RAM", DTCM);

INCLUDE common.ld
