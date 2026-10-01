#include "Mp3File.h"

#include <taglib/id3v2frame.h>
#include <taglib/id3v2header.h>
#include <taglib/id3v2tag.h>
#include <taglib/mpegfile.h>
#include <taglib/mpegproperties.h>
#include <taglib/tpropertymap.h>

#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace tagger {

namespace {

TagLib::String toTL(const std::string& utf8) { return TagLib::String(utf8, TagLib::String::UTF8); }
std::string fromTL(const TagLib::String& s) { return s.to8Bit(true); }  // true = UTF-8

std::string upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

}  // namespace

struct Mp3File::Impl {
    fs::path path;
    mutable TagLib::MPEG::File file;  // TagLib getters aren't const-correct

    explicit Impl(const fs::path& p) : path(p), file(p.c_str()) {}  // c_str(): char* or wchar_t* = TagLib::FileName
};

Mp3File::Mp3File(const fs::path& path) : impl_(std::make_unique<Impl>(path)) {
    if (!impl_->file.isValid())
        throw TagError("Cannot open as MP3: " + path.string());
}

Mp3File::~Mp3File() = default;

std::string Mp3File::formatName() const {
    auto& f = impl_->file;
    std::string s = "MP3";
    if (f.hasID3v2Tag())
        s += " / ID3v2." + std::to_string(f.ID3v2Tag(false)->header()->majorVersion());
    if (f.hasID3v1Tag()) s += " + ID3v1";
    return s;
}

AudioProperties Mp3File::audioProperties() const {
    AudioProperties out;
    if (const auto* p = impl_->file.audioProperties()) {
        out.lengthMs = p->lengthInMilliseconds();
        out.bitrateKbps = p->bitrate();
        out.sampleRate = p->sampleRate();
        out.channels = p->channels();
    }
    return out;
}

FieldMap Mp3File::fields() const {
    FieldMap out;
    const TagLib::PropertyMap props = impl_->file.properties();
    for (const auto& kv : props) {
        std::vector<std::string> values;
        for (const auto& v : kv.second) values.push_back(fromTL(v));
        out[fromTL(kv.first)] = std::move(values);
    }
    return out;
}

void Mp3File::setField(const std::string& field, std::vector<std::string> values) {
    if (values.empty()) return removeField(field);

    TagLib::PropertyMap props = impl_->file.properties();
    TagLib::StringList list;
    for (const auto& v : values) list.append(toTL(v));
    props.replace(toTL(upper(field)), list);

    const TagLib::PropertyMap rejected = impl_->file.setProperties(props);
    if (rejected.contains(toTL(upper(field))))
        throw TagError("Field not supported by ID3: " + field);
}

void Mp3File::removeField(const std::string& field) {
    TagLib::PropertyMap props = impl_->file.properties();
    props.erase(toTL(upper(field)));
    impl_->file.setProperties(props);
}

std::vector<FrameInfo> Mp3File::rawFrames() const {
    std::vector<FrameInfo> out;
    if (auto* tag = impl_->file.ID3v2Tag(false)) {
        for (auto* frame : tag->frameList()) {
            const auto id = frame->frameID();
            out.push_back({std::string(id.data(), id.size()), fromTL(frame->toString())});
        }
    }
    return out;
}

void Mp3File::save(const SaveOptions& opt) {
    // Safety net: tag writes may rewrite the whole file, so keep a copy until we know it worked.
    fs::path backup = impl_->path;
    backup += ".bak";
    try {
        fs::copy_file(impl_->path, backup, fs::copy_options::overwrite_existing);
    } catch (const fs::filesystem_error& e) {
        throw TagError(std::string("Could not create backup: ") + e.what());
    }

    const auto version = (opt.id3Version == Id3Version::V23) ? TagLib::ID3v2::v3 : TagLib::ID3v2::v4;

    // Writes ID3v2 only, leaves any existing ID3v1 tag untouched, no auto-duplication.
    const bool ok = impl_->file.save(TagLib::MPEG::File::ID3v2, TagLib::File::StripNone,
                                     version, TagLib::File::DoNotDuplicate);
    if (!ok)
        throw TagError("Save failed. Original preserved at: " + backup.string());

    if (!opt.keepBackup) {
        std::error_code ec;
        fs::remove(backup, ec);
    }
}

}  // namespace tagger
