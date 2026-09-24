#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "audio/AudioEngine.h"
namespace jeff::daw {class TransportComponent final:public juce::Component{public:explicit TransportComponent(AudioEngine&);void resized()override;private:AudioEngine&engine;juce::TextButton play{"Play"},stop{"Stop"};juce::ToggleButton metro{"Metronome"};juce::Slider tempo;};}
