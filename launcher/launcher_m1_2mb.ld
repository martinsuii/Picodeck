/* M1 Launcher linker script — PicoDeck
 * ----------------------------------------------------
 * Layout:
 *   0x10000000  boot2 (256 B)  — RP2040 second-stage
 *   0x10000100..0x101DFFFF  Game region (empty, ~1.95 MB)
 *   0x101E0000..0x101FAFFF  Launcher (vector table + code, up to 108 KB)
 *   0x101FB000..0x101FDFFF  Recovery (12 KB, separate image)
 *   0x101FE000  Settings (4 KB)
 *   0x101FF000  CF2 (4 KB)
 *
 * Build produces a single ELF spanning 0x10000000..0x101E0000+launcher_size.
 * Post-build script (post_process_boot2.py) patches boot2 jump target
 * from 0x10000100 to the recovery base (0x101FB000) and recomputes CRC32.
 * The recovery image then chain-loads the launcher (0x101E0000).
 */

MEMORY
{
    BOOT2(rx)         : ORIGIN = 0x10000000, LENGTH = 256
    /* PicoPad has 2 MB flash but we only use the first 1 MB. The second
       1 MB (0x10100000..0x101FFFFF) is intentionally left untouched so
       that CircuitPython's CIRCUITPY filesystem (= installed there by
       default) survives reflashing our launcher. Game region thus stays
       at 0x10000100..0x101DFFFF (~1.95 MB) — enough for any practical
       MakeCode Arcade game. */
    LAUNCHER(rx)      : ORIGIN = 0x101E0000, LENGTH = 0x1B000
    /* CF2 region 0x100FF000..0x100FFFFF (= MS PicoDeck picks this for 1 MB) */
    RAM(rwx)          : ORIGIN = 0x20000000, LENGTH = 0x39000 /* top 28K reserved for WiFi shared page + module */
    SCRATCH_X(rwx)    : ORIGIN = 0x20040000, LENGTH = 4K   /* core 0 stack */
    BOOTLOADERRAM(rw) : ORIGIN = 0x20041000, LENGTH = 32   /* boot loader resident RAM */
    SCRATCH_Y(rwx)    : ORIGIN = 0x20041000+32, LENGTH = 4K-32  /* core 1 stack */
}

ENTRY(_reset_handler)

