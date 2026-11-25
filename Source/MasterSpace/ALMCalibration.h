#pragma once
// Offline calibration accumulator. Inputs must be time-aligned and identically K-weighted.
// Do not use program audio or call from processBlock. No gate: this is a Gram matrix,
// not integrated BS.1770 loudness. Each channel contributes independently.
#include "AutoLevelMatch.h"
#include <cstdint>

namespace mas::alm
{
class CalibrationAccumulator
{
public:
    void add(double input, double direct, double early, double late) noexcept
    {
        if (!std::isfinite(input) || !std::isfinite(direct)
            || !std::isfinite(early) || !std::isfinite(late))
        { invalid = true; return; }
        energy += input*input;
        q[0] += direct*direct; q[1] += early*early; q[2] += late*late;
        q[3] += direct*early; q[4] += direct*late; q[5] += early*late;
        ++count;
    }
    Calibration finish() const noexcept
    {
        if (invalid || count == 0 || !std::isfinite(energy) || energy <= 0) return {};
        Calibration c;
        c.inputEnergy = energy / static_cast<double>(count);
        for (unsigned i=0; i<q.size(); ++i)
        {
            c.q[i] = q[i] / static_cast<double>(count);
            if (!std::isfinite(c.q[i])) return {};
        }
        return c;
    }
private:
    double energy = 0;
    std::array<double,6> q {};
    std::uint64_t count = 0;
    bool invalid = false;
};
} // namespace mas::alm
