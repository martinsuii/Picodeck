/* Recovery image linker script — PicoDeck (1 MB layout)
 * -------------------------------------------------------------------
 * The recovery image lives in a dedicated partition in the tail of the
 * launcher region. boot2 jumps here first; recovery either chain-loads
 * the launcher or shows the recovery menu. The game region is untouched.
 *
 *   0x10000000  boot2 (256 B)        — jumps to RECOVERY base
 *   0x10000100..0x100DFFFF  Game     — unchanged
 *   0x100E0000..0x100FAFFF  Launcher — 108 KB
 *   0x100FB000..0x100FDFFF  Recovery — this image (12 KB)
 *   0x100FE000..0x100FEFFF  Settings
 *   0x100FF000..0x100FFFFF  CF2
 */
MEMORY
{
    RECOVERY(rx)      : ORIGIN = 0x100FB000, LENGTH = 0x3000
    RAM(rwx)          : ORIGIN = 0x20000000, LENGTH = 256K
    SCRATCH_X(rwx)    : ORIGIN = 0x20040000, LENGTH = 4K
    BOOTLOADERRAM(rw) : ORIGIN = 0x20041000, LENGTH = 32
    SCRATCH_Y(rwx)    : ORIGIN = 0x20041000+32, LENGTH = 4K-32
}

ENTRY(_reset_handler)

SECTIONS
{
    .flash_begin : {
        __flash_binary_start = .;
    } > RECOVERY

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
    } > RECOVERY

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
    } > RECOVERY

    .rodata : {
        *(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:) .rodata.*)
        . = ALIGN(4);
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.flashdata*)))
        . = ALIGN(4);
    } > RECOVERY

    .ARM.extab : {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > RECOVERY

    __exidx_start = .;
    .ARM.exidx : {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > RECOVERY
    __exidx_end = .;

    . = ALIGN(4);
    __binary_info_start = .;
    .binary_info : {
        KEEP(*(.binary_info.keep.*))
        *(.binary_info.*)
    } > RECOVERY
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
    } > RAM AT> RECOVERY

    .ram_vector_table (NOLOAD): {
        . = ALIGN(256);
        *(.ram_vector_table)
    } > RAM

    .uninitialized_data (NOLOAD): {
        . = ALIGN(4);
        *(.uninitialized_data*)
    } > RAM

    .bss (NOLOAD): {
        . = ALIGN(4);
        __bss_start__ = .;
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.bss*)))
        *(COMMON)
        . = ALIGN(4);
        __bss_end__ = .;
    } > RAM

    .heap (NOLOAD): {
        __end__ = .;
        end = __end__;
        KEEP(*(.heap*))
        __HeapLimit = .;
        . = ALIGN(16);
        __malloc_start__ = .;
        /* Heap must never grow into the WiFi shared page (launcher) or the
           handoff/net buffer above the app's RAM (apps). The stack lives in
           SCRATCH_X, separate from this region. */
        __malloc_end__ = ORIGIN(RAM) + LENGTH(RAM);
    } > RAM

    .stack0_dummy (COPY): {
        . = ALIGN(16);
        KEEP (*(.stack0*))
    } > SCRATCH_X

    .bootloaderdata (COPY): {
        *(.bootloaderdata)
    } > BOOTLOADERRAM

    .stack1_dummy (COPY): {
        KEEP (*(.stack1*))
    } > SCRATCH_Y

    . = ALIGN(4);
    .flash_end : {
        __flash_binary_end = .;
    } > RECOVERY

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
