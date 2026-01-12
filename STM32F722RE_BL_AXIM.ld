INCLUDE STM32F722RE.ld

/* Choose AXIM interface for loading application */
REGION_ALIAS("LOAD", BL_AXIM);
/* Choose AXIM interface for running application */
REGION_ALIAS("EXEC", BL_AXIM);

REGION_ALIAS("RAM", DTCM);

INCLUDE common.ld
