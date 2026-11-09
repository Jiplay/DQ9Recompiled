// Local wireless diagnostics: log why ARM7's WM code drops a link on its
// own. Reads guest RAM only. See config/hooks/dq9_arm7.toml.
#include <cstdio>

#include "title_hooks.h"

namespace {

// WMSP work area: *(0x027F8C5C + 0x550). Offsets from 0x027E45DC,
// 0x027E5F70 and 0x027E0898.
constexpr uint32_t kWmGlobals = 0x027F8C5Cu;
constexpr uint32_t kWorkPtr = 0x550u;
constexpr uint32_t kState = 0x000u;          // u16: 8/10 child, 7/9 parent
constexpr uint32_t kLastHeard = 0x738u;      // u64 OS ticks, child slot
constexpr uint32_t kLifetime = 0x7B8u;       // u64 OS ticks
constexpr double kTicksPerMs = 0x82EA / 64.0;

uint64_t ReadU64(uint32_t addr) {
    return bus_read_u32(addr) |
           (static_cast<uint64_t>(bus_read_u32(addr + 4u)) << 32);
}

}  // namespace

// Before 0x027E8AD0: r0 = the internal disconnect request.
NDS_TITLE_HOOK void Dq9WmInternalDisconnect(uint32_t* req) {
    const uint32_t work = bus_read_u32(kWmGlobals + kWorkPtr);
    const uint32_t arg = bus_read_u32(*req + 4u);
    const uint32_t reason = bus_read_u32(*req + 8u);
    const uint64_t lifetime = ReadU64(work + kLifetime);
    std::fprintf(stderr,
                 "[dq9-wm] ARM7 internal disconnect: reason=0x%04X (%s) "
                 "arg=0x%X state=%u lifetime=%.0f ms last_heard=0x%llX\n",
                 static_cast<unsigned>(reason),
                 reason == 0x8001u ? "MP lifetime"
                 : reason == 0x8003u ? "MP indication 0x186" : "other",
                 static_cast<unsigned>(arg),
                 static_cast<unsigned>(bus_read_u16(work + kState)),
                 static_cast<double>(lifetime) / kTicksPerMs,
                 static_cast<unsigned long long>(ReadU64(work + kLastHeard)));
}

// Before 0x027EB384: r0 = status bits the WL MAC raises, lr = caller.
NDS_TITLE_HOOK void Dq9WlSetStatusFlag(uint32_t* bits, uint32_t* lr) {
    std::fprintf(stderr, "[dq9-wm] WL status flag 0x%04X from 0x%08X\n",
                 static_cast<unsigned>(*bits), static_cast<unsigned>(*lr));
}
