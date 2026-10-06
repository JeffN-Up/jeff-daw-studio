#pragma once
#include "audio/AudioBackend.h"
#include "audio/AudioEngine.h"
#include <juce_audio_utils/juce_audio_utils.h>
namespace jeff::daw {
class JuceAudioBackend final : public AudioBackend, private juce::AudioIODeviceCallback {
public:
  explicit JuceAudioBackend(AudioEngine &);
  ~JuceAudioBackend() override;
  AudioConfigurationResult configure(const AudioConfiguration &) override;
  juce::AudioDeviceManager &deviceManager() noexcept { return manager; }

private:
  void audioDeviceIOCallbackWithContext(const float *const *, int, float *const *, int, int,
                                        const juce::AudioIODeviceCallbackContext &) override;
  void audioDeviceAboutToStart(juce::AudioIODevice *) override;
  void audioDeviceStopped() override {}
  AudioEngine &engine;
  juce::AudioDeviceManager manager;
};
} // namespace jeff::daw
