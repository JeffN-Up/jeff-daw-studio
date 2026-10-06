#pragma once
#include "stems/PlaybackSnapshot.h"
namespace jeff::daw {
Result<void> exportMixWav(const PlaybackSnapshot &, const std::filesystem::path &,
                          CancellationToken &);
}
