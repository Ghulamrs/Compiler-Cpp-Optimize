/* C6747-ddr.cmd - everything in the C6747's 256 MB EMIFB SDRAM, for a program that
 * needs more than the 128 kB of shared RAM C6747.cmd places it in: Compiler++, whose
 * VM claims 4 MB a run. Every build that is timed against another links with this
 * same file, so SDRAM's latency is in every number alike. */
-heap  0x01000000
-stack 0x00040000

MEMORY
{
    EMIFBSDRAM   o = 0xC0000000  l = 0x10000000     /* 256MB SDRAM Data */
}

SECTIONS
{
    .text          >  EMIFBSDRAM
    .stack         >  EMIFBSDRAM
    .bss           >  EMIFBSDRAM
    .cio           >  EMIFBSDRAM
    .const         >  EMIFBSDRAM
    .data          >  EMIFBSDRAM
    .switch        >  EMIFBSDRAM
    .sysmem        >  EMIFBSDRAM
    .far           >  EMIFBSDRAM
    .args          >  EMIFBSDRAM
    .ppinfo        >  EMIFBSDRAM
    .ppdata        >  EMIFBSDRAM
    .fardata       >  EMIFBSDRAM
    .neardata      >  EMIFBSDRAM
    .rodata        >  EMIFBSDRAM
    .cinit         >  EMIFBSDRAM
    .init_array    >  EMIFBSDRAM
    .c6xabi.exidx  >  EMIFBSDRAM
    .c6xabi.extab  >  EMIFBSDRAM
}
