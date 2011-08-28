/*
    Copyright � 1995-2011, The AROS Development Team. All rights reserved.
    $Id$

    Desc: Early bootup section
    Lang: english
*/

/*
    AROS MemoryMap:
     
    First 4KB of RAM (page 0x0000-0x0FFF) are READ ONLY!
          
    +------------+-----------+-------------------------------------------------+
    |    address |    length |                                     description |
    +------------+-----------+-------------------------------------------------+
    |   System page. Read only! Addresses undefined here are not allowed!!!!   |    
    +------------------------+-------------------------------------------------+
    | 0x00000004 | 0x0000004 |                               ExecBase pointer. |
    | 0x00000020 | 0x0000020 |                                 CPU information |
    | 0x00000040 | 0x0000068 |                      TSS structure. Do not use! |
    | 0x000000a8 | 0x0000004 | INT server!                     cached_irq_mask |
    | 0x000000ac | 0x0000004 | INT server!                        io_apic_irqs |
    | 0x00000100 | 0x0000800 |                           256 interrupt vectors |
    | 0x00000900 | 0x0000040 |                         Global Descriptor Table |<---- needs changed! - must have space for GDT x No CPUs (NicJA)
    | 0x00000940 | 0x0000001 | INT server!                           softblock |
    | 0x00000a00 | 0x0000200 | INT server!                          irq_desc[] |
    +------------+-----------+-------------------------------------------------+
    | 0x00001000 | 0x0001000 |                           MultiBoot information |
    | 0x00002000 | ......... |                                      System RAM |
    | .......... | ......... |        Temporary stack frame for bootup process |
......0x0009e000   0x0001000                                                            SMP Trampoline  (NicJA)
    | 0x000a0000 | 0x0060000 | Data reserved for BIOS, VGA and some MMIO cards |
......0x000f7070                                                                        ACPI<-
 1MB| 0x00100000 | ......... |                                     AROS KERNEL |
    | .......... | ......... |           System RAM divided into DMA and rest. |
    +------------+-----------+-------------------------------------------------+ 
        0fef7f80
    WARNING!

        AROS Kernel will not be placed in first 1MB of ram anymore. Instead it
        will be either somewhere in DMA memory (eg at 2nd MByte), at the end of
        available memory or placed at Virtual address. Don't trust its position.

        When ROM module called from loader, the ExecBase should be cleared.      

        This file has been completely rewritten from assembler source!
*/


#include <exec/resident.h>
#include <exec/types.h>
#include <exec/nodes.h>
#include <exec/execbase.h>
#include <exec/memory.h>
#include <hardware/intbits.h>
#include <asm/segments.h>
#include <asm/linkage.h>
#include <asm/ptrace.h>
#include <dos/dosextens.h>

#include <aros/arossupportbase.h>
#include <aros/asmcall.h>
#include <aros/config.h>

#include <aros/kernel.h>

#define DEBUG    1

#include <aros/debug.h>
#include <aros/multiboot.h>

#include <utility/tagitem.h>

#include <hardware/custom.h>
#include <resources/acpi.h>

#include <proto/alib.h>
#include <proto/exec.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <inttypes.h>

#include LC_LIBDEFS_FILE

#include "etask.h"
#include "exec_intern.h"
#include "exec_debug.h"
#include "exec_util.h"
#include "intservers.h"
#include "memory.h"
#include "traps.h"
#include "vesa.h"

#define SMP_SUPPORT 0

#if SMP_SUPPORT
    extern void prepare_primary_cpu(struct ExecBase *SysBase);      /* FUNCTION FROM "cpu.resource"!!!!!!! */
#endif

/* As long as we don't have CPU detection routine, assume FPU to be present */

#define ASSUME_FPU 1

/*
 * Some macro definitions. __text will place given structure in .text section.
 * __no_ret will force function type to be no-return function. __packed will
 * force any structure to be non-aligned (we don't need alignment in native
 * AROS).
 */

#define	__text      __attribute__((section(".text")))
#define __no_ret    __attribute__((noreturn))
#define __packed    __attribute__((packed))

