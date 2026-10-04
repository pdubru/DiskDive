#include "AudioFile.h"

#include <algorithm>
#include <cctype>

#include "Mp3File.h"

namespace diskdive {

std::unique_ptr<AudioFile> openAudioFile(const std::filesystem::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (ext == ".mp3") return std::make_unique<Mp3File>(path);
    // if (ext == ".wav")  return std::make_unique<WavFile>(path);   // later
    // if (ext == ".aif" || ext == ".aiff") return std::make_unique<AiffFile>(path);

    throw TagError("Unsupported file type: " + path.string());
}

}  // namespace diskdive
