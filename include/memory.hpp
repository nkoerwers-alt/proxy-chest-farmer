#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

namespace mem {
    bool attach(const std::wstring& name);   // resolves pid + module base only
    bool begin_session();                    // short-lived handle
    void end_session();
    void detach();

    bool is_attached();
    DWORD pid();
    uint64_t base();
    size_t module_size();

    bool read(uint64_t addr, void* buf, size_t size);
    bool write(uint64_t addr, const void* buf, size_t size);
    std::vector<uint8_t> read_bytes(uint64_t addr, size_t size);

    template <typename T> T read(uint64_t a, T def = T{}) { T v = def; read(a, &v, sizeof(T)); return v; }
    template <typename T> bool write(uint64_t a, T v) { return write(a, &v, sizeof(T)); }
}