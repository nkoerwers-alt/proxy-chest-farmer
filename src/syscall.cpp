#include "syscall.hpp"
#include <cstring>
#include <algorithm>
#include <vector>

namespace sc {

    enum : uint32_t { S_OPEN, S_CLOSE, S_READ, S_WRITE, S_COUNT };

    struct CID { HANDLE unique; HANDLE thread; };

    static void*    g_stub[S_COUNT]{};
    static uint32_t g_ssn[S_COUNT]{};
    static bool     g_ok = false;

    // ---- module base via PEB, bypassing GetModuleHandle ----
    static uint64_t ntdll_base() {
        auto peb = (uint8_t*)__readgsqword(0x60);
        auto ldr = *(uint8_t**)(peb + 0x18);
        auto head = *(uint8_t**)(ldr + 0x10);
        for (auto e = head; e && e != ldr + 0x10; e = *(uint8_t**)e) {
            auto base = *(uint64_t*)(e + 0x30);
            auto len  = *(uint16_t*)(e + 0x58);
            auto nm   = *(wchar_t**)(e + 0x60);
            if (!base || !nm || len < 16) continue;
            wchar_t b[32]{};
            memcpy(b, nm, len > 60 ? 60 : len);
            if (_wcsicmp(b, L"ntdll.dll") == 0) return base;
        }
        return 0;
    }

    static bool read_stub_ssn(uint8_t* f, uint32_t* out) {
        if (f[0] == 0x4C && f[1] == 0x8B && f[2] == 0xD1 && f[3] == 0xB8) {
            *out = *(uint32_t*)(f + 4);
            return true;
        }
        return false; // hooked or unexpected prologue
    }

    // sorting fallback: SSNs are assigned in ascending address order inside ntdll
    static uint32_t ssn_by_sort(uint64_t ntdll, const char* target) {
        auto dos = (IMAGE_DOS_HEADER*)ntdll;
        auto nt  = (IMAGE_NT_HEADERS64*)(ntdll + dos->e_lfanew);
        auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (!dir.VirtualAddress) return 0;
        auto exp   = (IMAGE_EXPORT_DIRECTORY*)(ntdll + dir.VirtualAddress);
        auto names = (uint32_t*)(ntdll + exp->AddressOfNames);
        auto ords  = (uint16_t*)(ntdll + exp->AddressOfNameOrdinals);
        auto funcs = (uint32_t*)(ntdll + exp->AddressOfFunctions);

        std::vector<std::pair<uint64_t, uint32_t>> list;
        uint32_t idx = 0;
        for (uint32_t i = 0; i < exp->NumberOfNames; ++i) {
            const char* n = (const char*)(ntdll + names[i]);
            if (n[0] != 'N' || n[1] != 't') continue;
            if (strcmp(n, target) == 0) idx = i;
            list.push_back({ ntdll + funcs[ords[i]], i });
        }
        std::sort(list.begin(), list.end());
        for (uint32_t k = 0; k < list.size(); ++k)
            if (list[k].second == idx) return k;
        return 0;
    }

    static uint32_t resolve(uint64_t ntdll, const char* fn) {
        auto dos = (IMAGE_DOS_HEADER*)ntdll;
        auto nt  = (IMAGE_NT_HEADERS64*)(ntdll + dos->e_lfanew);
        auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        auto exp   = (IMAGE_EXPORT_DIRECTORY*)(ntdll + dir.VirtualAddress);
        auto names = (uint32_t*)(ntdll + exp->AddressOfNames);
        auto ords  = (uint16_t*)(ntdll + exp->AddressOfNameOrdinals);
        auto funcs = (uint32_t*)(ntdll + exp->AddressOfFunctions);

        for (uint32_t i = 0; i < exp->NumberOfNames; ++i) {
            if (strcmp((const char*)(ntdll + names[i]), fn) != 0) continue;
            uint32_t ssn = 0;
            if (read_stub_ssn((uint8_t*)(ntdll + funcs[ords[i]]), &ssn)) return ssn;
            return ssn_by_sort(ntdll, fn);
        }
        return 0;
    }

    // find a plain `ret` inside ntdll's .text to use as a fake return address
    static uint64_t ntdll_gadget(uint64_t ntdll) {
        auto dos = (IMAGE_DOS_HEADER*)ntdll;
        auto nt  = (IMAGE_NT_HEADERS64*)(ntdll + dos->e_lfanew);
        auto s   = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s) {
            if (memcmp(s->Name, ".text", 5) != 0) continue;
            auto p = (uint8_t*)(ntdll + s->VirtualAddress);
            for (DWORD k = 0x200; k < s->Misc.VirtualSize; ++k)
                if (p[k] == 0xC3) return (uint64_t)(p + k);
        }
        return 0;
    }

    static void* build(uint32_t ssn, uint64_t gadget) {
        auto m = (uint8_t*)VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!m) return nullptr;
        size_t i = 0;
        if (gadget) {                                   // mov rax, gadget ; push rax
            m[i++] = 0x48; m[i++] = 0xB8;
            *(uint64_t*)(m + i) = gadget; i += 8;
            m[i++] = 0x50;
        }
        m[i++] = 0x4C; m[i++] = 0x8B; m[i++] = 0xD1;    // mov r10, rcx
        m[i++] = 0xB8; *(uint32_t*)(m + i) = ssn; i += 4;
        m[i++] = 0x0F; m[i++] = 0x05;                   // syscall
        m[i++] = 0xC3;                                  // ret
        return m;
    }

    bool init() {
        uint64_t nt = ntdll_base();
        if (!nt) return false;

        const char* fns[S_COUNT] = {
            "NtOpenProcess", "NtClose", "NtReadVirtualMemory", "NtWriteVirtualMemory"
        };
        for (uint32_t i = 0; i < S_COUNT; ++i) {
            g_ssn[i] = resolve(nt, fns[i]);
            if (!g_ssn[i]) return false;
        }
        uint64_t gadget = ntdll_gadget(nt);
        for (uint32_t i = 0; i < S_COUNT; ++i) {
            g_stub[i] = build(g_ssn[i], gadget);
            if (!g_stub[i]) return false;
        }
        g_ok = true;
        return true;
    }

    bool ready() { return g_ok; }

    NTSTATUS nt_open_process(PHANDLE out, ACCESS_MASK access, uintptr_t pid) {
        if (!g_ok) return (NTSTATUS)0xC0000001;
        OBJECT_ATTRIBUTES oa{ sizeof(oa) };
        CID cid{ (HANDLE)pid, nullptr };
        auto f = (NTSTATUS(*)(PHANDLE, ACCESS_MASK, OBJECT_ATTRIBUTES*, CID*))g_stub[S_OPEN];
        return f(out, access, &oa, &cid);
    }

    NTSTATUS nt_close(HANDLE h) {
        if (!g_ok) return (NTSTATUS)0xC0000001;
        return ((NTSTATUS(*)(HANDLE))g_stub[S_CLOSE])(h);
    }

    NTSTATUS nt_read(HANDLE h, uint64_t a, void* b, size_t s, size_t* got) {
        if (!g_ok) return (NTSTATUS)0xC0000001;
        return ((NTSTATUS(*)(HANDLE, uint64_t, void*, size_t, size_t*))g_stub[S_READ])(h, a, b, s, got);
    }

    NTSTATUS nt_write(HANDLE h, uint64_t a, const void* b, size_t s, size_t* put) {
        if (!g_ok) return (NTSTATUS)0xC0000001;
        return ((NTSTATUS(*)(HANDLE, uint64_t, const void*, size_t, size_t*))g_stub[S_WRITE])(h, a, b, s, put);
    }
}