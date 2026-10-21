#pragma once
#include <string>

#include "AudioFile.h"

namespace diskdive {

// A track's identity: "<algorithm>:<hex digest>" of its audio bytes only, e.g. "sha256:9f86d0...".
// The prefix lets us change algorithm later without ambiguity. The ID does not depend on the
// file name, folder or tags, so moving, renaming or retagging a file never changes it.
using TrackId = std::string;

// Reads the file's audio region and hashes it. Cost: one sequential read of the audio data.
TrackId computeTrackId(const AudioFile& file);

}  // namespace diskdive
