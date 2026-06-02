#pragma once
#include <atomic>
#include <cstdint>

// Monitors a ring buffer's fill level and recommends sample-level corrections
// to compensate for clock drift between two independent hardware clocks.
class ClockDriftCorrector
{
public:
    explicit ClockDriftCorrector(int targetFillSamples, int correctionThreshold = 32);

    // Call from the consumer thread each callback. Returns -1 (drop), 0 (pass), +1 (insert).
    int getCorrection(int currentFillSamples);

    // Re-target the desired fill level (e.g. once the block size is known in
    // prepareToPlay). Resets the hysteresis counters.
    void setTargetFill(int targetFillSamples);

    void reset();

private:
    int  targetFill_;
    int  threshold_;
    int  consecutiveOver_  = 0;
    int  consecutiveUnder_ = 0;
    static constexpr int kHysteresisCycles = 8;
};
