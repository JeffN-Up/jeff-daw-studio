#pragma once
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/OrbaLayout.h"
#include "ui/OrbaMirrorModel.h"
namespace jeff::daw {class OrbaMirrorComponent final:public juce::Component{public:using Sink=std::function<void(const PerformanceEvent&)>;explicit OrbaMirrorComponent(Sink);void setSnapshot(const OrbaMirrorSnapshot&);void paint(juce::Graphics&)override;void resized()override;void mouseDown(const juce::MouseEvent&)override;void mouseDrag(const juce::MouseEvent&)override;void mouseUp(const juce::MouseEvent&)override;bool keyPressed(const juce::KeyPress&)override;bool keyStateChanged(bool)override;private:int hit(juce::Point<float>)const;void on(int,float);void off(int);Sink sink;OrbaMirrorSnapshot snap;OrbaLayout layout;std::array<juce::Path,8>paths;std::array<bool,8>keys{};int mousePad{-1};};}
