/*
 * Generated Linker script file for MK22FN512xxx12
 */

MEMORY
{
  /* Define each memory region */
  PROGRAM_FLASH (rx) : ORIGIN = 0x4000, LENGTH = 0x7B800 /* 0x00004000-0x007f7ff (alias Flash) */  
  SRAM_UPPER (rwx) : ORIGIN = 0x20000000, LENGTH = 0x10000 /* 64K bytes (alias RAM) */  
   SRAM_LOWER (rwx) : ORIGIN = 0x1fff0000, LENGTH = 0x10000  /* (alias RAM2) */  
}

