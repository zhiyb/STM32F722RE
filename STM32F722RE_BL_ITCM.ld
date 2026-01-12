INCLUDE STM32F722RE.ld

/* Choose AXIM interface for loading application */
REGION_ALIAS("LOAD", BL_AXIM);
/* Choose ITCM interface for running application */
REGION_ALIAS("EXEC", BL_ITCM);

REGION_ALIAS("RAM", DTCM);

INCLUDE common.ld
