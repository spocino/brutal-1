#pragma once

#include <brutal/base/defs.h>
#include <brutal/base/error.h>
#include <brutal/base/result.h>
#include <brutal/io/read.h>
#include <brutal/io/std.h>
#include <brutal/io/write.h>
#include <brutal/text/str.h>

#if defined(SYSTEM_EFI)
#    include <embed/efi/io.h>
#elif defined(SYSTEM_POSIX)
#    include <embed/posix/io.h>
#elif defined(SYSTEM_BRUTAL)
#    include <embed/brutal/io.h>
#elif defined(SYSTEM_KERNEL)
#    include <embed/kernel/io.h>
#else
#    error "Unsupported embedder!"
#endif

typedef Result(Error, HostIoFile) HostIoOpenFileResult;

HostIoOpenFileResult host_io_file_open(Str path);

HostIoOpenFileResult host_io_file_create(Str path);

MaybeError host_io_file_close(HostIoFile handle);

IoResult host_io_read_file(HostIoFile handle, uint8_t *data, size_t size);

IoResult host_io_write_file(HostIoFile handle, uint8_t const *data, size_t size);

IoResult host_io_read_std(IoStdChannel channel, uint8_t *data, size_t size);

IoResult host_io_write_std(IoStdChannel channel, uint8_t const *data, size_t size);
