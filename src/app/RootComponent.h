#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "audio/JuceAudioBackend.h"
#include "midi/JuceMidiBackend.h"
#include "midi/MidiDeviceService.h"
#include "ui/DeviceCenterComponent.h"
#include "ui/MidiMonitorModel.h"
#include "ui/OrbaMirrorComponent.h"
#include "ui/TransportComponent.h"
namespace jeff::daw {class RootComponent final:public juce::Component,private juce::Timer{public:RootComponent();void paint(juce::Graphics&)override;void resized()override;private:void timerCallback()override;void event(const PerformanceEvent&);SpscEventQueue<4096>queue;AudioEngine engine;JuceAudioBackend audioBackend;AudioDeviceController audioController;JuceMidiBackend midiBackend;MidiDeviceService midiService;ProfileRegistry profiles;OrbaMirrorModel mirrorModel;MidiMonitorModel monitorModel;DeviceCenterModel deviceModel;juce::ApplicationProperties properties;juce::Label title,status,monitorTitle;TransportComponent transport;OrbaMirrorComponent mirror;juce::TextEditor monitor;DeviceCenterComponent deviceCenter;int refreshCounter{};};}
