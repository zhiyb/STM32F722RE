/**
  ******************************************************************************
  * @file      startup_stm32f722xx.s
  * @author    MCD Application Team
  * @brief     STM32F722xx Devices vector table for GCC based toolchain.
  *            This module performs:
  *                - Set the initial SP
  *                - Set the initial PC == Reset_Handler,
  *                - Set the vector table entries with the exceptions ISR address
  *                - Branches to main in the C library (which eventually
  *                  calls main()).
  *            After Reset the Cortex-M7 processor is in Thread mode,
  *            priority is Privileged, and the Stack is set to Main.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2016 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

  .syntax unified
  .cpu cortex-m7
  .fpu softvfp
  .thumb

.global	g_pfnVectors
.global	Default_Handler

/* top of stack */
.word __stack_end
/* start address for the initialization values of the .data section.
defined in linker script */
.word __data_load
/* start address for the .data section. defined in linker script */
.word __data_start
/* end address for the .data section. defined in linker script */
.word __data_end
/* start address for the .bss section. defined in linker script */
.word __bss_start
/* end address for the .bss section. defined in linker script */
.word __bss_end

  .section .text.Reset_Handler
  .weak Reset_Handler
  .type Reset_Handler, %function
Reset_Handler:
  ldr   r0, =__stack_end
  mov   sp, r0          /* set stack pointer */
/* Call the clock system initialization function.*/
  @ bl  SystemInit

/* Copy the data segment initializers from flash to SRAM */
  movs r1, #0
  b LoopCopyDataInit

CopyDataInit:
  ldr r3, =__data_load
  ldr r3, [r3, r1]
  str r3, [r0, r1]
  adds r1, r1, #4

LoopCopyDataInit:
  ldr r0, =__data_start
  ldr r3, =__data_end
  adds r2, r0, r1
  cmp r2, r3
  bcc CopyDataInit
  ldr r2, =__bss_start
  b LoopFillZerobss
/* Zero fill the bss segment. */
FillZerobss:
  movs r3, #0
  str  r3, [r2]
  adds r2, r2, #4


LoopFillZerobss:
  ldr r3, = __bss_end
  cmp r2, r3
  bcc FillZerobss


/* Call static constructors */
  @ bl __libc_init_array
/* Call the application's entry point.*/
  bl main

LoopForever:
  bkpt 0
  b LoopForever

.size Reset_Handler, .-Reset_Handler
