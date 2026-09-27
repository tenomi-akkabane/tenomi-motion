/*
******************************************************************************
**
** @file        : STM32N657X0HXQ_LRUN_PSRAM.ld
**
** @brief       : Linker script for TENOMI hand-landmark port (μT-Kernel)
**                - Code/data: AXISRAM LRUN (same boot path as Lチカ)
**                - Large buffers: external PSRAM via XSPI1 (NOLOAD)
**
**                Based on:
**                  - mtk3bsp2 Appli STM32N657X0HXQ_LRUN.ld
**                  - landmarks Gcc/STM32N657xx.ld (PSRAM + FSBL guard)
**
**                IMPORTANT (runtime):
**                  PSRAM is only usable AFTER BSP_XSPI_RAM_Init() and
**                  BSP_XSPI_RAM_EnableMemoryMappedMode().
**                  NOR (AI weights @ 0x70xxxxxx) is separate; not in this map.
**
**                IMPORTANT (AXISRAM size):
**                  LENGTH 1535K ends at 0x34180000 so that NemaGFX CL pool /
**                  FSBL region (0x34180000+) is not claimed by .bss.
**                  When adopting this script, also align μT-Kernel
**                  INTERNAL_RAM_SIZE in sysdef.h (currently 2047K).
**
******************************************************************************
*/

/* Entry Point */
ENTRY(Reset_Handler)

/* FSBL occupies from this address; application must not grow into it */
_fsbl_start_address = 0x34180000;

/* Highest address of the user mode stack */
_estack = ORIGIN(RAM) + LENGTH(RAM);
_sstack = _estack - _Min_Stack_Size;

_Min_Heap_Size = 0x200;
_Min_Stack_Size = 0x1000;

/* Memories definition */
MEMORY
{
  /* Internal AXISRAM: match landmarks (ends at 0x34180000) */
  RAM    (xrw) : ORIGIN = 0x34000400, LENGTH = 1535K

  /*
   * External PSRAM (XSPI1): landmarks places .psram_bss at 0x91000000
   * so that 0x90000000.. is free for NemaGFX FB/VG pools.
   * Hardware window base is 0x90000000 (APS256XX); offset is software policy.
   */
  PSRAM  (xrw) : ORIGIN = 0x91000000, LENGTH = 16M
}

/* Sections */
SECTIONS
{
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >RAM

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)
    *(.RamFunc)
    *(.RamFunc*)

    KEEP (*(.init))
    KEEP (*(.fini))

    . = ALIGN(4);
    _etext = .;
  } >RAM

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >RAM

  .ARM.extab   (READONLY) :
  {
    . = ALIGN(4);
    *(.ARM.extab* .gnu.linkonce.armextab.*)
    . = ALIGN(4);
  } >RAM

  .ARM (READONLY) :
  {
    . = ALIGN(4);
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
    . = ALIGN(4);
  } >RAM

  .preinit_array     (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__preinit_array_start = .);
    KEEP (*(.preinit_array*))
    PROVIDE_HIDDEN (__preinit_array_end = .);
    . = ALIGN(4);
  } >RAM

  .init_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__init_array_start = .);
    KEEP (*(SORT(.init_array.*)))
    KEEP (*(.init_array*))
    PROVIDE_HIDDEN (__init_array_end = .);
    . = ALIGN(4);
  } >RAM

  .fini_array (READONLY) :
  {
    . = ALIGN(4);
    PROVIDE_HIDDEN (__fini_array_start = .);
    KEEP (*(SORT(.fini_array.*)))
    KEEP (*(.fini_array*))
    PROVIDE_HIDDEN (__fini_array_end = .);
    . = ALIGN(4);
  } >RAM

  _sidata = LOADADDR(.data);

  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)

    . = ALIGN(4);
    _edata = .;
    ASSERT ((_edata < _fsbl_start_address), "Error : Application will erase fsbl");
  } >RAM

  .noncacheable :
  {
    . = ALIGN(8);
    __snoncacheable = .;
    KEEP(*(noncacheable_buffer))
    KEEP(*(.noncacheable))
    . = ALIGN(8);
    __enoncacheable = .;
  } >RAM

  .gnu.sgstubs :
  {
    . = ALIGN(4);
    *(.gnu.sgstubs*)
    . = ALIGN(4);
  } >RAM

  . = ALIGN(4);
  .bss :
  {
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)

    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE ( end = . );
    PROVIDE ( _end = . );
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  /* Large LCD / NN buffers (landmarks IN_PSRAM → .psram_bss) */
  .psram_section (NOLOAD):
  {
    . = ALIGN(32);
    *(.psram_bss)
    . = ALIGN(32);
  } >PSRAM

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }

  .ARM.attributes 0 : { *(.ARM.attributes) }
}
