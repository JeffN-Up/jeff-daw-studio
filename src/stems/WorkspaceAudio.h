#pragma once
#include "stems/PlaybackSnapshot.h"
namespace jeff::daw {
using WorkspaceAudio = std::vector<PreparedAudio>;
Result<WorkspaceAudio> prepareWorkspaceAudio(const Project &, MediaStore &, AudioDecoder &,
                                             CancellationToken &);
Result<std::unique_ptr<PlaybackSnapshot>> workspaceSnapshot(const Project &,
                                                            const WorkspaceAudio &);
} // namespace jeff::daw
