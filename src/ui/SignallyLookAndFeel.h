#pragma once
#include <JuceHeader.h>

// App-wide modern dark look: flat rounded buttons/combos, neutral slate palette,
// soft accents. Installed as the default LookAndFeel in Main.cpp.
class SignallyLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SignallyLookAndFeel()
    {
        using juce::Colour;
        setColour(juce::ResizableWindow::backgroundColourId,          Colour(0xff16181d));
        setColour(juce::TextButton::buttonColourId,                   Colour(0xff2a2f3a));
        setColour(juce::TextButton::textColourOffId,                  Colour(0xffe6e8ec));
        setColour(juce::TextButton::textColourOnId,                   Colour(0xffffffff));
        setColour(juce::ComboBox::backgroundColourId,                 Colour(0xff242832));
        setColour(juce::ComboBox::textColourId,                       Colour(0xffe6e8ec));
        setColour(juce::ComboBox::outlineColourId,                    Colour(0xff3a4150));
        setColour(juce::ComboBox::arrowColourId,                      Colour(0xff8a93a3));
        setColour(juce::PopupMenu::backgroundColourId,                Colour(0xff242832));
        setColour(juce::PopupMenu::highlightedBackgroundColourId,     Colour(0xff3a4150));
        setColour(juce::PopupMenu::textColourId,                      Colour(0xffe6e8ec));
        setColour(juce::Label::textColourId,                          Colour(0xffc8ccd4));
        setColour(juce::ScrollBar::thumbColourId,                     Colour(0xff3a4150));
        setColour(juce::ListBox::backgroundColourId,                  Colour(0xff1b1e24));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& b,
                              const juce::Colour& bg, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(1.0f);
        auto c = bg;
        if (down)      c = c.darker(0.18f);
        else if (over) c = c.brighter(0.14f);
        g.setColour(c);
        g.fillRoundedRectangle(r, 7.0f);
        g.setColour(c.brighter(0.20f).withAlpha(0.5f));
        g.drawRoundedRectangle(r, 7.0f, 1.0f);
    }

    juce::Font getTextButtonFont(juce::TextButton&, int h) override
    {
        return juce::Font(juce::jmin(14.5f, h * 0.5f), juce::Font::bold);
    }

    void drawComboBox(juce::Graphics& g, int w, int h, bool /*down*/,
                      int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float>(0.0f, 0.0f, (float) w, (float) h).reduced(1.0f);
        g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(box.findColour(juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle(r, 6.0f, 1.0f);

        juce::Path arrow;
        float cx = (float) w - 14.0f, cy = (float) h * 0.5f;
        arrow.addTriangle(cx - 4.0f, cy - 2.5f, cx + 4.0f, cy - 2.5f, cx, cy + 3.0f);
        g.setColour(box.findColour(juce::ComboBox::arrowColourId));
        g.fillPath(arrow);
    }
};
