#pragma once
// Public interface of the core. Must NOT include any TagLib or UI header.
#include <filesystem>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace diskdive {

struct TagError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Generic, format-agnostic fields. Keys are upper-case (TITLE, ARTIST, ALBUM,
// DATE, GENRE, TRACKNUMBER, COMMENT, ...). Values are UTF-8, and a field can
// hold several values (e.g. multiple artists).
using FieldMap = std::map<std::string, std::vector<std::string>>;

// Read-only view of a raw container-level frame (ID3v2 frame, RIFF chunk, ...)
struct FrameInfo {
    std::string id;           // e.g. "TIT2", "APIC"
    std::string description;  // human readable summary
};

struct AudioProperties {
    int lengthMs = 0;
    int bitrateKbps = 0;
    int sampleRate = 0;
    int channels = 0;
};

enum class Id3Version { V23 = 3, V24 = 4 };

struct SaveOptions {
    Id3Version id3Version = Id3Version::V24;
    bool keepBackup = false;  // keep "<file>.bak" after a successful save
};

class AudioFile {
public:
    virtual ~AudioFile() = default;

    virtual std::string formatName() const = 0;  // "MP3 / ID3v2.4 + ID3v1"
    virtual AudioProperties audioProperties() const = 0;

    virtual FieldMap fields() const = 0;
    virtual void setField(const std::string& key, std::vector<std::string> values) = 0;
    virtual void removeField(const std::string& key) = 0;

    virtual std::vector<FrameInfo> rawFrames() const = 0;

    // Changes are held in memory until save() is called.
    virtual void save(const SaveOptions& options = SaveOptions{}) = 0;
};

// Factory: picks the implementation from the extension (later: sniff content).
// Throws TagError if the file can't be opened or the format is unsupported.
std::unique_ptr<AudioFile> openAudioFile(const std::filesystem::path& path);

}  // namespace diskdive
