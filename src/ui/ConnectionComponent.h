#pragma once
#include <JuceHeader.h>

// Stateless helper — connection lines are drawn directly in GraphEditorComponent::paint().
// This header is a placeholder for future interactive connection selection/deletion.
class ConnectionComponent
{
public:
    static juce::Path makeBezier(juce::Point<float> start, juce::Point<float> end);
    static bool hitTest(const juce::Path& bezier, juce::Point<float> point, float tolerance = 6.0f);
};
