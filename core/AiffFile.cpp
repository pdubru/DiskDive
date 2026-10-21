#include "AiffFile.h"

#include <taglib/aifffile.h>
#include <taglib/aiffproperties.h>

#include <algorithm>
#include <fstream>

#include "ChunkScan.h"
#include "TagLibUtil.h"

namespace fs = std::filesystem;

namespace diskdive {

struct AiffFile::Impl {
    fs::path path;
    mutable TagLib::RIFF::AIFF::File file;  // TagLib getters aren't const-correct

    explicit Impl(const fs::path& p) : path(p), file(p.c_str()) {}
};

AiffFile::AiffFile(const fs::path& path) : impl_(std::make_unique<Impl>(path)) {
    // TagLib's isValid() accepts any file with an .aiff name, so also check the structure ourselves.
    if (!impl_->file.isValid() || !detail::findChunk(path, detail::ChunkLayout::Iff, "SSND"))
        throw TagError("Cannot open as AIFF: " + path.string());
}

AiffFile::~AiffFile() = default;

std::string AiffFile::formatName() const {
    auto& f = impl_->file;
    std::string s = "AIFF";
    if (f.hasID3v2Tag()) s += " / ID3v2." + std::to_string(f.tag()->header()->majorVersion());
    return s;
}

AudioProperties AiffFile::audioProperties() const {
    AudioProperties out;
    if (const auto* p = impl_->file.audioProperties()) {
        out.lengthMs = p->lengthInMilliseconds();
        out.bitrateKbps = p->bitrate();
        out.sampleRate = p->sampleRate();
        out.channels = p->channels();
    }
    return out;
}

const fs::path& AiffFile::path() const { return impl_->path; }

AudioRegion AiffFile::audioRegion() const {
    // The audio lives in the "SSND" chunk. Its payload starts with two 32-bit numbers:
    //   offset    : how many bytes of padding come before the first sample
    //   blockSize : alignment hint (unused here)
    // followed by the samples. So the real audio starts at payload + 8 + offset.
    const auto chunk = detail::findChunk(impl_->path, detail::ChunkLayout::Iff, "SSND");
    if (!chunk || chunk->payloadSize < 8) throw TagError("No 'SSND' chunk in AIFF: " + impl_->path.string());

    std::ifstream in(impl_->path, std::ios::binary);
    in.seekg(static_cast<std::streamoff>(chunk->payloadOffset));
    unsigned char hdr[4];
    if (!in.read(reinterpret_cast<char*>(hdr), 4)) throw TagError("Truncated SSND chunk: " + impl_->path.string());
    const std::uint64_t padding =
        (std::uint64_t(hdr[0]) << 24) | (std::uint64_t(hdr[1]) << 16) | (std::uint64_t(hdr[2]) << 8) | hdr[3];

    if (chunk->payloadSize < 8 + padding) throw TagError("Corrupt SSND chunk: " + impl_->path.string());
    const std::uint64_t start = chunk->payloadOffset + 8 + padding;
    const std::uint64_t declared = chunk->payloadSize - 8 - padding;
    const std::uint64_t available = fs::file_size(impl_->path) - start;  // guards against truncated files
    return {start, std::min(declared, available)};
}

FieldMap AiffFile::fields() const { return detail::readFields(impl_->file); }

void AiffFile::setField(const std::string& key, std::vector<std::string> values) {
    if (values.empty()) return removeField(key);
    detail::writeField(impl_->file, key, values);
}

void AiffFile::removeField(const std::string& key) { detail::eraseField(impl_->file, key); }

std::vector<FrameInfo> AiffFile::rawFrames() const {
    return detail::listFrames(impl_->file.hasID3v2Tag() ? impl_->file.tag() : nullptr);
}

void AiffFile::save(const SaveOptions& opt) {
    const fs::path backup = detail::makeBackup(impl_->path);

    if (!impl_->file.save(detail::toTL(opt.id3Version)))
        throw TagError("Save failed. Original preserved at: " + backup.string());

    detail::finishBackup(backup, opt.keepBackup);
}

}  // namespace diskdive
