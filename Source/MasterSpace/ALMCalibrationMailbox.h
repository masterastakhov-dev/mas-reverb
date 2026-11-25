#pragma once
#include "AutoLevelMatch.h"
#include <atomic>
#include <cstdint>

namespace mas::alm
{
// One coherent atomic word: generation (upper 32 bits), valid/limited flags
// and gain in milli-dB (lower 32). No allocation, locks or spinning on read.
// begin() has ONE owner (message/coordinator thread); publish() may run on a
// worker. A result from a superseded job is never accepted after begin().
class CalibrationMailbox
{
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
                  "LEVEL MATCH requires lock-free 64-bit atomics");
public:
    using Ticket = std::uint32_t;
    struct Snapshot { Ticket ticket; Estimate estimate; };

    Ticket begin() noexcept
    {
        const auto ticket = static_cast<Ticket>(word.load(std::memory_order_relaxed)>>32)+Ticket{1};
        word.store(pack(ticket,0),std::memory_order_release);
        return ticket;
    }

    bool publish(Ticket ticket, Estimate e) noexcept
    {
        if(!e.valid || !std::isfinite(e.db)) return false;
        const auto db=std::clamp(e.db,-6.0f,12.0f);
        const auto value=static_cast<std::uint32_t>(std::lround(db*1000.0f)+6000);
        const auto flags=validBit | ((e.limited || db!=e.db)?limitedBit:0u);
        auto expected=pack(ticket,0);
        return word.compare_exchange_strong(expected,pack(ticket,flags|value),
            std::memory_order_release,std::memory_order_relaxed);
    }

    Snapshot read() const noexcept
    {
        const auto value=word.load(std::memory_order_acquire);
        const auto data=static_cast<std::uint32_t>(value);
        Estimate e {};
        if(data&validBit)
        {
            e.valid=true; e.limited=(data&limitedBit)!=0;
            e.db=static_cast<float>(static_cast<int>(data&0xffffu)-6000)*0.001f;
        }
        return {static_cast<Ticket>(value>>32),e};
    }
private:
    static constexpr std::uint32_t validBit=1u<<16,limitedBit=1u<<17;
    static constexpr std::uint64_t pack(Ticket t,std::uint32_t d) noexcept
    { return (static_cast<std::uint64_t>(t)<<32)|d; }
    std::atomic<std::uint64_t> word {0};
};
} // namespace mas::alm
