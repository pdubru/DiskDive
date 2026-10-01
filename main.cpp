// Usage:
//   tagcli show <file.mp3>
//   tagcli set  <file.mp3> KEY=VALUE [KEY=VALUE ...] [--v23]   (empty VALUE removes the field)
#include <iostream>
#include <string>

#include "AudioFile.h"

using namespace tagger;

static void show(const AudioFile& f) {
    const auto p = f.audioProperties();
    std::cout << "Format : " << f.formatName() << "\n"
              << "Audio  : " << p.lengthMs / 1000.0 << " s, " << p.bitrateKbps << " kbps, "
              << p.sampleRate << " Hz, " << p.channels << " ch\n\n"
              << "Fields:\n";
    for (const auto& [key, values] : f.fields())
        for (const auto& v : values) std::cout << "  " << key << " = " << v << "\n";

    std::cout << "\nRaw frames:\n";
    for (const auto& fr : f.rawFrames()) std::cout << "  " << fr.id << " : " << fr.description << "\n";
}

int main(int argc, char** argv) try {
    if (argc < 3) {
        std::cerr << "usage: tagcli show <file> | tagcli set <file> KEY=VALUE... [--v23]\n";
        return 2;
    }
    const std::string cmd = argv[1];
    auto file = openAudioFile(argv[2]);

    if (cmd == "show") {
        show(*file);
    } else if (cmd == "set") {
        SaveOptions opt;
        for (int i = 3; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--v23") { opt.id3Version = Id3Version::V23; continue; }
            const auto eq = arg.find('=');
            if (eq == std::string::npos) throw TagError("Expected KEY=VALUE, got: " + arg);
            const std::string key = arg.substr(0, eq), value = arg.substr(eq + 1);
            if (value.empty()) file->removeField(key);
            else file->setField(key, {value});
        }
        file->save(opt);
        std::cout << "Saved.\n\n";
        show(*openAudioFile(argv[2]));  // re-read from disk to confirm
    } else {
        throw TagError("Unknown command: " + cmd);
    }
    return 0;
} catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
}
