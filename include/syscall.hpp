#pragma once
#include <cstdint>
#include <windows.h>

namespace sc {
    bool init();                                        // resolve SSNs, build stubs
    bool ready();

    NTSTATUS nt_open_process(PHANDLE out, ACCESS_MASK access, uintptr_t pid);
    NTSTATUS nt_close(HANDLE h);
    NTSTATUS nt_read(HANDLE h, uint64_t addr, void* buf, size_t size, size_t* got);
    NTSTATUS nt_write(HANDLE h, uint64_t addr, const void* buf, size_t size, size_t* put);
}