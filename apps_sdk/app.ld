/* Custom app linker script — PicoDeck
 * ---------------------------------------------------
 * The app runs from the 252 KB strip at APP_SLOT_BASE, below the launcher.
 * RAM is the normal 0x20000000..0x20037800 region; the top 2 KB hold the
 * launcher handoff block (APP_INFO_ADDR) and the top 4 KB hold the WiFi
 * module's HTTP scratch buffer (APP_NET_BUF_ADDR).
 *
 * Core 0 stack lives in SCRATCH_X (0x20040000). SCRATCH_Y belongs to the
 * WiFi module on core 1 and is not touched.
 */

MEMORY
{
    FLASH(rx)         : ORIGIN = 0x100A0000, LENGTH = 0x3F000
    RAM(rwx)          : ORIGIN = 0x20000000, LENGTH = 0x37800
    SCRATCH_X(rwx)    : ORIGIN = 0x20040000, LENGTH = 4K
}

ENTRY(_reset_handler)

SECTIONS
{
    .flash_begin : {
        __flash_binary_start = .;
    } > FLASH

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
    } > FLASH

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
    } > FLASH

    .rodata : {
        *(EXCLUDE_FILE(*libgcc.a: *libc.a:*lib_a-mem*.o *libm.a:) .rodata.*)
        . = ALIGN(4);
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.flashdata*)))
        . = ALIGN(4);
    } > FLASH

    .ARM.extab : {
        *(.ARM.extab* .gnu.linkonce.armextab.*)
    } > FLASH

    __exidx_start = .;
    .ARM.exidx : {
        *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    } > FLASH
    __exidx_end = .;

    . = ALIGN(4);
    __binary_info_start = .;
    .binary_info : {
        KEEP(*(.binary_info.keep.*))
        *(.binary_info.*)
    } > FLASH
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
    } > RAM AT> FLASH

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
        __malloc_end__ = ORIGIN(RAM) + LENGTH(RAM);
    } > RAM

    .stack0_dummy (COPY) : {
        . = ALIGN(16);
        KEEP (*(.stack0*))
    } > SCRATCH_X

    . = ALIGN(4);
    .flash_end : {
        __flash_binary_end = .;
    } > FLASH

    __StackLimit = ORIGIN(RAM) + LENGTH(RAM);
    __Stack0Top = ORIGIN(SCRATCH_X) + LENGTH(SCRATCH_X);
    __Stack0Bottom = __Stack0Top - SIZEOF(.stack0_dummy);
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
