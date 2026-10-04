#pragma once
#include <filesystem>
#include <memory>

#include "AudioFile.h"

namespace diskdive {

class Mp3File final : public AudioFile {
public:
    explicit Mp3File(const std::filesystem::path& path);
    ~Mp3File() override;

    std::string formatName() const override;
    AudioProperties audioProperties() const override;
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
