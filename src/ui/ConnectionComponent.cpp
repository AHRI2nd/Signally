#include "ConnectionComponent.h"

juce::Path ConnectionComponent::makeBezier(juce::Point<float> start, juce::Point<float> end)
{
    juce::Path p;
    p.startNewSubPath(start);
    float dx = (end.x - start.x) * 0.5f;
    p.cubicTo(start.x + dx, start.y, end.x - dx, end.y, end.x, end.y);
    return p;
}

bool ConnectionComponent::hitTest(const juce::Path& bezier, juce::Point<float> point, float tolerance)
{
    juce::PathFlatteningIterator it(bezier, juce::AffineTransform(), 2.0f);
    juce::Point<float> prev;
    bool first = true;
    while (it.next())
    {
        juce::Point<float> curr(it.x2, it.y2);
        if (!first)
        {
            // Distance from point to line segment prev→curr
            auto ab = curr - prev;
            auto ap = point - prev;
            float t = juce::jlimit(0.0f, 1.0f, ap.getDotProduct(ab) / ab.getDotProduct(ab));
            auto closest = prev + ab * t;
            if (point.getDistanceFrom(closest) <= tolerance) return true;
        }
        prev = curr;
        first = false;
    }
    return false;
}
