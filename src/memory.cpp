#include "memory.hpp"
#include "syscall.hpp"
#include <tlhelp32.h>

namespace mem {
    static HANDLE   g_h = nullptr;
    static DWORD    g_pid = 0;
    static uint64_t g_base = 0;
    static size_t   g_msize = 0;
    static bool     g_sys = false;

    static DWORD find_pid(const std::wstring& n) {
        HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (s == INVALID_HANDLE_VALUE) return 0;
        PROCESSENTRY32W pe{ sizeof(pe) };
        DWORD out = 0;
        for (BOOL ok = Process32FirstW(s, &pe); ok; ok = Process32NextW(s, &pe))
            if (n == pe.szExeFile) { out = pe.th32ProcessID; break; }
        CloseHandle(s);
        return out;
    }

    bool attach(const std::wstring& name) {
        detach();
        g_pid = find_pid(name);
        if (!g_pid) return false;

        HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, g_pid);
        if (s == INVALID_HANDLE_VALUE) return false;
        MODULEENTRY32W me{ sizeof(me) };
        for (BOOL ok = Module32FirstW(s, &me); ok; ok = Module32NextW(s, &me))
            if (name == me.szModule) { g_base = (uint64_t)me.modBaseAddr; g_msize = me.modBaseSize; break; }
        CloseHandle(s);

        g_sys = sc::init();
        return g_base != 0;
    }

    bool begin_session() {
        if (g_h) return true;
        const ACCESS_MASK rights = PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION;

        if (g_sys && NT_SUCCESS(sc::nt_open_process(&g_h, rights, g_pid))) return true;

        g_h = OpenProcess(rights | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, g_pid);
        return g_h != nullptr;
    }

    void end_session() {
        if (!g_h) return;
        if (g_sys) sc::nt_close(g_h); else CloseHandle(g_h);
        g_h = nullptr;
    }

    void detach() { end_session(); g_pid = 0; g_base = 0; g_msize = 0; }

    bool     is_attached() { return g_pid && g_base; }
    DWORD    pid()         { return g_pid; }
    uint64_t base()        { return g_base; }
    size_t   module_size() { return g_msize; }

    bool read(uint64_t addr, void* buf, size_t size) {
        SIZE_T got = 0;
        if (g_sys && g_h) return NT_SUCCESS(sc::nt_read(g_h, addr, buf, size, &got)) && got == size;
        return g_h && ReadProcessMemory(g_h, (LPCVOID)addr, buf, size, &got) && got == size;
    }

    bool write(uint64_t addr, const void* buf, size_t size) {
        SIZE_T put = 0;
        if (g_sys && g_h) return NT_SUCCESS(sc::nt_write(g_h, addr, buf, size, &put)) && put == size;
        return g_h && WriteProcessMemory(g_h, (LPVOID)addr, buf, size, &put) && put == size;
    }

    std::vector<uint8_t> read_bytes(uint64_t addr, size_t size) {
        std::vector<uint8_t> v(size);
        if (!read(addr, v.data(), size)) v.clear();
        return v;
    }
}