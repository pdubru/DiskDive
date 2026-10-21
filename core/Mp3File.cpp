#include "Mp3File.h"

#include <taglib/id3v2header.h>
#include <taglib/mpegfile.h>
#include <taglib/mpegheader.h>
#include <taglib/mpegproperties.h>
#include <taglib/xingheader.h>

#include <algorithm>

#include "TagLibUtil.h"

namespace fs = std::filesystem;

namespace diskdive {

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

const fs::path& Mp3File::path() const { return impl_->path; }

AudioRegion Mp3File::audioRegion() const {
    auto& f = impl_->file;

    // Length of the MPEG frame that starts at `offset` (a frame header says how long its frame is).
    auto frameLengthAt = [&f](TagLib::offset_t offset) -> TagLib::offset_t {
        if (offset < 0) return 0;
        const TagLib::MPEG::Header header(&f, offset, false);  // false: don't require a valid next frame
        return header.isValid() ? header.frameLength() : 0;
    };

    TagLib::offset_t start = f.firstFrameOffset();
    const TagLib::offset_t last = f.lastFrameOffset();
    const TagLib::offset_t lastLen = frameLengthAt(last);
    if (start < 0 || lastLen == 0) throw TagError("No MPEG audio frames found: " + impl_->path.string());

    // VBR files start with a "Xing"/"Info" frame: a table of contents, not sound. Tools that
    // rewrite it (e.g. to fix the duration) would change the ID, so we leave it out.
    if (const auto* p = impl_->file.audioProperties(); p && p->xingHeader() && p->xingHeader()->isValid())
        start += frameLengthAt(start);

    const TagLib::offset_t end = std::min<TagLib::offset_t>(last + lastLen, f.length());
    if (end <= start) throw TagError("Empty MPEG audio region: " + impl_->path.string());
    return {static_cast<std::uint64_t>(start), static_cast<std::uint64_t>(end - start)};
}

FieldMap Mp3File::fields() const { return detail::readFields(impl_->file); }

void Mp3File::setField(const std::string& key, std::vector<std::string> values) {
    if (values.empty()) return removeField(key);
    detail::writeField(impl_->file, key, values);
}

void Mp3File::removeField(const std::string& key) { detail::eraseField(impl_->file, key); }

std::vector<FrameInfo> Mp3File::rawFrames() const { return detail::listFrames(impl_->file.ID3v2Tag(false)); }

void Mp3File::save(const SaveOptions& opt) {
    const fs::path backup = detail::makeBackup(impl_->path);

    // Writes ID3v2 only, leaves any existing ID3v1 tag untouched, no auto-duplication.
    const bool ok = impl_->file.save(TagLib::MPEG::File::ID3v2, TagLib::File::StripNone,
                                     detail::toTL(opt.id3Version), TagLib::File::DoNotDuplicate);
    if (!ok) throw TagError("Save failed. Original preserved at: " + backup.string());

    detail::finishBackup(backup, opt.keepBackup);
}

}  // namespace diskdive
