#pragma once

#include <brutal/text.h>
#include "brutal/text/str.h"

// "handover" in ascii
#define HANDOVER_TAG 0x68616e646f766572

typedef enum
{
    HANDOVER_BOOT_SRC_OTHER,
    HANDOVER_BOOT_SRC_BRUTAL_LOADER,
    HANDOVER_BOOT_SRC_STIVALE2,
} HandoverBootSource;

typedef enum
{
    HANDOVER_MMAP_FREE,
    HANDOVER_MMAP_KERNEL_MODULE,
    HANDOVER_MMAP_RESERVED,
    HANDOVER_MMAP_RECLAIMABLE,
    HANDOVER_MMAP_FRAMEBUFFER,
} HandoverMmapType;

static inline Str ho_mmtype_to_str(HandoverMmapType type)
{
    switch (type)
    {
    case HANDOVER_MMAP_FREE:
        return str$("FREE");

    case HANDOVER_MMAP_KERNEL_MODULE:
        return str$("KERNEL");

    case HANDOVER_MMAP_RESERVED:
        return str$("RESERVED");

    case HANDOVER_MMAP_RECLAIMABLE:
        return str$("RECLAIMABLE");

    case HANDOVER_MMAP_FRAMEBUFFER:
        return str$("FRAMEBUFFER");

    default:
        return str$("UNKNOWN");
    }
}

typedef struct
{
    HandoverMmapType type;

    uintptr_t base;
    size_t len;
} HandoverMmapEntry;

typedef struct
{
    size_t size;

#define HANDOVER_MMAP_MAX_SIZE (64)
    HandoverMmapEntry entries[HANDOVER_MMAP_MAX_SIZE];
} HandoverMmap;

typedef struct
{
    bool present;
    uintptr_t addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
} HandoverFramebuffer;

typedef struct
{
    size_t size;
    uintptr_t addr;
    StrFix128 module_name;
} HandoverModule;

typedef struct
{
    size_t size;
#define MAX_MODULE_COUNT 16
    HandoverModule module[MAX_MODULE_COUNT];
} HandoverModules;

typedef struct
{
    bool present;
    StrFix128 cmd_line;
} HandoverCmdLines;

typedef struct
{
    uint64_t tag; // must be HANDOVER_TAG
    HandoverBootSource boolloader_from;

    HandoverMmap mmap;
    HandoverFramebuffer framebuffer;
    HandoverModules modules;
    uintptr_t rsdp;
    HandoverCmdLines cmd_lines;
} Handover;

HandoverModule const *handover_find_module(Handover const *handover, Str name);

void handover_dump(Handover const *handover);
