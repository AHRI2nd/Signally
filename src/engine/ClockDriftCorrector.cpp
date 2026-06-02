#include "ClockDriftCorrector.h"

ClockDriftCorrector::ClockDriftCorrector(int targetFillSamples, int correctionThreshold)
    : targetFill_(targetFillSamples), threshold_(correctionThreshold)
{}

int ClockDriftCorrector::getCorrection(int currentFill)
{
    if (currentFill > targetFill_ + threshold_)
    {
        if (++consecutiveOver_ >= kHysteresisCycles)
        {
            consecutiveOver_  = 0;
            consecutiveUnder_ = 0;
            return -1; // drop a sample
        }
    }
    else
    {
        consecutiveOver_ = 0;
    }

    if (currentFill < targetFill_ - threshold_)
    {
        if (++consecutiveUnder_ >= kHysteresisCycles)
        {
            consecutiveOver_  = 0;
            consecutiveUnder_ = 0;
            return +1; // insert a sample
        }
    }
    else
    {
        consecutiveUnder_ = 0;
    }

    return 0;
}

void ClockDriftCorrector::reset()
{
    consecutiveOver_  = 0;
    consecutiveUnder_ = 0;
}
