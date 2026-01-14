INCLUDE STM32F722RE.ld

/* Choose AXIM interface for loading application */
REGION_ALIAS("LOAD", ITCM);
/* Choose ITCM interface for running application */
REGION_ALIAS("EXEC", ITCM);

REGION_ALIAS("RAM", DTCM);

INCLUDE common.ld
