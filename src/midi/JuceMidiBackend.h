#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <memory>
#include <unordered_map>
#include "midi/MidiBackend.h"
namespace jeff::daw {class JuceMidiBackend final:public MidiBackend,private juce::MidiInputCallback{public:std::vector<DeviceIdentity>devices()const override;bool open(const std::string&,Callback)override;void close(const std::string&)override;private:void handleIncomingMidiMessage(juce::MidiInput*,const juce::MidiMessage&)override;std::unordered_map<std::string,std::unique_ptr<juce::MidiInput>>inputs;std::unordered_map<std::string,Callback>callbacks;};}
