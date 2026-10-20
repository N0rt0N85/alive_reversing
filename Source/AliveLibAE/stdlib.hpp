#pragma once

#include "../AliveLibCommon/FunctionFwd.hpp"

EXPORT void CC ae_internal_free_521334(void* ptr);
EXPORT void* CC ae_internal_malloc_5212C0(size_t size);
EXPORT void* CC ae_realloc_522335(void* ptr, size_t size);
EXPORT s32 CC ae_fseek_521955(FILE* stream, s32 offset, s32 origin);
EXPORT size_t CC ae_fread_520B5C(void* ptr, size_t size, size_t count, FILE* stream);
EXPORT FILE* CC ae_fopen_520C64(const char_type* filename, const char_type* mode);
EXPORT s32 CC ae_fclose_520CBE(FILE* stream);
EXPORT void* CC ae_malloc_4F4E60(size_t size);
EXPORT void* CC ae_new_malloc_4954D0(size_t size);
EXPORT void* CC ae_malloc_non_zero_4954F0(size_t size);
EXPORT void CC ae_free_4F4EA0(void* ptr);
EXPORT void CC ae_delete_free_495540(void* ptr);
EXPORT void CC ae_non_zero_free_495560(void* ptr);
EXPORT void* CC realloc_4F4E80(void* ptr, size_t size);
EXPORT s32 CC ae_remove_520B27(const char* lpFileName);

#if defined(TETHYS_GAME_AE)
// SATURN (Exoddus, decision D-B4, U1b T4 / U1d T1; AO's bt976 twin,
// AliveLibAO/stdlib.hpp): the allocation policy of AO. ae_new_malloc_4954D0 is
// a NAMED death screen when the HWRAM pool is exhausted (src/game_ae.cxx, the
// bt813 canary), because almost no upstream site tests the result. A cosmetic
// type whose EVERY ae_new<T> site null-tests the result may opt into the soft
// allocator instead (src/stdlib_saturn.cxx: a 2 KB reserve stays for the
// gameplay allocations that follow, a refusal is counted): an exhausted pool
// then means no debris, not a crash. Opt in after the class, at namespace
// scope, with TETHYS_AE_SOFT_OOM_TYPE(T) (AliveLibAE/TethysObjectHooks.hpp).
// The null test makes a soft refusal return nullptr instead of constructing
// through a null block (undefined behaviour, and on SH-2 a write near the BIOS
// vector table). It is compiled for soft types only: ae_new_malloc_4954D0
// never returns null, so every other instantiation stays the upstream code
// byte for byte (507 sites: a test each would cost about 2 KB of HWRAM).
// Distinct names from AO's (Tethys_SoftOom, Tethys_AoNewMallocSoft): this
// header is included next to AO's in AO translation units.
template <typename T>
struct Tethys_SoftOomAE
{
    static constexpr bool value = false;
};
EXPORT void* CC Tethys_AeNewMallocSoft(size_t size);
#define TETHYS_AE_SOFT_OOM 1

template <typename T, typename... Args>
inline T* ae_new(Args&&... args)
{
    void* buffer;
    if constexpr (Tethys_SoftOomAE<T>::value)
    {
        buffer = Tethys_AeNewMallocSoft(sizeof(T));
        if (!buffer)
        {
            return nullptr;
        }
    }
    else
    {
        buffer = ae_new_malloc_4954D0(sizeof(T)); // a named fatal on OOM, never null
    }
    if constexpr (sizeof...(args) == 0)
    {
        return new (buffer) T();
    }
    else
    {
        return new (buffer) T(std::forward<Args>(args)...);
    }
}
#else
template <typename T, typename... Args>
inline T* ae_new(Args&&... args)
{
    void* buffer = ae_new_malloc_4954D0(sizeof(T));
    if constexpr (sizeof...(args) == 0)
    {
        return new (buffer) T();
    }
    else
    {
        return new (buffer) T(std::forward<Args>(args)...);
    }
}
#endif

s32 access_impl(char_type const* fileName, s32 accessMode);
