#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/DeviceCenterModel.h"
namespace jeff::daw {class DeviceCenterComponent final:public juce::Component{public:DeviceCenterComponent();void setModel(const DeviceCenterModel&);void resized()override;private:juce::Label title,audio,diagnostics;juce::TextEditor devices;};}