SECTIONS
{
    .flash_begin : {
        __flash_binary_start = .;
    } > BOOT2

    .boot2 : {
        __boot2_start__ = .;
        KEEP (*(.boot2))
        __boot2_end__ = .;
    } > BOOT2

    ASSERT(__boot2_end__ - __boot2_start__ == 256,
        "ERROR: boot2 must be exactly 256 bytes")

    /* Launcher vector table + headers + code */
    .boot3 : {
        __logical_binary_start = .;
        KEEP (*(.vectors))
        KEEP (*(.picopad_info_header))
        KEEP (*(.binary_info_header))
        __binary_info_header_end = .;
        KEEP (*(.embedded_block))
        __embedded_block_end = .;
        KEEP (*(.vector_handlers))
        KEEP (*(.reset))
    } > LAUNCHER

    .text : {
        *(.init)
        *(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:) .text*)
        *(.fini)
        *crtbegin.o(.ctors)
        *crtbegin?.o(.ctors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .ctors)
        *(SORT(.ctors.*))
        *(.ctors)
        *crtbegin.o(.dtors)
        *crtbegin?.o(.dtors)
        *(EXCLUDE_FILE(*crtend?.o *crtend.o) .dtors)
        *(SORT(.dtors.*))
        *(.dtors)

        . = ALIGN(4);
        PROVIDE_HIDDEN (__preinit_array_start = .);
        KEEP(*(SORT(.preinit_array.*)))
        KEEP(*(.preinit_array))
        PROVIDE_HIDDEN (__preinit_array_end = .);

        . = ALIGN(4);
        PROVIDE_HIDDEN (__init_array_start = .);
        KEEP(*(SORT(.init_array.*)))
        KEEP(*(.init_array))
        PROVIDE_HIDDEN (__init_array_end = .);

        . = ALIGN(4);
        PROVIDE_HIDDEN (__fini_array_start = .);
        *(SORT(.fini_array.*))
        *(.fini_array)
        PROVIDE_HIDDEN (__fini_array_end = .);

        *(.eh_frame*)
        . = ALIGN(4);
    } > LAUNCHER

    .rodata : {
        *(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:) .rodata.*)
        . = ALIGN(4);
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.flashdata*)))
        . = ALIGN(4);
    } > LAUNCHER

    .ARM.extab : {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > LAUNCHER

    __exidx_start = .;
    .ARM.exidx : {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > LAUNCHER
    __exidx_end = .;

    . = ALIGN(4);
    __binary_info_start = .;
    .binary_info : {
        KEEP(*(.binary_info.keep.*))
        *(.binary_info.*)
    } > LAUNCHER
    __binary_info_end = .;
    . = ALIGN(4);

    __etext = .;

    .data (READONLY) : {
        __data_start__ = .;
        *(vtable)
        *(.time_critical*)
        *(.text*)
        . = ALIGN(4);
        *(.rodata*)
        . = ALIGN(4);
        *(.data*)
        . = ALIGN(4);
        *(.after_data.*)
        . = ALIGN(4);
        PROVIDE_HIDDEN (__mutex_array_start = .);
        KEEP(*(SORT(.mutex_array.*)))
        KEEP(*(.mutex_array))
        PROVIDE_HIDDEN (__mutex_array_end = .);
        . = ALIGN(4);
        *(.jcr)
        . = ALIGN(4);
        __data_end__ = .;
    } > RAM AT> LAUNCHER

    .ram_vector_table (NOLOAD): {
        . = ALIGN(256);
        *(.ram_vector_table)
    } > RAM

    .uninitialized_data (NOLOAD): {
        . = ALIGN(4);
        *(.uninitialized_data*)
    } > RAM

    .bss (NOLOAD) : {
        . = ALIGN(4);
        __bss_start__ = .;
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.bss*)))
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

    .heap (NOLOAD) : {
        __end__ = .;
        end = __end__;
        KEEP(*(.heap*))
        __HeapLimit = .;
        . = ALIGN(16);
        __malloc_start__ = .;
        /* Heap must never grow into the WiFi shared page (launcher) or the
           handoff/net buffer above the app's RAM (apps). The stack lives in
           SCRATCH_X, separate from this region. */
        __malloc_end__ = ORIGIN(RAM) + 0x38000;  /* 0x38000..0x39000 is the module net/HTTP buffer */
    } > RAM

    .stack0_dummy (COPY) : {
        . = ALIGN(16);
        KEEP (*(.stack0*))
    } > SCRATCH_X

    .bootloaderdata (COPY) : {
        *(.bootloaderdata)
    } > BOOTLOADERRAM

    .stack1_dummy (COPY) : {
        KEEP (*(.stack1*))
    } > SCRATCH_Y

    . = ALIGN(4);
    .flash_end : {
        __flash_binary_end = .;
    } > LAUNCHER

    __StackLimit = ORIGIN(RAM) + LENGTH(RAM);
    __Stack0Top = ORIGIN(SCRATCH_X) + LENGTH(SCRATCH_X);
    __Stack1Top = ORIGIN(SCRATCH_Y) + LENGTH(SCRATCH_Y);
    __Stack0Bottom = __Stack0Top - SIZEOF(.stack0_dummy);
    __Stack1Bottom = __Stack1Top - SIZEOF(.stack1_dummy);
    PROVIDE(__stack = __Stack0Top);
    PROVIDE(__StackTop = __Stack0Top);

    ASSERT(__StackLimit >= __HeapLimit, "region RAM overflowed")
    ASSERT(picopad_info_header - __logical_binary_start == 0xc0,
        "Incorrect offset of Picopad Info Header!")
    ASSERT(__binary_info_header_end - __logical_binary_start <= 256,
        "Binary info must be in first 256 bytes")
    ASSERT(__embedded_block_end - __logical_binary_start <= 4096,
        "Embedded block must be in first 4096 bytes")
}
