#pragma once
#include <filesystem>
#include <memory>

#include "AudioFile.h"

namespace diskdive {

class WavFile final : public AudioFile {
public:
    explicit WavFile(const std::filesystem::path& path);
    ~WavFile() override;

    std::string formatName() const override;
    AudioProperties audioProperties() const override;
    const std::filesystem::path& path() const override;
    AudioRegion audioRegion() const override;
    FieldMap fields() const override;
    void setField(const std::string& key, std::vector<std::string> values) override;
    void removeField(const std::string& key) override;
    std::vector<FrameInfo> rawFrames() const override;
    void save(const SaveOptions& options) override;

private:
    struct Impl;  // pimpl: keeps TagLib out of this header
    std::unique_ptr<Impl> impl_;
};

}  // namespace diskdive
