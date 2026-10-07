#include "stems/StemImporter.h"
#include <cmath>
#include <stdexcept>

namespace jeff::daw {
StemImporter::StemImporter(AudioDecoder& decoder, StemImportLimits limits) : decoder_(decoder), limits_(limits) {
  if (limits.maxFiles == 0 || limits.maxFiles > 4096 || limits.maxWaveformBins == 0 || limits.maxWaveformBins > 4096)
    throw std::invalid_argument("Import limits must be between 1 and 4096.");
}
ImportReport StemImporter::import(const std::vector<InputStreamFactory>& inputs, MediaStore& store,
                                  CancellationToken& token, ProgressCallback progress) {
  ImportReport report;
  if (inputs.size() > limits_.maxFiles) {
    for (const auto& input : inputs) report.files.push_back({input.displayName, {}, Error{ErrorCode::storageLimit, "Too many files in one import batch."}, {}});
    return report;
  }
  report.files.reserve(inputs.size());
  for (const auto& input : inputs) report.files.push_back({input.displayName, {}, {}, {}});
  std::vector<std::optional<MediaStore::StagedMedia>> stages(inputs.size());
  std::vector<DecodedAudio> decoded(inputs.size());
  auto discardStage = [&](std::size_t i) {
    if (!stages[i] || report.files[i].unresolvedStaging) return;
    const auto cleaned = store.discard(*stages[i]);
    if (cleaned) { stages[i].reset(); return; }
    auto& file = report.files[i];
    file.unresolvedStaging = PendingStagedCleanup{stages[i]->id(), ".import/" + stages[i]->id(), cleaned.error()};
    if (file.error) file.error->message += " Staging cleanup failed: " + cleaned.error().message;
    else file.error = cleaned.error();
  };
  for (std::size_t i = 0; i < inputs.size() && !token.isCancelled(); ++i) {
    try {
      auto copied = store.stage(inputs[i], token, [&](std::uint64_t bytes) {
        if (progress) progress({i, inputs.size(), inputs[i].displayName, bytes, inputs[i].expectedBytes});
      }, &report.files[i].unresolvedStaging);
      if (!copied) { report.files[i].error = copied.error(); continue; }
      stages[i].emplace(std::move(copied.value()));
      if (progress) progress({i, inputs.size(), inputs[i].displayName, stages[i]->byteCount(), inputs[i].expectedBytes, ImportPhase::decoding});
      auto audio = decoder_.inspect(stages[i]->path(), token, limits_.maxWaveformBins);
      if (!audio) { report.files[i].error = audio.error(); discardStage(i); continue; }
      const auto& metadata = audio.value();
      bool valid = metadata.channels > 0 && metadata.channels <= 64 && metadata.sourceRate > 0 && metadata.frameCount > 0 &&
                   !metadata.waveform.empty() && metadata.waveform.size() <= limits_.maxWaveformBins;
      for (const auto& bin : metadata.waveform)
        valid = valid && std::isfinite(bin.minimum) && std::isfinite(bin.maximum) && std::isfinite(bin.rms) && bin.minimum <= bin.maximum && bin.rms >= 0;
      if (!valid) { report.files[i].error = Error{ErrorCode::decodeFailure, "Decoder returned invalid audio metadata or waveform."}; discardStage(i); continue; }
      decoded[i] = std::move(audio.value());
    } catch (const std::exception& error) {
      report.files[i].error = Error{ErrorCode::readFailure, "Import failed: " + std::string(error.what())};
      discardStage(i);
    }
  }
  // Publication is delayed until copying and full decoding have finished. A late
  // cancellation rolls back only this batch, never pre-existing project media.
  for (std::size_t i = 0; i < inputs.size() && !token.isCancelled(); ++i) {
    if (!stages[i] || report.files[i].error) continue;
    try {
      if (progress) progress({i, inputs.size(), inputs[i].displayName, stages[i]->byteCount(), inputs[i].expectedBytes, ImportPhase::committing});
      auto committed = store.commit(*stages[i], decoded[i].channels, decoded[i].sourceRate, decoded[i].frameCount, &token);
      if (!committed) { report.files[i].error = committed.error(); discardStage(i); }
      else { report.files[i].asset = std::move(committed.value()); report.files[i].waveform = std::move(decoded[i].waveform); }
    } catch (const std::exception& error) {
      report.files[i].error = Error{ErrorCode::writeFailure, "Import publication failed: " + std::string(error.what())};
      discardStage(i);
    }
  }
  if (token.isCancelled()) {
    report.cancelled = true;
    for (auto& file : report.files) {
      if (file.asset) {
        const auto removed = store.removeCommitted(*file.asset);
        if (!removed) { file.error = removed.error(); continue; } // Retain identity if storage refused cleanup.
        file.asset.reset(); file.waveform.clear();
      }
      if (!file.error) file.error = Error{ErrorCode::cancelled, "Import batch cancelled."};
    }
  }
  // Rejected copies were already released before advancing to the next file.
  // Discard remaining cancelled copies; retain any earlier unresolved cleanup
  // record for an explicit caller retry rather than losing it at batch exit.
  for (std::size_t i = 0; i < stages.size(); ++i) {
    discardStage(i);
  }
  return report;
}

Result<void> applyImportedBatch(const ImportReport& report, ProjectHistory& history, double insertionBeats) {
  if (report.cancelled) return Result<void>::failure(ErrorCode::cancelled, "A cancelled import cannot change the project.");
  if (!std::isfinite(insertionBeats)) return Result<void>::failure(ErrorCode::invalidCommand, "Insertion position must be finite.");
  auto snapshot = history.current();
  TimingGroup group;
  for (const auto& file : report.files) {
    if (!file.asset || file.error) continue;
    if (group.id.empty()) {
      group.id = "import-" + file.asset->id;
      group.timingMap = group.importedTimingMap = preserveTimingMap(snapshot.tempoBpm);
    }
    snapshot.assets.push_back(*file.asset);
    Track track; track.id = "track-" + file.asset->id; track.name = file.displayName;
    track.assetId = file.asset->id; track.timingGroupId = group.id;
    track.placementBeats = track.importedPlacementBeats = insertionBeats;
    track.trimEndSeconds = track.importedTrimEndSeconds = static_cast<double>(file.asset->frameCount) / file.asset->sourceRate;
    snapshot.tracks.push_back(std::move(track));
  }
  if (group.id.empty()) return Result<void>::failure(ErrorCode::invalidCommand, "Import contains no successful audio files.");
  snapshot.groups.push_back(std::move(group));
  return history.apply(std::move(snapshot));
}
} // namespace jeff::daw
