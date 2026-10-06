#pragma once
#include "audio/AudioEngine.h"
#include "stems/WorkspaceAudio.h"
#include <future>
#include <juce_gui_extra/juce_gui_extra.h>
#include <map>
namespace jeff::daw {
class TrackLaneComponent final : public juce::Component {
public:
  TrackLaneComponent(Track, std::vector<WaveformBin>, std::function<void(SetMixer)>);
  void paint(juce::Graphics &) override;
  void resized() override;

private:
  Track track_;
  std::vector<WaveformBin> waveform_;
  std::function<void(SetMixer)> changed_;
  juce::Label name_;
  juce::TextButton mute_{"M"}, solo_{"S"};
  juce::Slider gain_, pan_;
};
class StemWorkspaceComponent final : public juce::Component,
                                     public juce::FileDragAndDropTarget,
                                     private juce::Timer {
public:
  explicit StemWorkspaceComponent(AudioEngine &);
  ~StemWorkspaceComponent() override;
  void paint(juce::Graphics &) override;
  void resized() override;
  bool isInterestedInFileDrag(const juce::StringArray &) override;
  void filesDropped(const juce::StringArray &, int, int) override;

private:
  struct JobResult {
    Project project;
    WorkspaceAudio audio;
    std::unique_ptr<PlaybackSnapshot> snapshot;
    std::map<Id, std::vector<WaveformBin>> waveforms;
    std::filesystem::path root;
    std::string message;
    bool replaceState{true};
  };
  using Job = std::function<Result<JobResult>(CancellationToken &)>;
  void runJob(Job, juce::String);
  void timerCallback() override;
  void chooseFiles();
  void addFiles(const juce::StringArray &);
  void openProject();
  void saveCurrent();
  void demo();
  void exportMix();
  void record();
  void relayoutTracks();
  void rebuildTracks();
  void changeMixer(SetMixer);
  static bool supported(const juce::File &);
  static Result<JobResult> prepare(Project, std::filesystem::path, CancellationToken &);
  AudioEngine &engine_;
  Project project_;
  WorkspaceAudio prepared_;
  std::map<Id, std::vector<WaveformBin>> waveforms_;
  std::filesystem::path root_;
  std::future<Result<JobResult>> job_;
  std::shared_ptr<CancellationToken> cancellation_;
  bool playing_{};
  bool recording_{};
  std::filesystem::path recordingPath_;
  juce::Label heading_, subheading_, tempoLabel_, status_;
  juce::Slider tempo_;
  juce::TextButton addStems_{"+ Add Stems"}, play_{"Play"}, record_{"Record Output"},
      rewind_{"Rewind"}, demo_{"Load Demo"}, exportMix_{"Export Mix"}, saveProject_{"Save"},
      openProject_{"Open"}, cancel_{"Cancel"}, folder_{"Project Folder"};
  juce::Viewport viewport_;
  juce::Component trackContent_;
  juce::OwnedArray<TrackLaneComponent> tracks_;
  std::unique_ptr<juce::FileChooser> chooser_;
};
} // namespace jeff::daw