#define rdcr(reg) \
    ({ long val; asm volatile("mov %%" #reg ",%0":"=r"(val)); val; })

#define wrcr(reg, val) \
    do { asm volatile("mov %0,%%" #reg::"r"(val)); } while(0)

#define cpuid(num, eax, ebx, ecx, edx) \
    do { asm volatile("cpuid":"=a"(eax),"=b"(ebx),"=c"(ecx),"=d"(edx):"a"(num)); } while(0)


/*
 * Some declarations
 */
void    get_ACPI_RSDPTR(struct arosmb *mb);
void    exec_cinit() __no_ret;      /* c-style init NEVER returns */
int     exec_check_base();
void    exec_DefaultTrap();
//void    exec_CheckCPU();
int 	exec_RamCheck_fast();
int 	exec_RamCheck_dma();
unsigned char setupVesa(struct multiboot *mbinfo);


asmlinkage void Exec_SystemCall(struct pt_regs);
ULONG   **exec_RomTagScanner(struct ExecBase *, struct TagItem *);
void    irqSetup(void);

extern const UBYTE LIBEND __text;             /* Somewhere in library */
extern ULONG _edata,_end;                       /* They are standard for ELF */
extern const APTR LIBFUNCTABLE[] __text;

extern struct Library * pc_PrepareAROSSupportBase (void);
extern void AROS_SLIB_ENTRY(SerialRawIOInit, Exec, 84)();
extern void AROS_SLIB_ENTRY(SerialRawPutChar, Exec, 86)(UBYTE chr);
extern void AROS_SLIB_ENTRY(MemoryRawIOInit, Exec, 84)();
extern void AROS_SLIB_ENTRY(MemoryRawPutChar, Exec, 86)(UBYTE chr);

extern void AROS_SLIB_ENTRY(Switch_FPU, Exec, 9)();
extern void AROS_SLIB_ENTRY(PrepareContext_FPU, Exec, 6)();
extern void AROS_SLIB_ENTRY(Dispatch_FPU, Exec, 10)();

extern void AROS_SLIB_ENTRY(Switch_SSE, Exec, 9)();
extern void AROS_SLIB_ENTRY(PrepareContext_SSE, Exec, 6)();
extern void AROS_SLIB_ENTRY(Dispatch_SSE, Exec, 10)();
extern void AROS_SLIB_ENTRY(CopyMem_SSE, Exec, 104)();


#undef memcpy
#define memcpy(_d, _s, _len)                     \
{                                                \
    int len = _len;                              \
    while (len)                                  \
    {                                            \
       ((char *)_d)[len-1] = ((char *)_s)[len-1];\
       len--;                                    \
    }                                            \
}


/* Temporary information */

#if 1

void clr();
void scr_RawPutChars(char *, int);

char tab[127];

#ifdef rkprintf
# undef rkprintf
#endif
#define rkprintf(x...)	scr_RawPutChars(tab, snprintf(tab,126, x))

#else

#define clr() /* eps */
#define rkprintf(x...) /* eps */

#endif

/*
 * Make SysBase global. This way we will have no problems with all crapy modules
 * which rely on global SysBase. They will get what they want - they will access
 * it :)
 */

asm(        ".globl aros_intern\n\t"
            ".globl SysBase     \n\t"
            ".set   aros_intern,0\n\t"
            ".set   SysBase,4       ");


const char exec_core[] __text       = "Native/CORE v2.0.1";

/*
 * First, we will define exec.library (global) to make it usable outside this
 * file.
 */
const char exec_name[] __text       = "exec.library";

/* Now ID string as it will be used in a minute in resident structure. */
const char exec_idstring[] __text = VERSION_STRING;

/* We would need also version and revision fields placed somewhere here. */
const short exec_Version __text     = VERSION_NUMBER;
const short exec_Revision __text    = REVISION_NUMBER;

/*
 * The RomTag structure. It has to be placed inside .text block as there will
 * be no public RomTagList. In future we may change RTC_MATCHWORD to be machine
 * specific.
 */
const struct Resident Exec_resident __text=
{
    RTC_MATCHWORD,          /* Magic value used to find resident */
    &Exec_resident,         /* Points to Resident itself */
    (APTR)&LIBEND,          /* Where could we find next Resident? */
    0,                      /* There are no flags!! */
    VERSION_NUMBER,         /* Version */
    NT_LIBRARY,             /* Type */
    126,                    /* Very high startup priority. */
    (char *)exec_name,      /* Pointer to name string */
    (char *)exec_idstring,  /* Ditto */
    NULL               /* Library initializer (for exec this value is irrelevant since we've jumped there at the begining to bring the system up */
};

struct view { unsigned char sign; unsigned char attr; };

/*
 * Mixed stuff used to control TSS/GDT/IDT stuff. This is strictly machine
 * specific
 */

asm(".globl TSS\n\t"            /* Make these three global in case one would */
    ".globl SSP\n\t"            /* decide to use them directly */    
    ".globl EIP\n\t"
    ".set   TSS, 0x00000040\n\t"    /* See Memory Map at the top of the file */
    ".set   SSP, 0x00000044\n\t"
    ".set   EIP, 0x00000060");

/*
 * Two defines used to keep control over interrupt server
 */

asm(".globl cached_irq_mask\n\t"
    ".globl io_apic_irqs\n\t"
    ".globl softblock\n\t"
    ".globl irq_desc\n\t"
    ".set   cached_irq_mask, 0x000000a8\n\t"
    ".set   io_apic_irqs,    0x000000ac\n\t"
    ".set   softblock,       0x00000940\n\t"
    ".set   irq_desc,        0x00000a00");

/*
 * TaskStateStructure, defined only in matter of making life (setup)
 * more human-readable
 */
struct _tss {
    ULONG   link,               /* link to previous task        - UNUSED        */
            ssp,                /* Supervisor Stack Pointer                     */
            ssp_seg,            /* SSP descriptor                               */
            t0,t1,              /* Stack for CPL1 code          - USE IN FUTURE */
            t2,t3,              /* Stack for CPL2 code          - UNUSED        */
            cr3,                /* used in paging                               */
            eip,                /* Instruction pointer                          */
            eflags,             /* Flags for given task                         */
            r0,r1,r2,r3,        /* 8 general purpouse registers                 */
            r4,r5,r6,r7,
            es,cs,ss,ds,fs,gs,  /* segment descriptors                          */
            ldt;                /* LocalDescriptorTable         - UNUSED        */
    UWORD   trap,iomap;         /* trap flag and iomap pointer                  */
};

/*
 * Nice macro used for setting given gate to specified type.
 * dpl is used to control access of this gate - setting to something
 * lower than 3 makes gate impossible to use from User Mode
 */
#define set_gate(gate_addr,type,dpl,addr) \
do { \
  int __d0, __d1; \
  __asm__ __volatile__ ("movw %%dx,%%ax\n\t" \
    "movw %4,%%dx\n\t" \
    "movl %%eax,%0\n\t" \
    "movl %%edx,%1" \
    :"=m" (*((long *) (gate_addr))), \
     "=m" (*(1+(long *) (gate_addr))), "=&a" (__d0), "=&d" (__d1) \
    :"i" ((short) (0x8000+(dpl<<13)+(type<<8))), \
     "3" ((char *) (addr)),"2" (KERNEL_CS << 16)); \
} while (0)

/* Empty interrupt. Used for short while */
void exec_DummyInt();
asm("\nexec_DummyInt:   iret");

/*
 * RO copy of GlobalDescriptorTable. It's easier to copy this table than
 * to generate completely new one
 */
const struct {UWORD l1, l2, l3, l4;}
    GDT_Table[] __text = {
        { 0x0000, 0x0000, 0x0000, 0x0000 },
        { 0xffff, 0x0000, 0x9a00, 0x00cf },
        { 0xffff, 0x0000, 0x9200, 0x00cf },
        { 0xffff, 0x0000, 0xfa00, 0x00cf },
        { 0xffff, 0x0000, 0xf200, 0x00cf },
        { 0x0000, 0x0000, 0x0000, 0x0000 },
        { 0x0067, 0x0040, 0x8900, 0x0000 },
        { 0x0000, 0x0000, 0x0000, 0x0000 }};

/* Two magic registers pointing to Global Descriptor Table and Interrupt
 * Descriptor Table */
const struct
{
    UWORD l1 __packed;
    ULONG l3 __packed;
}
GDT_reg __text = {0x3f,  0x900},
IDT_reg __text = {0x7ff, 0x100};

const char exec_chipname[] __text = "Chip Memory";
const char exec_fastname[] __text = "Fast Memory";

struct arosmb *arosmb;

/*
 * Here the real game begins...
 * Init the exec.library and as well whole system. This routine has to prepare
 * all needed structures, GDT and IDT tables, TSS structure and many other.
 */
void exec_cinit(struct TagItem *tags, struct multiboot *mbinfo)
{
    struct ExecBase *ExecBase;
    char *opts;
    ULONG locmem, extmem;
    ULONG   negsize = 0;             	/* size of vector table */
    void **fp = (void **)LIBFUNCTABLE;  /* pointer to a function in the table */
    APTR KickMemPtr = NULL;
    APTR KickTagPtr = NULL;
    APTR KickCheckSum = NULL;

    struct _tss *tss = (struct _tss *)0x40; /* Dummy pointer making life easier */
    long long *idt = (long long *)0x100;    /* Ditto */

    int i;                                  /* Whatever? Counter? Erm... */

    clr();
    rkprintf("AROS - The AROS Research OS\nCompiled %s\n\n",__DATE__);

    /* MultiBoot
     * This messy bit here will store away useful information we receive from
     * the bootloader. It will happen when we are loaded, and not on when we
     * are here from ColdReboot().
     * This is just the first stage of it. Later this information will be extracted
     * by bootloader.resource, but we do this here anyway. Both to keep the info
     * safe, and also since we will use some of it in here.
     */
    arosmb = (struct arosmb *)0x1000;
    if (mbinfo)
    {
	if (mbinfo->flags & MB_FLAGS_CMDLINE)
	    arosmb->vbe_palette_width = setupVesa(mbinfo);
        rkprintf("Copying multiboot information into storage\n");
        arosmb->magic = MBRAM_VALID;
        arosmb->flags = 0L;
        if (mbinfo->flags & MB_FLAGS_MEM)
        {
            arosmb->flags |= MB_FLAGS_MEM;
            arosmb->mem_lower = mbinfo->mem_lower;
            arosmb->mem_upper = mbinfo->mem_upper;
        }
        if (mbinfo->flags & MB_FLAGS_LDRNAME)
        {
            arosmb->flags |= MB_FLAGS_LDRNAME;
            snprintf(arosmb->ldrname,29,"%s",(char *)mbinfo->loader_name);
        }
        if (mbinfo->flags & MB_FLAGS_CMDLINE)
        {
            arosmb->flags |= MB_FLAGS_CMDLINE;
            snprintf(arosmb->cmdline,199,"%s",(char *)mbinfo->cmdline);
        }
        if (mbinfo->flags & MB_FLAGS_MMAP)
        {
            arosmb->flags |= MB_FLAGS_MMAP;
            arosmb->mmap_addr = ((IPTR)(0x1000 + sizeof(struct arosmb)));
            arosmb->mmap_len = mbinfo->mmap_length;
            memcpy((void *)arosmb->mmap_addr,(void *)mbinfo->mmap_addr,mbinfo->mmap_length);
        }
        if (mbinfo->flags & MB_FLAGS_DRIVES)
        {
            if (mbinfo->drives_length > 0)
            {
                arosmb->flags |= MB_FLAGS_DRIVES;
                arosmb->drives_addr = ((ULONG)(arosmb->mmap_addr + arosmb->mmap_len));
                arosmb->drives_len = mbinfo->drives_length;
                memcpy((void *)arosmb->drives_addr,(void *)mbinfo->drives_addr,mbinfo->drives_length);
            }
        }
        if (mbinfo->flags & MB_FLAGS_GFX)
        {
            arosmb->flags |= MB_FLAGS_GFX;
            arosmb->vbe_mode = mbinfo->vbe_mode;
            memcpy((void *)&arosmb->vmi,(void *)mbinfo->vbe_mode_info,sizeof(struct vbe_mode));
            memcpy((void *)&arosmb->vci,(void *)mbinfo->vbe_control_info,sizeof(struct vbe_controller));
        }
    }
    rkprintf("Done\n");

/* TODO: WE MUST PARSE THE BIOS MEMORY MAP HERE AND PROTECT NECESSARY STRUCTS (i.e ACPI stores its data in the last few meg of physical ram..) */

    /* save the RSP PTR */
    get_ACPI_RSDPTR(arosmb);

    rkprintf("Clearing system area...");

    /*
     * Well, if we all in clearing moode, then it's the best occasion to clear
     * some areas in AROS private 4KB ram area. Hmmm. If there would occur any
     * interrupt at this moment, AROS would be really dead as it is going to
     * destroy its private stuff (which is really needed to make CPU operate
     * properly)
     */
    bzero((void *)8, 4096-8);

    /*
     * Feel better? Now! Quick. We will have to build new tables for our CPU to
     * make it work nicely.
     */
    tss->ssp = 0x00090000;      /* temporary Supervisor Stack Pointer */
    tss->ssp_seg = KERNEL_DS;   /* SSP segment descriptor */
    tss->cs = USER_CS;
    tss->ds = USER_DS;
    tss->es = USER_DS;
    tss->ss = USER_DS;
    tss->iomap = 104;

    /* Restore IDT structure. */
    for (i=0; i<256; i++)
    {
        /* Feed all vectors with dummy interrupt */
        set_gate(idt + i, 14, 0, exec_DummyInt);
    }

    /*
     * Fix Global Descriptor Table. Because I'm too lazy I'll just copy one from
     * here. I've already prepared such a nice one :)
     */
    memcpy((void *)0x900, &GDT_Table, 64);

    /*
     * As we prepared all necessary stuff, we can hopefully load GDT and LDT
     * into CPU. We may also play a bit with TSS
     */
    asm("lgdt   %0\n\t"
        "lidt   %1"
        :
        :"m"(GDT_reg),"m"(IDT_reg));

    asm("mov    %0,%%ds\n\t"    /* Now it's high time to set segment   */
        "mov    %0,%%es\n\t"    /* registers (segment descriptors).    */
        "mov    %0,%%ss\n\t"    /* AROS uses only %CS %SS %DS and %ES  */
        "mov    %1,%%fs\n\t"    /* %FS and %GS are set to 0 so we can  */
        "mov    %1,%%gs\n\t"    /* generate GP if someone uses them.   */
        "ljmp   %2,$1f\n1:\n\t" /* And finally, set the %CS!!!         */
        :
        :"a"(KERNEL_DS),"b"(0),"i"(KERNEL_CS));

    asm("ltr    %%ax"::"ax"(0x30));

    rkprintf("OK\nSystem restored\n");

    /*
     * Wew have to do some stuff from setup.S due to remove of it.
     *
     * Remap IRQs. We really HAVE TO do that because IBM did it in a very bad
     * way - they've ignored what Intel said about interrupts. See intel manual
     * for further information.
     * For now all irq's are placed in range 0x20 - 0x30 as vectors 0x00 - 0x1f
     * are reserved by Intel
     */
    asm("outb   %b0,%b1\n\tcall delay"::"a"((char)0x11),"i"(0x20)); /* Initialization sequence for 8259A-1 */
    asm("outb   %b0,%b1\n\tcall delay"::"a"((char)0x11),"i"(0xa0)); /* Initialization sequence for 8259A-2 */
    asm("outb   %b0,%b1\n\tcall delay"::"a"((char)0x20),"i"(0x21)); /* IRQs at 0x20 - 0x27 */
    asm("outb   %b0,%b1\n\tcall delay"::"a"((char)0x28),"i"(0xa1)); /* IRQs at 0x28 - 0x2f */
    asm("outb   %b0,%b1\n\tcall delay"::"a"((char)0x04),"i"(0x21)); /* 8259A-1 is master */
    asm("outb   %b0,%b1\n\tcall delay"::"a"((char)0x02),"i"(0xa1)); /* 8259A-2 is slave */
    asm("outb   %b0,%b1\n\tcall delay"::"a"((char)0x01),"i"(0x21)); /* 8086 mode for both */
    asm("outb   %b0,%b1\n\tcall delay"::"a"((char)0x01),"i"(0xa1));
    asm("outb   %b0,%b1\n\tcall delay"::"a"((char)0xff),"i"(0x21)); /* Enable cascade int */
    asm("outb   %b0,%b1\n\tcall delay"::"a"((char)0xff),"i"(0xa1)); /* Mask all interrupts */

    rkprintf("Interrupts redirected\n");

    /* Calculate the size of the vector table */
    while (*fp++ != (VOID *) -1) negsize += LIB_VECTSIZE;

    /*
     * Check for valid ExecBase. This is quite important part, because this way
     * we may determine whether to clear and reconfigure memory or whether we
     * already know everything and may continue further initialisation.
     */
    if (!exec_check_base())
    { 
        rkprintf("Reallocating ExecBase...");
        /*
         * If we managed to reach this point, it means that there was no ExecBase in
         * the memory. That means that we have to calculate amount of memory available.
         *
         * We will guess ExecBase pointer by taking the lowest possible address and
         * subtracting from it the offset of lowest vector used by Exec. This way the ExecBase
         * will be placed in the lowest address possible while fitting all functions :)
         */

        ExecBase = (struct ExecBase *) 0x00002000; /* Got ExecBase at the lowest possible addr */

        /* Check whether we have some FAST memory,
         * If not, then use calculated ExecBase */
        if ((extmem = exec_RamCheck_fast(arosmb)))
        {
    	    rkprintf("%x Fastmem\n",extmem);
            /* We have found some FAST memory. Let's use it for ExecBase */
            ExecBase = (struct ExecBase *) 0x01000000;
        }

        ExecBase += negsize;   /* Subtract lowest vector so jump table would fit */
	
        /*
         * Now, ExecBase is reserved and partially cleared. We have only to
         * determine how much CHIP memory we have.
         */

        locmem = exec_RamCheck_dma(arosmb);
    	rkprintf("%x Chipmem\n",locmem);

        rkprintf("OK\nExecBase=%p\n", ExecBase);
    }
    else
    {
        ExecBase = *(struct ExecBase **)4UL;

        locmem = ExecBase->MaxLocMem;
        extmem = (ULONG)ExecBase->MaxExtMem;

        rkprintf("Got old ExecBase = %p\n",ExecBase);

	/* Store this values as they may point to interesting stuff */
	KickMemPtr = ExecBase->KickMemPtr;
	KickTagPtr = ExecBase->KickTagPtr;
	KickCheckSum = ExecBase->KickCheckSum;
    }
    /*
     * We happen to be here with local ExecBase properly set as well as
     * local locmem and extmem
     */
     
    rkprintf("Initializing ExecBase\n");
    *(struct ExecBase **)4 = ExecBase;
    
    memset(ExecBase, 0, sizeof(struct IntExecBase));
    InitExecBase(ExecBase, negsize, tags);

    /* Bring back saved values */
    ExecBase->KickMemPtr = KickMemPtr;
    ExecBase->KickTagPtr = KickTagPtr;
    ExecBase->KickCheckSum = KickCheckSum;

    rkprintf("Initializing library...");

    /* Set up system stack */
    tss->ssp = (extmem) ? extmem : locmem;  /* Either in FAST or in CHIP */
    ExecBase->SysStkUpper = (APTR)tss->ssp;
    ExecBase->SysStkLower = (APTR)tss->ssp - 0x10000; /* 64KB of system stack */

    /* Store memory configuration */
    ExecBase->MaxLocMem = (IPTR)locmem;
    ExecBase->MaxExtMem = (APTR)extmem;

    /* We have own trap handler */
    ExecBase->TaskTrapCode = exec_DefaultTrap;

    /* Add FAST memory at 0x01000000 to free memory lists */
    if (extmem)
    {
        ULONG base = ((ULONG)ExecBase + sizeof(struct IntExecBase) + 15) & ~15;

        AddMemList(extmem - (base + 0x10000),
            MEMF_FAST | MEMF_PUBLIC | MEMF_KICK | MEMF_LOCAL,
            0,
            (APTR)base,
            (STRPTR)exec_fastname);

        AddMemList(locmem - 0x2000,
            MEMF_CHIP | MEMF_PUBLIC | MEMF_KICK | MEMF_LOCAL | MEMF_24BITDMA,
            -10,
            (APTR)0x2000,
            (STRPTR)exec_chipname);

        rkprintf("Chip Memory : %uMB\nFast Memory : %uMB\n",
                locmem >> 20, (extmem - locmem) >> 20);
    }
    else
    {
        ULONG base = ((ULONG)ExecBase + sizeof(struct IntExecBase) + 15) & ~15;
        AddMemList(locmem - (base + 0x10000),
            MEMF_CHIP | MEMF_PUBLIC | MEMF_KICK | MEMF_LOCAL | MEMF_24BITDMA,
            -10,
            (APTR)base,
            (STRPTR)exec_chipname);

        rkprintf("Chip Memory : %uMB\n", locmem >> 20);
    }

    rkprintf("Memory added\n");

    /* Protect kernel and RO data from being allocated by software */
    IPTR kernel_highest =
        LibGetTagData(KRN_KernelHighest, (ULONG)&_end, tags);
    IPTR kernel_lowest =
        LibGetTagData(KRN_KernelLowest, (ULONG)&_end - 0x000a0000, tags);
    InternalAllocAbs((APTR)kernel_lowest, kernel_highest - kernel_lowest, ExecBase);

    /* Protect bootup stack from being allocated */
    InternalAllocAbs((APTR)0x90000, 0x3000, SysBase);

    /* Protect ACPI & other spaces returned by GRUB loader  */

    /* Protect the RSD PTR which is always in the first MB for later use */
    if(arosmb->acpirsdp)
        InternalAllocAbs((APTR)arosmb->acpirsdp, arosmb->acpilength, ExecBase);

/*
    // tcheko : GRUB returns end of uppermem (fastmem) always lower than ACPI table data
    // protecting TYPE_RESERVED hangs boot process

    struct mb_mmap* mmap = arosmb->mmap_addr;
    for (mmap = (struct mb_mmap *) arosmb->mmap_addr;
                (unsigned long) mmap < arosmb->mmap_addr + arosmb->mmap_len;
                mmap = (struct mb_mmap *) ((unsigned long) mmap + mmap->size + sizeof (mmap->size)))
    {
        if(mmap->type == MMAP_TYPE_ACPIDATA)
        {
            rkprintf("Protecting ACPI DATA memory space (%x:%lu)\n", mmap->addr_low, mmap->len_low);
            InternalAllocAbs(mmap->addr_low, mmap->len_low, ExecBase);
        }         
        if(mmap->type == MMAP_TYPE_ACPINVS)
        {
            rkprintf("Protecting ACPI NVS memory space (%x:%lu)\n", mmap->addr_low, mmap->len_low);
            InternalAllocAbs(mmap->addr_low, mmap->len_low, ExecBase);
        }
        if(mmap->type == MMAP_TYPE_RESERVED)
        {
            rkprintf("Protecting reserved memory space (%x:%lu)\n", mmap->addr_low, mmap->len_low);
            InternalAllocAbs(mmap->addr_low, mmap->len_low, ExecBase);
        }
    }
*/
    rkprintf("Checksumming \"exec.library\"...");

    /* Add exec.library to system library list */
    SumLibrary((struct Library *)SysBase);

    rkprintf("OK\n");

    ExecBase->DebugAROSBase = pc_PrepareAROSSupportBase();

#if SMP_SUPPORT
    /* Early Boot CPU preperation.. */
    prepare_primary_cpu( ExecBase );
#endif

    rkprintf( "[CPU] Primary CPU Probed ..\n" );

    for (i=0; i<16; i++)
    {
        if( (1<<i) & (INTF_PORTS|INTF_COPER|INTF_VERTB|INTF_EXTER|INTF_SETCLR))
        {
            struct Interrupt *is;
            struct SoftIntList *sil;
            is = AllocMem
            (
                sizeof(struct Interrupt) + sizeof(struct SoftIntList),
                MEMF_CLEAR | MEMF_PUBLIC
            );
            if( is == NULL )
            {
                rkprintf("ERROR: Cannot install Interrupt Servers!\n");
            }
            sil = (struct SoftIntList *)((struct Interrupt *)is + 1);

            is->is_Code = &IntServer;
            is->is_Data = sil;
            NEWLIST((struct List *)sil);
            SetIntVector(i,is);
        }
        else
        {
            struct Interrupt *is;
            switch (i)
            {
                case INTB_SOFTINT :
                    is = AllocMem
                    (
                        sizeof(struct Interrupt), 
                        MEMF_CLEAR | MEMF_PUBLIC
                    );
                    if (is == NULL)
                    {
                        rkprintf("Error: Cannot install Interrupt Servers!\n");
                        // Alert(AT_DeadEnd | AN_IntrMem);
                    }
                    is->is_Node.ln_Type = NT_SOFTINT;   //INTERRUPT;
                    is->is_Node.ln_Pri = 0;
                    is->is_Node.ln_Name = "SW Interrupt Dispatcher";
                    is->is_Data = NULL;
                    is->is_Code = (void *)SoftIntDispatch;
                    SetIntVector(i,is);
                    break;
            }
        }
    }

/* TODO: Write CPU detailed detection scheme. Patch proper functions?? */

    Init_Traps();
    irqSetup();
    rkprintf("IRQ services initialized\n");

    /* Create user interrupt used to enter supervisor mode */
    set_gate(idt + 0x80, 14, 3, Exec_SystemCall);

    /* Enable interrupts and set int disable level to -1 */
    asm("sti");
    ExecBase->TDNestCnt = -1;
    ExecBase->IDNestCnt = -1;

    rkprintf("Creating the very first task...");

    /* Create boot task.  Sigh, we actually create a Process sized Task,
	since DOS needs to call things which think it has a Process and
	we don't want to overwrite memory with something strange do we?

	We do this until at least we can boot dos more cleanly.
    */
    {
        struct Task    *t;
        struct MemList *ml;

        ml = (struct MemList *)AllocMem(sizeof(struct MemList), MEMF_PUBLIC|MEMF_CLEAR);
        t  = (struct Task *)   AllocMem(sizeof(struct Process), MEMF_PUBLIC|MEMF_CLEAR);

        if( !ml || !t )
        {
            rkprintf("ERROR: Cannot create Boot Task!\n");
        }
        ml->ml_NumEntries = 1;
        ml->ml_ME[0].me_Addr = t;
        ml->ml_ME[0].me_Length = sizeof(struct Process);

        NEWLIST(&t->tc_MemEntry);
        NEWLIST(&((struct Process *)t)->pr_MsgPort.mp_MsgList);

        /* It's the boot process that RunCommand()s the boot shell, so we
           must have this list initialized */
        NEWLIST((struct List *)&((struct Process *)t)->pr_LocalVars);

        AddHead(&t->tc_MemEntry,&ml->ml_Node);

        t->tc_Node.ln_Name = (char *)exec_name;
        t->tc_Node.ln_Pri = 0;
        t->tc_Node.ln_Type = NT_TASK;
        t->tc_State = TS_RUN;
        t->tc_SigAlloc = 0xFFFF;
        t->tc_SPLower = 0;	    /* This is the system's stack */
        t->tc_SPUpper = (APTR)~0UL;
        t->tc_Flags |= TF_ETASK;

        if (t->tc_Flags & TF_ETASK)
        {
            t->tc_UnionETask.tc_ETask = AllocVec
            (
        	sizeof(struct IntETask), 
        	MEMF_ANY|MEMF_CLEAR
            );

            if (!t->tc_UnionETask.tc_ETask)
            {
                rkprintf("Not enough memory for first task\n");
            }

            /* Initialise the ETask data. */
            InitETask(t, t->tc_UnionETask.tc_ETask);

            GetIntETask(t)->iet_Context = AllocTaskMem(t
                , SIZEOF_ALL_REGISTERS
                , MEMF_PUBLIC|MEMF_CLEAR
            );

            if (!GetIntETask(t)->iet_Context)
            {
                rkprintf("Not enough memory for first task\n");
            }
        }

        ExecBase->ThisTask = t;
    }

    rkprintf("Done\n");

    /*
	Check whether the CPU supports SSE and FXSAVE.
	
	Dirty check, without use of any defines and human readable constants. The
	cpuid instruction with %eax=1 will return some essential cpu informations in %edx back, 
	including:
	- bit 24: CPU does support FXSAVE and FXRSTOR for MMX/FPU/SSE context saving and restoring
	- bit 25: CPU supports SSE
	- bit 26: CPU supports SSE2
    
    */
    ULONG v1,v2,v3,v4;
    cpuid(1, v1,v2,v3,v4);

    rkprintf("cpuid 1 = %08x %08x %08x %08x\n", v1, v2, v3, v4);
    
    if (v4 & (1 << 24))
    {
	rkprintf("The CPU supports FXSAVE and FXRSTOR. Good.\n");
	rkprintf("CPU Supports ");
	
	switch ((v4 >> 25) & 3)
	{
	    case 3:
	    case 2:
		rkprintf("SSE2 ");
	    case 1:
		rkprintf("SSE\n");
		/* Patch exec with some SSE-aware functions */
        SetFunction(&ExecBase->LibNode, -6*LIB_VECTSIZE, AROS_SLIB_ENTRY(PrepareContext_SSE, Exec, 6));
        SetFunction(&ExecBase->LibNode, -9*LIB_VECTSIZE, AROS_SLIB_ENTRY(Switch_SSE, Exec, 9));
        SetFunction(&ExecBase->LibNode, -10*LIB_VECTSIZE, AROS_SLIB_ENTRY(Dispatch_SSE, Exec, 10));
        SetFunction(&ExecBase->LibNode, -104*LIB_VECTSIZE, AROS_SLIB_ENTRY(CopyMem_SSE, Exec, 104));
		SetFunction(&ExecBase->LibNode, -105*LIB_VECTSIZE, AROS_SLIB_ENTRY(CopyMem_SSE, Exec, 104));
		/* tell the CPU that we will support SSE */
		wrcr(cr4, rdcr(cr4) | (3 << 9));
		/* Clear the EM and MP flags of CR0 */
		wrcr(cr0, rdcr(cr0) & ~6);	
		rkprintf("SSE enabled.\n");
		break;

	    default:
		/* 
		    Ha! Bloody PentiumII does supports MMX/FPU/SSE saving instructions,
		    but it does not support SSE 
		*/
		rkprintf("no SSE. Sorry :)\n");
		break;
	}	
    }
    else
    { 
#if ASSUME_FPU
        SetFunction(&ExecBase->LibNode, -6*LIB_VECTSIZE, AROS_SLIB_ENTRY(PrepareContext_FPU, Exec, 6));
        SetFunction(&ExecBase->LibNode, -9*LIB_VECTSIZE, AROS_SLIB_ENTRY(Switch_FPU, Exec, 9));
        SetFunction(&ExecBase->LibNode, -10*LIB_VECTSIZE, AROS_SLIB_ENTRY(Dispatch_FPU, Exec, 10));

	rkprintf("FPU enabled.\n");
#endif
    }


    rkprintf("Jumping out from Supervisor mode...");

#if 0

    asm("mov %0,%%ds\n\t"   /* User DS */
	"mov %0,%%es\n\t"       /* User ES */
	"movl %%esp,%%ebx\n\t"  /* Hold the esp value before pushing! */
	"pushl %0\n\t"          /* User SS */
	"pushl %%ebx\n\t"       /* Stack frame */
	"pushl $0x3002\n\t"     /* IOPL:3 */
	"pushl %1\n\t"          /* User CS */
	"pushl $1f\n\t"         /* Entry address */
	"iret\n"                /* Go down to the user mode */
	"1:\tsti"               /* Enable interrupts */
	:
	: "eax"(USER_DS),"ecx"(USER_CS));
#else
#    define _stringify(x) #x
#    define stringify(x) _stringify(x)

    asm("movl $" stringify(USER_DS) ",%%eax\n\t"
        "mov %%eax,%%ds\n\t"                         /* User DS */
	"mov %%eax,%%es\n\t"                         /* User ES */
	"movl %%esp,%%ebx\n\t"			/* Hold the esp value before pushing! */
	"pushl %%eax\n\t"                           /* User SS */
	"pushl %%ebx\n\t"                           /* Stack frame */
	"pushl $0x3002\n\t"                        /* IOPL:3 */
	"pushl $" stringify(USER_CS) "\n\t"        /* User CS */
	"pushl $1f\n\t"                            /* Entry address */
	"iret\n"                                   /* Go down to the user mode */
	"1:\tsti":::"eax","ebx");                                /* Enable interrupts */

#   undef stringify
#   undef _stringify
#endif

    rkprintf("Done\n");

    ExecBase->TDNestCnt++;
    Permit();

    /* Enable type of debug output chosen by user */
    if (strstr(arosmb->cmdline, "debug=serial"))
    {
        SetFunction(&ExecBase->LibNode, -84 * LIB_VECTSIZE,
            AROS_SLIB_ENTRY(SerialRawIOInit, Exec, 84));
        SetFunction(&ExecBase->LibNode, -86 * LIB_VECTSIZE,
            AROS_SLIB_ENTRY(SerialRawPutChar, Exec, 86));
    }
    else if (strstr(arosmb->cmdline, "debug=memory"))
    {
        SetFunction(&ExecBase->LibNode, -84 * LIB_VECTSIZE,
            AROS_SLIB_ENTRY(MemoryRawIOInit, Exec, 84));
        SetFunction(&ExecBase->LibNode, -86 * LIB_VECTSIZE,
            AROS_SLIB_ENTRY(MemoryRawPutChar, Exec, 86));
    }
    PrivExecBase(SysBase)->PageSize = MEMCHUNK_TOTAL; /* temp fix for debug=memory */
    RawIOInit();

    /* Scan for valid RomTags */
    ExecBase->ResModules = exec_RomTagScanner(ExecBase, tags);

    if (ExecBase->CoolCapture)
    {
        void (*p)() = ExecBase->CoolCapture;
 
	rkprintf("Executing CoolCapture at 0x%p\n", p);
	(*p)();
    }

    InitCode(RTF_SINGLETASK, 0);
    InitCode(RTF_COLDSTART, 0);
    
    /*
     * We suppose that at this point dos.library has already taken over.
     * The last thing to do is to call WarmCapture vector. After that this
     * task has completely nothing to do so it may execute Debug() in
     * forever loop
     */
/* InitCode(RTF_COLDSTART) never returns!!!
    if (ExecBase->WarmCapture)
    {
        void (*p)() = ExecBase->WarmCapture;

        (*p)();
    } */

    rkprintf("Failed to start up the system!");
    do { Debug(0); } while(1);
}

/* Small delay routine used by exec_cinit initializer */
asm("\ndelay:\t.short   0x00eb\n\tret");

/* Exec default trap routine */
asm("\nexec_DefaultTrap:\n\t"
    "popl   %eax\n\t"
    "popl   %eax\n\t"
    "pushl  4\n\t"
    "pushl  %eax\n\t"
    "call    Exec_18_Alert");



void get_ACPI_RSDPTR(struct arosmb* mb)
{
	unsigned char *j, *k, sum=0;
	
    /* Finding RSD PTR */
    for (j = (unsigned char*)0x000E0000; j < (unsigned char*)(0x000E0000 + 0x00020000); j += 16)
    {
        
        /* The signature and checksum must both be correct */
        if(j[0] == 'R' && j[1] == 'S' && j[2] == 'D' && j[3] == ' ' && j[4] == 'P' && j[5] == 'T' && j[6] == 'R' && j[7] == ' ')
        { 
            
            /* We have the signature, let's check the checksum*/ 
        	k = j + ((((struct ACPI_TABLE_TYPE_RSDP*)j)->revision < 2)?20:36);		/* revision is stored at index 15 */

	        for (; j < k; sum += *(j++));
	        
    		if(!sum)
            {
                rkprintf("ACPI RSD PTR address found at %p!\n", j);
                mb->acpirsdp = (IPTR)j;
                mb->acpilength = (((struct ACPI_TABLE_TYPE_RSDP*)j)->revision < 2)?20:36;
                break;
            }
            else
            {
                rkprintf("Wrong ACPI RSDP checksum\n");
                break;
            }
        }
    }
}



/* TODO: We should use info from BIOS here. */
int exec_RamCheck_dma(struct arosmb *arosmb)
{
    ULONG   volatile *ptr,tmp;

    ptr = (ULONG *)(((int)&_end + 0x0FFF) &~0x0FFF);

    if(arosmb->flags & MB_FLAGS_MEM)
    {
	/* If there is upper memory, assume that lower is 1MB. Dirty hack++ */
	if(arosmb->mem_upper)
	{
	    if ((arosmb->mem_upper<<10) & 0xff000000)
	    {
		/* More than 16MB in total, return 16 */
		return 16<<20;
	    }
	    /* Lower 16MB is marked as DMA memory */
	    tmp = (arosmb->mem_upper * 1024) & 0x00ffffff;
	    tmp += 0x100000;
	    return tmp;
	}
	else
	{
	    /* No upper memory, return only lower mem.
	     * Most likely fatal, can't see aros working with less than one MB of ram */
	    return (arosmb->mem_lower * 1024);
	}
    }
    /* No memory info from bios, do a scan */
    do
    {
        tmp = *ptr;
        *ptr = 0xdeadbeef;
        if (*ptr != 0xdeadbeef)
            break;
        *ptr = tmp;
        ptr += 4;
    } while ((int)ptr < 0x01000000);

    return (int)ptr;
}

int exec_RamCheck_fast(struct arosmb *arosmb)
{
    ULONG volatile *ptr, tmp;

    ptr = (ULONG *)0x01000000;

    if(arosmb->flags & MB_FLAGS_MEM)
    {
	    /* If less than 15MB upper, no fastmem here */
	    if ((arosmb->mem_upper * 1024) <= 0xF00000)
	    {
	        return 0;
	    }
	    /* Found memory, so we need to do some quick math */
	    tmp = (arosmb->mem_upper * 1024) + 0x100000;
	    return tmp;
    }
    /* No memory info from bios, do a scan */
    do
    {
        tmp = *ptr;
        *ptr = 0xdeadbeef;
        if (*ptr != 0xdeadbeef)
            break;
        *ptr = tmp;
        ptr += 4;
    } while(1);

    return ((int)ptr > 0x01000000) ? (int)ptr : 0;
}

/*
 * Check for valid ExecBase
 */
int exec_check_base()
{
    /* Get ExecBase from 0x00000004 */
    struct ExecBase *ExecBase=*(struct ExecBase **)4UL;

    /* Attempt first test. If ExecBase is not aligned to 4 byte boundary then
     * it is not proper ExecBase */
    if (!((ULONG)ExecBase & 0x3))
    {
        /*
         * Assume for a while, that it is ExecBase indeed. Check complement of
         * this pointer, which is stored in ExecBase->ChkBase
         */
        if (!~((LONG)ExecBase + ExecBase->ChkBase))
        {
            /*
             * Still here? Nice, let's check one more thing. Static part of
             * ExecBase has its checksum calculated. Verify it please...
             */
            UWORD sum=0, *ptr = &ExecBase->SoftVer;
            int i=((int)&ExecBase->IntVects[0] - (int)&ExecBase->SoftVer) / 2,
                j;

            /* Calculate sum for every static part from SoftVer to ChkSum */
            for (j=0;j < i;j++)
            {
                sum+=*(ptr++);
            }

            /* Do we have proper checksum? */
            if (!~sum)
            {
                /* Well well, almost sure that we have ExecBase here. Hey, it's
                 * time to execute ColdCapture code! */
                void (*p)() = ExecBase->ColdCapture;

                /* Only if there is ColdCapture code... */
                if (p)
                {
                    ExecBase->ColdCapture = NULL;
                    (*p)();
                }

                /*
                 * Let's check ExecBase last time. Compare library version and
                 * revision vs ones stored deep in the core. If they will differ
                 * then ExecBase is damaged
                 */

                if ((ExecBase->LibNode.lib_Version == exec_Version) &&
                    (ExecBase->LibNode.lib_Revision == exec_Revision))
                {
                    /*
                     * Really last thing. Check MaxLocMem and MaxExtMem fields
                     * in ExecBase. First cannot be greater than 16MB and smaller
                     * than 2MB, second, if is not zero then has to be greater
                     * than 16MB
                     */

                    if ((ExecBase->MaxLocMem >= 0x00200000) &&
                        (ExecBase->MaxLocMem <= 0x01000000))
                    {
                        if (ExecBase->MaxExtMem && ((ULONG)ExecBase->MaxExtMem < 0x01000000))
                        {
                            return 0;
                        }
                        return 1;
                    }
                }
            }
        }
    }
    return 0;
}

/*
 * RomTag scanner.
 *
 * This function scans kernel for existing Resident modules. If two modules
 * with the same name are found, the one with higher version or priority wins.
 *
 * After building list of kernel modules, the KickTagPtr and KickMemPtr are
 * checksummed. If checksum is proper and all memory pointed in KickMemPtr may
 * be allocated, then all modules from KickTagPtr are added to RT list
 *
 * Afterwards the proper RomTagList is created (see InitCode() for details) and
 * memory after list and nodes is freed.
 */

struct rt_node
{
    struct Node     node;
    struct Resident *module;
};

ULONG **exec_RomTagScanner(struct ExecBase *SysBase, struct TagItem *tags)
{
    struct List     rtList;             /* List of modules */
    UWORD           *ptr = (UWORD*)LibGetTagData(KRN_KernelLowest, (ULONG)&_end, tags);  /* Start looking here */

    struct Resident *res;               /* module found */

    int     i;
    ULONG   **RomTag;

    /* Initialize list */
    NEWLIST(&rtList);

    kprintf("Resident modules (addr: pri version name):\n");
    
    /* Look in whole kernel for resident modules */
    do
    {
        /* Do we have RTC_MATCHWORD? */
        if (*ptr == RTC_MATCHWORD)
        {
            /* Yes, assume we have Resident */
            res = (struct Resident *)ptr;

            /* Does rt_MatchTag point to Resident? */
            if (res == res->rt_MatchTag
                && !(strstr(arosmb->cmdline, "debug=serial") != NULL
                && strcmp(res->rt_Name, "serial.hidd") == 0))
            {
                /* Yes, it is Resident module */
                struct rt_node  *node;

                /* Check if there is module with such name already */
                node = (struct rt_node*)FindName(&rtList, res->rt_Name);
                if (node)
                {
                    /* Yes, there was such module. It it had lower pri then replace it */
                    if (node->node.ln_Pri <= res->rt_Pri)
                    {
                        /* If they have the same Pri but new one has higher Version, replace */
                        if ((node->node.ln_Pri == res->rt_Pri) &&
                            (node->module->rt_Version < res->rt_Version))
                        {
                            node->node.ln_Pri   = res->rt_Pri;
                            node->module        = res;
                        }
                    }
                }
                else
                {
                    /* New module. Allocate some memory for it */
                    node = (struct rt_node *)
                        AllocMem(sizeof(struct rt_node),MEMF_PUBLIC|MEMF_CLEAR);

                    if (node)
                    {
                        node->node.ln_Name  = (char *)res->rt_Name;
                        node->node.ln_Pri   = res->rt_Pri;
                        node->module        = res;

                        Enqueue(&rtList,(struct Node*)node);
                    }
                }

                /* Get address of EndOfResident from RomTag but only when it's
                 * higher then present one - this avoids strange locks when 
                 * not all modules have Resident structure in .text section */
                ptr = ((ULONG)res->rt_EndSkip > (ULONG)ptr) ? (UWORD *)res->rt_EndSkip : ptr + 2;
                if ((ULONG)ptr & 0x01)
                   ptr = (UWORD *)((ULONG)ptr+1);
                continue;
            }
        }

        /* Get next address... */
        ptr++;
    } while (ptr < (UWORD*)LibGetTagData(KRN_KernelHighest, (ULONG)&_end, tags));
    
    /*
     * By now we have valid (and sorted) list of kernel resident modules.
     *
     * Now, we will have to analyze used-defined RomTags (via KickTagPtr and
     * KickMemPtr)
     */
/* TODO: Implement external modules! */

    /*
     * Everything is done now. Allocate buffer for normal RomTag and convert
     * list to RomTag
     */
    
    ListLength(&rtList,i);      /* Get length of the list */

    RomTag = AllocMem((i+1)*4,MEMF_PUBLIC | MEMF_CLEAR);

    if (RomTag)
    {
        int             j;
        struct rt_node  *n;

        for (j=0; j<i; j++)
        {
            n = (struct rt_node *)RemHead(&rtList);
            kprintf("+ 0x%08.8lx: %4d %3d \"%s\"\n",
                n->module,
                n->node.ln_Pri,
                n->module->rt_Version,
                n->node.ln_Name);
            RomTag[j] = (ULONG*)n->module;

            FreeMem(n, sizeof(struct rt_node));
        }
        RomTag[i] = 0;
    }

    return RomTag;
}

struct vbe_controller my_vbe_control;
struct vbe_mode my_vbe_mode;

unsigned char setupVesa(struct multiboot *mbinfo)
{
    char *str = (char *)mbinfo->cmdline;
    char *vesa = strstr(str, "vesa=");
    short r;
    unsigned char palwidth = 0;
    BOOL prioritise_depth = FALSE, set_refresh = FALSE;

    if (vesa)
    {
        long x=0, y=0, d=0, vfreq=0;
        long mode, setmode;
        unsigned long vesa_size = (unsigned long)&_binary_vesa_size;
        void *vesa_start = &_binary_vesa_start;
        vesa+=5;

        while (*vesa >= '0' && *vesa <= '9')
            x = x * 10 + *vesa++ - '0';
        if (*vesa++ == 'x')
        {
            while (*vesa >= '0' && *vesa <= '9')
                y = y * 10 + *vesa++ - '0';
            if (*vesa++ == 'x')
            {
                while (*vesa >= '0' && *vesa <= '9')
                    d = d * 10 + *vesa++ - '0';
            }
            else
                d = 32;
        }
        else
            d = x, x = 10000, y = 10000, prioritise_depth = TRUE;

        /* Check for user-set refresh rate */
        if (*(vesa - 1) == '@')
        {
            while (*vesa >= '0' && *vesa <= '9')
                vfreq = vfreq * 10 + *vesa++ - '0';
            set_refresh = TRUE;
        }
        else
            vfreq = 60;

        rkprintf("[VESA] module (@ %p) size=%ld\n", &_binary_vesa_start, (long)&_binary_vesa_size);
        memcpy((void *)0x1000, vesa_start, vesa_size);
        rkprintf("[VESA] Module installed\n");

        rkprintf("[VESA] BestModeMatch for %ldx%ldx%ld = ", x, y, d);
        mode = findMode(x, y, d, vfreq, prioritise_depth);

        getModeInfo(mode);

	rkprintf("%lx\n",mode);
	if (modeinfo->mode_attributes & 0x80)
	    setmode = mode | 0x4000;
	else
	    setmode = mode;
	r = setVbeMode(setmode, set_refresh);
        if (r == 0x004f) {
	    rkprintf("\x03");
	    if (controllerinfo->capabilities & 0x01)
		paletteWidth(0x0800, &palwidth);
	    else
		palwidth = 6;
	    memcpy(&my_vbe_mode, modeinfo, sizeof(struct vbe_mode));
	    memcpy(&my_vbe_control, controllerinfo, sizeof(struct vbe_controller));
	    mbinfo->vbe_mode_info = (ULONG)&my_vbe_mode;
    	    mbinfo->vbe_control_info = (ULONG)&my_vbe_control;
	    mbinfo->flags |= MB_FLAGS_GFX;
	    mbinfo->vbe_mode = mode;
	} else
	    rkprintf("[VESA] mode setting error: 0x%04X\n", r);
    }
    return palwidth;
}

AROS_LH1(struct ExecBase *, open,
    AROS_LHA(ULONG, version, D0),
        struct ExecBase *, SysBase, 1, Exec)
{
    AROS_LIBFUNC_INIT

    /* I have one more opener. */
    SysBase->LibNode.lib_OpenCnt++;
    return SysBase;
    AROS_LIBFUNC_EXIT
}

AROS_LH0(BPTR, close,
    struct ExecBase *, SysBase, 2, Exec)
{
    AROS_LIBFUNC_INIT

    /* I have one fewer opener. */
    SysBase->LibNode.lib_OpenCnt--;
    return 0;
    AROS_LIBFUNC_EXIT
}

AROS_LH0I(int, null,
    struct ExecBase *, SysBase, 3, Exec)
{
    AROS_LIBFUNC_INIT
    return 0;
    AROS_LIBFUNC_EXIT
}

AROS_LH0I(int, null,
    struct ExecBase *, SysBase, 4, Exec)
{
    AROS_LIBFUNC_INIT
    return 0;
    AROS_LIBFUNC_EXIT
}

/*
    We temporarily redefine kprintf() so we use the real version in case
    we have one of these two fn's called before AROSSupportBase is ready.
*/

#undef kprintf
#undef rkprintf
#undef vkprintf

#define kprintf(x...)
#define rkprintf(x...)
#define vkprintf(x...)

struct Library *pc_PrepareAROSSupportBase(void)
{
    struct AROSSupportBase *AROSSupportBase = 
	AllocMem(sizeof(struct AROSSupportBase), MEMF_CLEAR);
	
    AROSSupportBase->kprintf = (void *)kprintf;
    AROSSupportBase->rkprintf = (void *)rkprintf;
    AROSSupportBase->vkprintf = (void *)vkprintf;

    return (struct Library *)AROSSupportBase;
}
