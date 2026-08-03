#pragma once

#include "../AliveLibCommon/FunctionFwd.hpp"

EXPORT void* alloc_450740(size_t);


EXPORT void ao_delete_free_450770(void*);

EXPORT void* CC ao_new_malloc_447520(s32 size);

EXPORT void CC ao_delete_free_447540(void* pMemory);

#ifdef TETHYS_SATURN
// SATURN (bt976): cosmetic-burst types opt into SOFT OOM -- on Saturn the
// ao_new pool is ~27 KB and HWRAM exhaustion is a NAMED fatal (the bt813
// canary, because most sites never null-check). A type may specialize this
// trait to true ONLY if EVERY ao_new<T> call site null-guards the return
// (Gibs/Blood/BulletShell do, verified); its allocation then degrades to
// "no debris" through Tethys_AoNewMallocSoft (src/stdlib_saturn.cxx, 2 KB
// reserve + skip counter) instead of killing the game mid-combat.
template <typename T>
struct Tethys_SoftOom
{
    static constexpr bool value = false;
};
EXPORT void* CC Tethys_AoNewMallocSoft(s32 size);
#endif

template <typename T, typename... Args>
inline T* ao_new(Args&&... args)
{
#ifdef TETHYS_SATURN
    void* buffer = Tethys_SoftOom<T>::value ? Tethys_AoNewMallocSoft(sizeof(T))
                                            : ao_new_malloc_447520(sizeof(T));
#else
    void* buffer = ao_new_malloc_447520(sizeof(T));
#endif
    if (buffer)
    {
        if constexpr (sizeof...(args) == 0)
        {
            return new (buffer) T();
        }
        else
        {
            return new (buffer) T(std::forward<Args>(args)...);
        }
    }
    return nullptr;
}
