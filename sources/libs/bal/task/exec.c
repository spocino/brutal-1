#include <bal/abi.h>
#include <bal/task/exec.h>
#include <brutal/debug.h>
#include <elf/elf.h>

static void bal_exec_load(BrSpace space, Elf64Header const *elf_header, BrMemObj elf_obj)
{
    Elf64ProgramHeader *prog_header = (Elf64ProgramHeader *)((uint8_t *)elf_header + elf_header->program_header_table_file_offset);

    for (size_t i = 0; i < elf_header->program_header_table_entry_count; i++)
    {
        if (prog_header->type != ELF_PROGRAM_HEADER_LOAD)
        {
            log$("Ignoring segment with type={#0x}", prog_header->type);
            continue;
        }

        size_t size = ALIGN_UP(MAX(prog_header->memory_size, prog_header->file_size), MEM_PAGE_SIZE);

        if (!(prog_header->flags & ELF_PROGRAM_HEADER_WRITABLE) &&
            prog_header->file_size == prog_header->memory_size)
        {
            BrMapArgs prog_map = {
                .space = space,
                .mem_obj = elf_obj,
                .offset = prog_header->file_offset,
                .size = size,
                .vaddr = prog_header->virtual_address,
                .flags = BR_MEM_NONE,
            };

            assert_br_success(br_map(&prog_map));
        }
        else
        {
            BrCreateArgs prog_obj = {
                .type = BR_OBJECT_MEMORY,
                .mem_obj = {
                    .size = size,
                    .flags = BR_MEM_OBJ_NONE,
                },
            };

            assert_br_success(br_create(&prog_obj));

            BrMapArgs prog_local_map = {
                .space = BR_SPACE_SELF,
                .mem_obj = prog_obj.handle,
                .flags = BR_MEM_WRITABLE,
            };

            assert_br_success(br_map(&prog_local_map));

            uint8_t *ptr = (uint8_t *)prog_local_map.vaddr;

            mem_set(ptr, 0, size);
            mem_cpy(ptr, (uint8_t *)elf_header + prog_header->file_offset, prog_header->file_size);

            BrMapArgs prog_map = {
                .space = space,
                .mem_obj = prog_obj.handle,
                .offset = 0,
                .size = 0,
                .vaddr = prog_header->virtual_address,
                .flags = BR_MEM_WRITABLE,
            };

            assert_br_success(br_map(&prog_map));

            assert_br_success(br_unmap(&(BrUnmapArgs){
                .space = BR_SPACE_SELF,
                .vaddr = prog_local_map.vaddr,
                .size = size,
            }));

            assert_br_success(bal_close(prog_obj.handle));
        }

        prog_header = (Elf64ProgramHeader *)((uint8_t *)prog_header + elf_header->program_header_table_entry_size);
    }
}

static uintptr_t bal_exec_stack(BrSpace space, BrExecArgs const *exec_args, BrTaskArgs *start_args)
{
    BrCreateArgs stack_obj = {
        .type = BR_OBJECT_MEMORY,
        .mem_obj = {
            .size = 0x4000,
            .flags = BR_MEM_OBJ_NONE,
        },
    };

    assert_br_success(br_create(&stack_obj));

    BrMapArgs local_stack_map = {
        .space = BR_SPACE_SELF,
        .mem_obj = stack_obj.handle,
        .flags = BR_MEM_WRITABLE,
    };

    assert_br_success(br_map(&local_stack_map));

    uint8_t *base = (uint8_t *)local_stack_map.vaddr + 0x4000;
    uint8_t *head = base;

    start_args->type = exec_args->type;

    switch (exec_args->type)
    {
    case BR_START_CMAIN:
    {
        BrCMainArgs cmain = exec_args->cmain;
        uintptr_t argv[BR_MAX_ARGC + 1];

        for (int i = 0; i < cmain.argc; i++)
        {
            head -= ALIGN_UP(cmain.argv[i].len + 1, 16);
            mem_cpy(head, cmain.argv[i].buf, cmain.argv[i].len);
            head[cmain.argv[i].len] = 0;

            argv[i] = 0xC0000000 - ((uintptr_t)base - (uintptr_t)head);
        }

        argv[cmain.argc] = 0;

        head -= ALIGN_UP(sizeof(argv), 16);
        mem_cpy(head, argv, sizeof(argv));

        start_args->arg1 = cmain.argc;
        start_args->arg2 = 0xC0000000 - ((uintptr_t)base - (uintptr_t)head);
    }
    break;

    case BR_START_ARGS:
    {
        // Nothing to copy
        start_args->arg1 = exec_args->args.arg1;
        start_args->arg2 = exec_args->args.arg2;
        start_args->arg3 = exec_args->args.arg3;
        start_args->arg4 = exec_args->args.arg4;
        start_args->arg5 = exec_args->args.arg5;
    }
    break;

    case BR_START_HANDOVER:
    {
        head -= ALIGN_UP(sizeof(Handover), 16);
        mem_cpy(head, exec_args->handover, sizeof(Handover));
        start_args->arg1 = 0xC0000000 - ((uintptr_t)base - (uintptr_t)head);
    }
    break;

    default:
        break;
    }

    uintptr_t sp = 0xC0000000 - ((uintptr_t)base - (uintptr_t)head);

    BrMapArgs stack_map = {
        .space = space,
        .mem_obj = stack_obj.handle,
        .vaddr = 0xC0000000 - 0x4000,
        .flags = BR_MEM_WRITABLE,
    };

    assert_br_success(br_map(&stack_map));

    BrUnmapArgs local_stack_unmap = {
        .space = BR_SPACE_SELF,
        .vaddr = local_stack_map.vaddr,
        .size = local_stack_map.size,
    };

    assert_br_success(br_unmap(&local_stack_unmap));

    assert_br_success(bal_close(stack_obj.handle));

    return sp;
}

BrResult bal_exec(BrMemObj elf_obj, Str name, BrExecArgs const *args, BrTaskInfos *infos)
{
    BrMapArgs elf_map = {
        .space = BR_SPACE_SELF,
        .mem_obj = elf_obj,
        .flags = BR_MEM_WRITABLE,
    };

    assert_br_success(br_map(&elf_map));

    BrCreateArgs elf_space = {
        .type = BR_OBJECT_SPACE,
        .space = {
            .flags = BR_SPACE_NONE,
        },
    };

    assert_br_success(br_create(&elf_space));

    BrCreateArgs elf_task = {
        .type = BR_OBJECT_TASK,
        .task = {
            .name = str_fix$(StrFix128, name),
            .space = elf_space.handle,
            .caps = BR_CAP_ALL,
        },
    };

    assert_br_success(br_create(&elf_task));

    Elf64Header *elf_header = (Elf64Header *)elf_map.vaddr;

    bal_exec_load(elf_space.handle, elf_header, elf_obj);

    BrTaskArgs task_args;
    uintptr_t sp = bal_exec_stack(elf_space.handle, args, &task_args);

    BrStartArgs elf_start = {
        .task = elf_task.handle,
        .ip = elf_header->entry,
        .args = task_args,
        .sp = sp,
    };

    assert_br_success(br_start(&elf_start));

    assert_br_success(bal_close(elf_space.handle));

    BrUnmapArgs elf_unmap = {
        .space = BR_SPACE_SELF,
        .vaddr = elf_map.vaddr,
        .size = elf_map.size,
    };

    assert_br_success(br_unmap(&elf_unmap));

    infos->handle = elf_task.handle;
    infos->tid = elf_task.id;

    return BR_SUCCESS;
}
