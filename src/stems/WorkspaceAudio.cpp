#include "stems/WorkspaceAudio.h"
#include <algorithm>
namespace jeff::daw {
Result<WorkspaceAudio> prepareWorkspaceAudio(const Project &p, MediaStore &store,
                                             AudioDecoder &decoder, CancellationToken &token) {
  auto valid = validate(p);
  if (!valid)
    return Result<WorkspaceAudio>::failure(valid.error().code, valid.error().message);
  if (p.tracks.size() > 64)
    return Result<WorkspaceAudio>::failure(ErrorCode::storageLimit,
                                           "Playback supports up to 64 tracks.");
  for (auto &asset : p.assets) {
    auto checked = store.verifyAsset(asset, token);
    if (!checked)
      return Result<WorkspaceAudio>::failure(checked.error().code, checked.error().message);
  }
  WorkspaceAudio audio;
  StretchRenderer renderer(decoder);
  for (auto &group : p.groups) {
    if (std::none_of(p.tracks.begin(), p.tracks.end(),
                     [&](auto &t) { return t.timingGroupId == group.id; }))
      continue;
    auto prepared = renderer.prepareGroup(p, group.id, store, token);
    if (!prepared)
      return Result<WorkspaceAudio>::failure(prepared.error().code, prepared.error().message);
    audio.push_back(std::move(prepared.value()));
  }
  if (token.isCancelled())
    return Result<WorkspaceAudio>::failure(ErrorCode::cancelled, "Preparation cancelled.");
  auto snapshot = workspaceSnapshot(p, audio);
  if (!snapshot)
    return Result<WorkspaceAudio>::failure(snapshot.error().code, snapshot.error().message);
  return Result<WorkspaceAudio>::success(std::move(audio));
}
Result<std::unique_ptr<PlaybackSnapshot>> workspaceSnapshot(const Project &p,
                                                            const WorkspaceAudio &audio) {
  using R = Result<std::unique_ptr<PlaybackSnapshot>>;
  auto valid = validate(p);
  if (!valid)
    return R::failure(valid.error().code, valid.error().message);
  try {
    std::vector<PlaybackTrack> tracks;
    for (auto &t : p.tracks) {
      bool found = false;
      for (auto &a : audio)
        for (auto &m : a.members)
          if (m.trackId == t.id) {
            tracks.push_back(
                {a, m.firstChannel, m.channels, 0, float(t.gain), float(t.pan), t.mute, t.solo});
            found = true;
          }
      if (!found)
        return R::failure(ErrorCode::missingEntity, "Track audio is not prepared: " + t.name);
    }
    return R::success(std::make_unique<PlaybackSnapshot>(p.tempoBpm, std::move(tracks)));
  } catch (const std::exception &e) {
    return R::failure(ErrorCode::invalidProject, e.what());
  }
}
} // namespace jeff::daw
