#include "WavFile.h"

#include <taglib/wavfile.h>
#include <taglib/wavproperties.h>

#include <algorithm>

#include "ChunkScan.h"
#include "TagLibUtil.h"

namespace fs = std::filesystem;

namespace diskdive {

struct WavFile::Impl {
    fs::path path;
    mutable TagLib::RIFF::WAV::File file;  // TagLib getters aren't const-correct

    explicit Impl(const fs::path& p) : path(p), file(p.c_str()) {}
};

WavFile::WavFile(const fs::path& path) : impl_(std::make_unique<Impl>(path)) {
    // TagLib's isValid() accepts any file with a .wav name, so also check the structure ourselves.
    if (!impl_->file.isValid() || !detail::findChunk(path, detail::ChunkLayout::Riff, "data"))
        throw TagError("Cannot open as WAV: " + path.string());
}

WavFile::~WavFile() = default;

std::string WavFile::formatName() const {
    auto& f = impl_->file;
    std::string s = "WAV";
    if (f.hasID3v2Tag()) s += " / ID3v2." + std::to_string(f.ID3v2Tag()->header()->majorVersion());
    if (f.hasInfoTag()) s += " + INFO";
    return s;
}

AudioProperties WavFile::audioProperties() const {
    AudioProperties out;
    if (const auto* p = impl_->file.audioProperties()) {
        out.lengthMs = p->lengthInMilliseconds();
        out.bitrateKbps = p->bitrate();
        out.sampleRate = p->sampleRate();
        out.channels = p->channels();
    }
    return out;
}

const fs::path& WavFile::path() const { return impl_->path; }

AudioRegion WavFile::audioRegion() const {
    // The audio is the payload of the "data" chunk; everything else (fmt, LIST, id3 ...) is metadata.
    const auto chunk = detail::findChunk(impl_->path, detail::ChunkLayout::Riff, "data");
    if (!chunk) throw TagError("No 'data' chunk in WAV: " + impl_->path.string());

    // Streamed/truncated files can declare a size bigger than what is really there: clamp to the file.
    const std::uint64_t available = fs::file_size(impl_->path) - chunk->payloadOffset;
    return {chunk->payloadOffset, std::min(chunk->payloadSize, available)};
}

FieldMap WavFile::fields() const { return detail::readFields(impl_->file); }

void WavFile::setField(const std::string& key, std::vector<std::string> values) {
    if (values.empty()) return removeField(key);
    detail::writeField(impl_->file, key, values);
}

void WavFile::removeField(const std::string& key) { detail::eraseField(impl_->file, key); }

std::vector<FrameInfo> WavFile::rawFrames() const {
    return detail::listFrames(impl_->file.hasID3v2Tag() ? impl_->file.ID3v2Tag() : nullptr);
}

void WavFile::save(const SaveOptions& opt) {
    const fs::path backup = detail::makeBackup(impl_->path);

    // Writes the ID3v2 chunk only and leaves any existing INFO chunk untouched.
    const bool ok = impl_->file.save(TagLib::RIFF::WAV::File::ID3v2, TagLib::File::StripNone,
                                     detail::toTL(opt.id3Version));
    if (!ok) throw TagError("Save failed. Original preserved at: " + backup.string());

    detail::finishBackup(backup, opt.keepBackup);
}

}  // namespace diskdive
