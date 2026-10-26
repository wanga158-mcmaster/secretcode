// Pinned-SDK constructor-only regression. The offline host supplies immutable
// SDK startup metadata and bitboard-zero; no gameplay/action import is allowed.
#include "../archipelago/src/runtime.h"
#define PLAN4_MACRO_MODE 1
#define PLAN4_ONLINE_EVIDENCE 1
#define PLAN4_SPATIAL_EVIDENCE 1
#define PLAN4_STATIC_SPATIAL 1
#define PLAN4_ACTOR_EVIDENCE 1
#define PLAN4_PUBLIC_COMPOSITION 1
#define PLAN4_RUNTIME_TYPE plan4::runtime::Archipelago
#define PLAN4_MODEL_ENABLED 1
#define PLAN4_REFERENCE_VERSION "constructor-offline"
#define PLAN4_SETUP_TYPE ConstructorOnly
#include "../common/legacy.h"
#include <cstdio>
#include <cstdlib>

namespace probe {
bool enabled=false;
std::size_t calls=0, bytes=0, planning_allocations=0;
}
void* operator new(std::size_t size) {
    if (void* result=std::malloc(size?size:1)) {
        if (probe::enabled) {
            ++probe::calls; probe::bytes+=size;
            if (size>=sizeof(plan4::Information)) ++probe::planning_allocations;
        }
        return result;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer,std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer,std::size_t) noexcept { std::free(pointer); }

int main() {
    using Bot=plan4_legacy::Bot;
    alignas(Bot) static unsigned char storage[sizeof(Bot)];
    int checks=0;
    for (bool enabled : {false,true}) {
        probe::calls=probe::bytes=probe::planning_allocations=0; probe::enabled=true;
        auto* bot=::new(static_cast<void*>(storage)) Bot(enabled);
        probe::enabled=false;
        if (probe::planning_allocations || probe::calls || probe::bytes) {
            std::fprintf(stderr,"FAIL: unused planning/model storage allocated during Bot construction: %zu calls, %zu bytes\n",probe::calls,probe::bytes);
            return 1;
        }
        ++checks;
        // Fixture suspension may precede the first planning observation. It
        // must not materialize Information or Evaluation merely to cancel.
        probe::enabled=true; bot->fixture_suspend(); probe::enabled=false;
        if (probe::planning_allocations || probe::calls || probe::bytes) {
            std::fprintf(stderr,"FAIL: cold fixture suspension allocated planning storage\n"); return 1;
        }
        ++checks; bot->~Bot();
    }
    std::printf("plan4 common constructor: %d checks passed; Bot_bytes=%zu; deferred_planning_bytes=%zu\n",
        checks,sizeof(Bot),sizeof(plan4::Information)+sizeof(plan4::Evaluation));
}
