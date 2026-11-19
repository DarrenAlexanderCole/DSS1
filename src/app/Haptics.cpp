#include "Haptics.h"

namespace dss1::ui::haptics
{

#if ! JUCE_MAC
void tap() {}
#endif

namespace
{
    struct Detents final : juce::Slider::Listener
    {
        std::map<juce::Slider*, int> lastNotch;

        static int notchOf (juce::Slider& s)
        {
            const auto range = s.getRange();
            if (range.getLength() <= 0)
                return 0;
            const auto interval = s.getInterval();
            const auto steps = interval > 0 ? range.getLength() / interval : 0.0;
            if (steps > 0 && steps <= 64)
                return juce::roundToInt ((s.getValue() - range.getStart()) / interval);
            return juce::roundToInt ((s.getValue() - range.getStart()) / range.getLength() * 20.0);
        }

        void sliderValueChanged (juce::Slider* s) override
        {
            const auto notch = notchOf (*s);
            auto [it, inserted] = lastNotch.try_emplace (s, notch);
            if (! inserted && it->second != notch && s->isMouseButtonDown())
                tap();
            it->second = notch;
        }
    };

    Detents& detents()
    {
        static Detents d;
        return d;
    }
}

void addDetents (juce::Component& root)
{
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c) {
        if (auto* s = dynamic_cast<juce::Slider*> (&c))
        {
            s->removeListener (&detents());
            s->addListener (&detents());
            detents().lastNotch[s] = Detents::notchOf (*s);
        }
        for (auto* child : c.getChildren())
            walk (*child);
    };
    walk (root);
}

} // namespace dss1::ui::haptics
