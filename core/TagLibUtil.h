#pragma once
// INTERNAL to core: shared TagLib plumbing for the format classes (Mp3File, WavFile, AiffFile).
// Only .cpp files inside core/ may include this; it must never be included from a public header.
#include <taglib/fileref.h>
#include <taglib/id3v2frame.h>
#include <taglib/id3v2tag.h>
#include <taglib/tpropertymap.h>

#include <algorithm>
#include <cctype>
#include <filesystem>

#include "AudioFile.h"

namespace diskdive::detail {

inline TagLib::String toTL(const std::string& utf8) { return TagLib::String(utf8, TagLib::String::UTF8); }
inline std::string fromTL(const TagLib::String& s) { return s.to8Bit(true); }  // true = UTF-8

inline std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

// The functions below work on any TagLib file type (MPEG, WAV, AIFF all derive from TagLib::File).

inline FieldMap readFields(const TagLib::File& file) {
    FieldMap out;
    const TagLib::PropertyMap props = file.properties();
    for (const auto& kv : props) {
        std::vector<std::string> values;
        for (const auto& v : kv.second) values.push_back(fromTL(v));
        out[fromTL(kv.first)] = std::move(values);
    }
    return out;
}

inline void writeField(TagLib::File& file, const std::string& key, const std::vector<std::string>& values) {
    TagLib::PropertyMap props = file.properties();
    TagLib::StringList list;
    for (const auto& v : values) list.append(toTL(v));
    props.replace(toTL(upper(key)), list);

    const TagLib::PropertyMap rejected = file.setProperties(props);
    if (rejected.contains(toTL(upper(key)))) throw TagError("Field not supported by this format: " + key);
}

inline void eraseField(TagLib::File& file, const std::string& key) {
    TagLib::PropertyMap props = file.properties();
    props.erase(toTL(upper(key)));
    file.setProperties(props);
}

inline std::vector<FrameInfo> listFrames(const TagLib::ID3v2::Tag* tag) {
    std::vector<FrameInfo> out;
    if (!tag) return out;
    for (auto* frame : tag->frameList()) {
        const auto id = frame->frameID();
        out.push_back({std::string(id.data(), id.size()), fromTL(frame->toString())});
    }
    return out;
}

inline TagLib::ID3v2::Version toTL(Id3Version v) {
    return v == Id3Version::V23 ? TagLib::ID3v2::v3 : TagLib::ID3v2::v4;
}

// Tag writes may rewrite the whole file, so keep a copy until we know the save worked.
// Usage: auto backup = makeBackup(path); ...save...; if (!ok) throw ...; finishBackup(backup, keep);
inline std::filesystem::path makeBackup(const std::filesystem::path& path) {
    std::filesystem::path backup = path;
    backup += ".bak";
    try {
        std::filesystem::copy_file(path, backup, std::filesystem::copy_options::overwrite_existing);
    } catch (const std::filesystem::filesystem_error& e) {
        throw TagError(std::string("Could not create backup: ") + e.what());
    }
    return backup;
}

inline void finishBackup(const std::filesystem::path& backup, bool keep) {
    if (keep) return;
    std::error_code ec;
    std::filesystem::remove(backup, ec);
}

}  // namespace diskdive::detail
