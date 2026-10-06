#include "ui/StemWorkspaceComponent.h"
#include "project/ProjectSerializer.h"
#include "stems/JuceStemDecoder.h"
#include "stems/WavExporter.h"
#include <array>
#include <cmath>
#include <fstream>
#include <juce_audio_formats/juce_audio_formats.h>
namespace jeff::daw {
namespace {
constexpr auto panel = 0xff171d27, ink = 0xffeef3fb, muted = 0xff9aa8ba, accent = 0xff5be7c4;
std::filesystem::path nativePath(const juce::File &f) {
  return std::filesystem::path(f.getFullPathName().toWideCharPointer());
}
juce::File juceFile(const std::filesystem::path &p) {
  return juce::File(juce::String(p.wstring().c_str()));
}
class FileStream final : public InputStream {
public:
  explicit FileStream(const std::filesystem::path &p) : in_(p, std::ios::binary) {}
  Result<std::size_t> read(std::span<std::byte> out) override {
    if (!in_ && !in_.eof())
      return Result<std::size_t>::failure(ErrorCode::readFailure, "Cannot read source file.");
    in_.read(reinterpret_cast<char *>(out.data()), std::streamsize(out.size()));
    if (in_.bad() || (!in_.eof() && in_.fail()))
      return Result<std::size_t>::failure(ErrorCode::readFailure, "Source read failed.");
    return Result<std::size_t>::success(std::size_t(in_.gcount()));
  }

private:
  std::ifstream in_;
};
std::vector<InputStreamFactory> inputs(const juce::StringArray &files) {
  std::vector<InputStreamFactory> result;
  for (auto &name : files) {
    auto path = nativePath(juce::File(name));
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    result.push_back({juce::File(name).getFileName().toStdString(),
                      ec ? std::optional<std::uint64_t>{} : size, [path] {
                        return Result<std::unique_ptr<InputStream>>::success(
                            std::make_unique<FileStream>(path));
                      }});
  }
  return result;
}
} // namespace
TrackLaneComponent::TrackLaneComponent(Track t, std::vector<WaveformBin> bins,
                                       std::function<void(SetMixer)> changed)
    : track_(std::move(t)), waveform_(std::move(bins)), changed_(std::move(changed)) {
  name_.setText(juce::String(track_.name), juce::dontSendNotification);
  name_.setColour(juce::Label::textColourId, juce::Colour(ink));
  name_.setFont(juce::FontOptions(15, juce::Font::bold));
  for (auto *c : std::array<juce::Component *, 5>{&name_, &mute_, &solo_, &gain_, &pan_})
    addAndMakeVisible(*c);
  gain_.setRange(0, 1.5, .01);
  gain_.setValue(track_.gain);
  gain_.setSliderStyle(juce::Slider::LinearHorizontal);
  gain_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 22);
  gain_.setName("Track volume");
  pan_.setRange(-1, 1, .01);
  pan_.setValue(track_.pan);
  pan_.setSliderStyle(juce::Slider::LinearHorizontal);
  pan_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 22);
  pan_.setName("Track pan");
  mute_.setClickingTogglesState(true);
  solo_.setClickingTogglesState(true);
  mute_.setToggleState(track_.mute, juce::dontSendNotification);
  solo_.setToggleState(track_.solo, juce::dontSendNotification);
  auto notify = [this] {
    changed_({track_.id, gain_.getValue(), pan_.getValue(), mute_.getToggleState(),
              solo_.getToggleState()});
  };
  gain_.onDragEnd = notify;
  pan_.onDragEnd = notify;
  gain_.onValueChange = [this, notify] {
    if (!gain_.isMouseButtonDown())
      notify();
  };
  pan_.onValueChange = [this, notify] {
    if (!pan_.isMouseButtonDown())
      notify();
  };
  mute_.onClick = notify;
  solo_.onClick = notify;
}
void TrackLaneComponent::paint(juce::Graphics &g) {
  auto r = getLocalBounds().toFloat().reduced(1);
  g.setColour(juce::Colour(panel));
  g.fillRoundedRectangle(r, 8);
  auto w = r.withTrimmedLeft(240).withTrimmedRight(230).reduced(10, 16);
  g.setColour(juce::Colour(0xff222d3b));
  g.fillRoundedRectangle(w, 5);
  g.setColour(juce::Colour(accent));
  if (w.getWidth() > 0 && !waveform_.empty())
    for (int x = 0; x < int(w.getWidth()); ++x) {
      auto index =
          std::min(waveform_.size() - 1, std::size_t(double(x) / w.getWidth() * waveform_.size()));
      auto &b = waveform_[index];
      g.drawVerticalLine(int(w.getX()) + x,
                         w.getCentreY() - std::clamp(b.maximum, -1.f, 1.f) * w.getHeight() * .45f,
                         w.getCentreY() - std::clamp(b.minimum, -1.f, 1.f) * w.getHeight() * .45f);
    }
  g.setColour(juce::Colour(muted));
  g.setFont(11);
  g.drawText("PRESERVE · ORIGINAL MEDIA", 12, 43, 210, 20, juce::Justification::centredLeft);
}
void TrackLaneComponent::resized() {
  auto r = getLocalBounds().reduced(10, 8);
  auto l = r.removeFromLeft(215);
  name_.setBounds(l.removeFromTop(32));
  auto t = l.removeFromTop(28);
  mute_.setBounds(t.removeFromLeft(42));
  solo_.setBounds(t.removeFromLeft(42));
  auto m = r.removeFromRight(220);
  gain_.setBounds(m.removeFromTop(32));
  pan_.setBounds(m.removeFromTop(32));
}
StemWorkspaceComponent::StemWorkspaceComponent(AudioEngine &engine) : engine_(engine) {
  project_.projectId = juce::Uuid().toString().toStdString();
  project_.revisionId = juce::Uuid().toString().toStdString();
  root_ = nativePath(juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                         .getChildFile("Jeff DAW Projects")
                         .getChildFile(juce::String(project_.projectId)));
  heading_.setText("Stem Tracks", juce::dontSendNotification);
  heading_.setFont(juce::FontOptions(25, juce::Font::bold));
  heading_.setColour(juce::Label::textColourId, juce::Colour(ink));
  subheading_.setText("Import stems, build your mix, save and export", juce::dontSendNotification);
  subheading_.setColour(juce::Label::textColourId, juce::Colour(muted));
  tempoLabel_.setText("PROJECT BPM", juce::dontSendNotification);
  tempoLabel_.setColour(juce::Label::textColourId, juce::Colour(muted));
  tempo_.setRange(40, 240, .1);
  tempo_.setValue(120);
  tempo_.setSliderStyle(juce::Slider::LinearHorizontal);
  tempo_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 60, 24);
  tempo_.setName("Project tempo BPM");
  tempo_.setEnabled(false);
  tempo_.setTooltip(
      "Imported projects retain their saved tempo. Timing editing is a later milestone.");
  status_.setText("Load Demo to hear a groove, or add WAV, AIFF, FLAC or MP3 stems.",
                  juce::dontSendNotification);
  status_.setColour(juce::Label::textColourId, juce::Colour(muted));
  for (auto *c : std::array<juce::Component *, 15>{
           &heading_, &subheading_, &record_, &addStems_, &play_, &rewind_, &demo_, &exportMix_,
           &saveProject_, &openProject_, &cancel_, &folder_, &tempoLabel_, &tempo_, &status_})
    addAndMakeVisible(*c);
  record_.onClick = [this] { record(); };
  addStems_.onClick = [this] { chooseFiles(); };
  demo_.onClick = [this] { demo(); };
  saveProject_.onClick = [this] { saveCurrent(); };
  openProject_.onClick = [this] { openProject(); };
  exportMix_.onClick = [this] { exportMix(); };
  play_.onClick = [this] {
    playing_ = !playing_;
    engine_.setTempo(project_.tempoBpm);
    engine_.setPlaying(playing_);
    play_.setButtonText(playing_ ? "Pause" : "Play");
  };
  rewind_.onClick = [this] { engine_.seekBeats(0); };
  cancel_.onClick = [this] {
    if (cancellation_)
      cancellation_->cancel();
  };
  cancel_.setEnabled(false);
  folder_.onClick = [this] { juceFile(root_).revealToUser(); };
  viewport_.setViewedComponent(&trackContent_, false);
  viewport_.setScrollBarsShown(true, false);
  addAndMakeVisible(viewport_);
  play_.setEnabled(false);
  exportMix_.setEnabled(false);
  startTimerHz(20);
}
StemWorkspaceComponent::~StemWorkspaceComponent() {
  stopTimer();
  if (cancellation_)
    cancellation_->cancel();
  if (job_.valid())
    job_.wait();
  if (recording_)
    engine_.recorder().stop();
  engine_.setPlaying(false);
}
void StemWorkspaceComponent::paint(juce::Graphics &g) {
  g.fillAll(juce::Colour(0xff0d1118));
  if (tracks_.isEmpty()) {
    auto r = viewport_.getBounds().toFloat().reduced(24);
    g.setColour(juce::Colour(0xff17202b));
    g.fillRoundedRectangle(r, 14);
    g.setColour(juce::Colour(accent));
    g.drawRoundedRectangle(r, 14, 1.5f);
    g.setFont(juce::FontOptions(20, juce::Font::bold));
    g.drawText("LOAD DEMO OR DROP STEM FILES HERE", r.toNearestInt().reduced(20),
               juce::Justification::centred);
  }
}
void StemWorkspaceComponent::resized() {
  auto r = getLocalBounds().reduced(18);
  auto top = r.removeFromTop(52);
  heading_.setBounds(top.removeFromLeft(190));
  subheading_.setBounds(top);
  auto tools = r.removeFromTop(44);
  for (auto *c : std::array<juce::Component *, 6>{&addStems_, &demo_, &play_, &rewind_,
                                                  &openProject_, &saveProject_})
    c->setBounds(tools.removeFromLeft(juce::jmax(65, juce::jmin(110, getWidth() / 7))).reduced(2));
  auto second = r.removeFromTop(40);
  record_.setBounds(second.removeFromLeft(135).reduced(2));
  exportMix_.setBounds(second.removeFromLeft(112).reduced(2));
  folder_.setBounds(second.removeFromLeft(130).reduced(2));
  cancel_.setBounds(second.removeFromLeft(80).reduced(2));
  tempoLabel_.setBounds(second.removeFromLeft(100));
  tempo_.setBounds(second.removeFromLeft(180));
  status_.setBounds(r.removeFromBottom(36));
  viewport_.setBounds(r.reduced(0, 8));
  relayoutTracks();
}
bool StemWorkspaceComponent::supported(const juce::File &f) {
  return f.hasFileExtension("wav;aiff;aif;flac;mp3");
}
bool StemWorkspaceComponent::isInterestedInFileDrag(const juce::StringArray &f) {
  if (job_.valid())
    return false;
  for (auto &p : f)
    if (supported(juce::File(p)))
      return true;
  return false;
}
void StemWorkspaceComponent::filesDropped(const juce::StringArray &files, int, int) {
  addFiles(files);
}
void StemWorkspaceComponent::runJob(Job work, juce::String label) {
  if (job_.valid())
    return;
  if (recording_) {
    status_.setText("Finish recording before changing the project.", juce::dontSendNotification);
    return;
  }
  engine_.setPlaying(false);
  playing_ = false;
  play_.setButtonText("Play");
  cancellation_ = std::make_shared<CancellationToken>();
  auto token = cancellation_;
  job_ = std::async(std::launch::async, [work = std::move(work), token]() mutable {
    try {
      return work(*token);
    } catch (const std::exception &e) {
      return Result<JobResult>::failure(ErrorCode::writeFailure, e.what());
    }
  });
  status_.setText(label, juce::dontSendNotification);
  for (auto *c :
       std::array<juce::Component *, 9>{&record_, &addStems_, &demo_, &play_, &rewind_,
                                        &saveProject_, &openProject_, &exportMix_, &trackContent_})
    c->setEnabled(false);
  cancel_.setEnabled(true);
}
void StemWorkspaceComponent::timerCallback() {
  if (!job_.valid() || job_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
    return;
  auto result = job_.get();
  cancellation_.reset();
  cancel_.setEnabled(false);
  record_.setEnabled(true);
  if (result) {
    auto state = std::move(result.value());
    if (state.replaceState) {
      project_ = std::move(state.project);
      prepared_ = std::move(state.audio);
      waveforms_ = std::move(state.waveforms);
      root_ = std::move(state.root);
      tempo_.setValue(project_.tempoBpm, juce::dontSendNotification);
      engine_.setTempo(project_.tempoBpm);
      engine_.seekBeats(project_.playheadBeats);
      engine_.publishPlayback(std::move(state.snapshot));
      rebuildTracks();
    }
    status_.setText(juce::String(state.message), juce::dontSendNotification);
  } else
    status_.setText(juce::String(result.error().message), juce::dontSendNotification);
  for (auto *c : std::array<juce::Component *, 6>{&addStems_, &demo_, &rewind_, &saveProject_,
                                                  &openProject_, &trackContent_})
    c->setEnabled(true);
  play_.setEnabled(!project_.tracks.empty());
  exportMix_.setEnabled(!project_.tracks.empty());
  repaint();
}
Result<StemWorkspaceComponent::JobResult>
StemWorkspaceComponent::prepare(Project p, std::filesystem::path root, CancellationToken &token) {
  JuceStemDecoder decoder;
  MediaStore store(root);
  JobResult result;
  result.project = std::move(p);
  result.root = std::move(root);
  for (auto &a : result.project.assets) {
    auto waveform = decoder.inspect(result.root / a.relativePath, token, 1024);
    if (!waveform)
      return Result<JobResult>::failure(waveform.error().code, waveform.error().message);
    result.waveforms[a.id] = std::move(waveform.value().waveform);
  }
  auto audio = prepareWorkspaceAudio(result.project, store, decoder, token);
  if (!audio)
    return Result<JobResult>::failure(audio.error().code, audio.error().message);
  result.audio = std::move(audio.value());
  auto snapshot = workspaceSnapshot(result.project, result.audio);
  if (!snapshot)
    return Result<JobResult>::failure(snapshot.error().code, snapshot.error().message);
  result.snapshot = std::move(snapshot.value());
  return Result<JobResult>::success(std::move(result));
}
void StemWorkspaceComponent::chooseFiles() {
  chooser_ = std::make_unique<juce::FileChooser>("Add stem files", juce::File{},
                                                 "*.wav;*.aiff;*.aif;*.flac;*.mp3");
  chooser_->launchAsync(
      juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
          juce::FileBrowserComponent::canSelectMultipleItems,
      [safe = juce::Component::SafePointer<StemWorkspaceComponent>(this)](const auto &c) {
        if (!safe)
          return;
        juce::StringArray paths;
        for (auto &f : c.getResults())
          paths.add(f.getFullPathName());
        safe->addFiles(paths);
      });
}
void StemWorkspaceComponent::addFiles(const juce::StringArray &files) {
  auto p = project_;
  auto root = root_;
  auto sources = inputs(files);
  if (sources.empty())
    return;
  runJob(
      [p = std::move(p), root, sources = std::move(sources)](auto &token) mutable {
        using R = Result<JobResult>;
        if (p.tracks.size() + sources.size() > 64)
          return R::failure(ErrorCode::storageLimit, "This workspace supports up to 64 tracks.");
        JuceStemDecoder decoder;
        MediaStore store(root);
        StemImporter importer(decoder);
        auto report = importer.import(sources, store, token);
        ProjectHistory history(p);
        auto applied = applyImportedBatch(report, history);
        if (!applied)
          return R::failure(applied.error().code, applied.error().message);
        auto result = prepare(history.current(), root, token);
        if (!result)
          return result;
        auto saved = saveProject(root / "project.json", result.value().project);
        if (!saved)
          return R::failure(saved.error().code, saved.error().message);
        int failed = 0;
        std::string errors;
        for (auto &f : report.files)
          if (f.error) {
            ++failed;
            errors += " " + f.displayName + ": " + f.error->message;
          }
        result.value().message = std::to_string(report.files.size() - failed) +
                                 " stems ready. Project saved locally." + (failed ? errors : "");
        return result;
      },
      "Importing and preparing audio…");
}
void StemWorkspaceComponent::rebuildTracks() {
  tracks_.clear();
  for (auto &t : project_.tracks) {
    auto *lane = tracks_.add(new TrackLaneComponent(
        t, waveforms_[t.assetId], [this](auto mixer) { changeMixer(std::move(mixer)); }));
    trackContent_.addAndMakeVisible(lane);
  }
  relayoutTracks();
}
void StemWorkspaceComponent::relayoutTracks() {
  int width = juce::jmax(760, viewport_.getWidth() - viewport_.getScrollBarThickness()), row = 92;
  trackContent_.setSize(width, juce::jmax(viewport_.getHeight(), tracks_.size() * row));
  for (int i = 0; i < tracks_.size(); ++i)
    tracks_[i]->setBounds(0, i * row, width, row - 6);
}
void StemWorkspaceComponent::changeMixer(SetMixer mixer) {
  ProjectHistory history(project_);
  auto changed = history.apply(mixer);
  if (!changed) {
    status_.setText(juce::String(changed.error().message), juce::dontSendNotification);
    return;
  }
  project_ = history.current();
  auto snap = workspaceSnapshot(project_, prepared_);
  if (snap)
    engine_.publishPlayback(std::move(snap.value()));
  status_.setText("Mix changed — click Save to keep these settings.", juce::dontSendNotification);
}
void StemWorkspaceComponent::saveCurrent() {
  project_.playheadBeats = engine_.playheadBeats();
  auto p = project_;
  auto root = root_;
  runJob(
      [p, root](auto &token) {
        if (token.isCancelled())
          return Result<JobResult>::failure(ErrorCode::cancelled, "Save cancelled.");
        std::filesystem::create_directories(root);
        auto saved = saveProject(root / "project.json", p);
        if (!saved)
          return Result<JobResult>::failure(saved.error().code, saved.error().message);
        JobResult r;
        r.replaceState = false;
        r.message = "Saved locally: " + (root / "project.json").string();
        return Result<JobResult>::success(std::move(r));
      },
      "Saving project…");
}
void StemWorkspaceComponent::openProject() {
  chooser_ = std::make_unique<juce::FileChooser>(
      "Open a saved project.json",
      juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
          .getChildFile("Jeff DAW Projects"),
      "*.json");
  chooser_->launchAsync(
      juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
      [safe = juce::Component::SafePointer<StemWorkspaceComponent>(this)](const auto &c) {
        if (!safe || !c.getResult().existsAsFile())
          return;
        auto path = nativePath(c.getResult());
        safe->runJob(
            [path](auto &token) {
              auto loaded = loadProject(path);
              if (!loaded)
                return Result<JobResult>::failure(loaded.error().code, loaded.error().message);
              auto result = prepare(std::move(loaded.value()), path.parent_path(), token);
              if (result)
                result.value().message = "Project reopened with saved alignment and mix.";
              return result;
            },
            "Opening and preparing project…");
      });
}
void StemWorkspaceComponent::exportMix() {
  auto snap = workspaceSnapshot(project_, prepared_);
  if (!snap) {
    status_.setText(juce::String(snap.error().message), juce::dontSendNotification);
    return;
  }
  chooser_ = std::make_unique<juce::FileChooser>("Export stereo WAV (choose a new filename)",
                                                 juceFile(root_).getChildFile("mix.wav"), "*.wav");
  chooser_->launchAsync(
      juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
      [safe = juce::Component::SafePointer<StemWorkspaceComponent>(this)](const auto &c) {
        if (!safe || c.getResult() == juce::File{})
          return;
        auto path = nativePath(c.getResult().withFileExtension("wav"));
        auto snapshot = workspaceSnapshot(safe->project_, safe->prepared_);
        if (!snapshot)
          return;
        auto shared = std::shared_ptr<PlaybackSnapshot>(std::move(snapshot.value()));
        safe->runJob(
            [shared, path](auto &token) {
              auto result = exportMixWav(*shared, path, token);
              if (!result)
                return Result<JobResult>::failure(result.error().code, result.error().message);
              JobResult r;
              r.replaceState = false;
              r.message = "Exported 48 kHz / 24-bit stereo WAV: " + path.string();
              return Result<JobResult>::success(std::move(r));
            },
            "Rendering WAV mix…");
      });
}
void StemWorkspaceComponent::demo() {
  auto p = project_;
  auto root = root_;
  runJob(
      [p, root](auto &token) mutable {
        using R = Result<JobResult>;
        if (p.tracks.size() > 60)
          return R::failure(ErrorCode::storageLimit, "Demo needs four available tracks.");
        auto dir = root / ".demo";
        std::filesystem::create_directories(dir);
        juce::StringArray paths;
        for (int track = 0; track < 4; ++track) {
          if (token.isCancelled())
            return R::failure(ErrorCode::cancelled, "Demo cancelled.");
          auto path = dir / (std::string(std::array<const char *, 4>{
                                 "Demo drums", "Demo bass", "Demo chords", "Demo lead"}[track]) +
                             ".wav");
          juce::WavAudioFormat format;
          std::unique_ptr<juce::OutputStream> output = juceFile(path).createOutputStream();
          auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate(48000)
                             .withNumChannels(1)
                             .withBitsPerSample(16);
          auto writer = format.createWriterFor(output, options);
          if (!writer)
            return R::failure(ErrorCode::writeFailure, "Cannot create demo audio.");
          juce::AudioBuffer<float> buffer(1, 4096);
          for (int start = 0; start < 384000; start += 4096) {
            int n = juce::jmin(4096, 384000 - start);
            for (int i = 0; i < n; ++i) {
              double time = double(start + i) / 48000, beat = std::fmod(time, .5),
                     phase = std::fmod(time, .25);
              double value = 0;
              if (track == 0)
                value = .6 *
                        std::sin(2 * juce::MathConstants<double>::pi *
                                 (55 * beat + 20 * (1 - std::exp(-beat * 30)))) *
                        std::exp(-beat * 20);
              else if (track == 1) {
                double freq = std::array<double, 4>{65.406, 87.307, 98, 65.406}[int(time / 2) % 4];
                value = .22 * std::sin(2 * juce::MathConstants<double>::pi * freq * time) *
                        std::exp(-beat * 4);
              } else if (track == 2)
                value = .09 * (std::sin(2 * juce::MathConstants<double>::pi * 261.626 * time) +
                               std::sin(2 * juce::MathConstants<double>::pi * 329.628 * time) +
                               std::sin(2 * juce::MathConstants<double>::pi * 391.995 * time));
              else {
                double freq =
                    std::array<double, 8>{523.25, 659.25, 783.99, 659.25,
                                          587.33, 523.25, 392,    523.25}[int(time / .25) % 8];
                value = .12 * std::sin(2 * juce::MathConstants<double>::pi * freq * time) *
                        std::exp(-phase * 12);
              }
              buffer.setSample(0, i, float(value));
            }
            if (!writer->writeFromAudioSampleBuffer(buffer, 0, n))
              return R::failure(ErrorCode::writeFailure, "Demo write failed.");
          }
          writer.reset();
          paths.add(juceFile(path).getFullPathName());
        }
        JuceStemDecoder decoder;
        MediaStore store(root);
        StemImporter importer(decoder);
        auto report = importer.import(inputs(paths), store, token);
        ProjectHistory history(p);
        auto applied = applyImportedBatch(report, history);
        if (!applied)
          return R::failure(applied.error().code, applied.error().message);
        auto result = prepare(history.current(), root, token);
        if (!result)
          return result;
        auto saved = saveProject(root / "project.json", result.value().project);
        if (!saved)
          return R::failure(saved.error().code, saved.error().message);
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
        result.value().message = "Four demo tracks ready — press Play. Saved locally.";
        return result;
      },
      "Creating demo groove…");
}
void StemWorkspaceComponent::record() {
  if (job_.valid())
    return;
  if (recording_) {
    auto result = engine_.recorder().stop();
    recording_ = false;
    record_.setButtonText("Record Output");
    status_.setText(result ? "Recorded WAV: " + juceFile(recordingPath_).getFullPathName()
                           : juce::String(result.error().message),
                    juce::dontSendNotification);
    return;
  }
  std::error_code ec;
  std::filesystem::create_directories(root_ / "recordings", ec);
  if (ec) {
    status_.setText("Cannot create recordings folder.", juce::dontSendNotification);
    return;
  }
  recordingPath_ =
      root_ / "recordings" / ("take-" + juce::Uuid().toString().toStdString() + ".wav");
  auto result = engine_.recorder().start(recordingPath_, engine_.sampleRate());
  if (!result) {
    status_.setText(juce::String(result.error().message), juce::dontSendNotification);
    return;
  }
  recording_ = true;
  record_.setButtonText("Finish Recording");
  status_.setText(
      "Recording the app output. Press Play or perform in Jam + Devices, then Finish Recording.",
      juce::dontSendNotification);
}

} // namespace jeff::daw
