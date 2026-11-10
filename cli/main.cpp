// Usage:
//   diskdive show <file.mp3>
//   diskdive set  <file.mp3> KEY=VALUE [KEY=VALUE ...] [--v23]   (empty VALUE removes the field)
//   diskdive id   <file>
//   diskdive scan <folder> [--db <file>]
//   diskdive list [--db <file>]
#include <iostream>
#include <string>

#include "AudioFile.h"
#include "Library.h"
#include "TrackId.h"

using namespace diskdive;

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

// diskdive scan <folder> [--db <file>]  /  diskdive list [--db <file>]
// The database defaults to the per-user location (see defaultDatabasePath()); --db overrides it (handy for tests).
static int libraryCommand(const std::string& cmd, int argc, char** argv) {
    std::filesystem::path folder, dbFile;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--db" && i + 1 < argc) dbFile = argv[++i];
        else if (cmd == "scan" && folder.empty()) folder = arg;
        else throw TagError("Unexpected argument: " + arg);
    }
    if (cmd == "scan" && folder.empty()) throw TagError("usage: diskdive scan <folder> [--db <file>]");
    if (dbFile.empty()) dbFile = defaultDatabasePath();

    Library lib(dbFile);
    if (cmd == "scan") {
        const auto r = lib.scan(lib.addRoot(folder));
        std::cout << "added " << r.added << ", moved " << r.moved << ", duplicates " << r.duplicate
                  << ", updated " << r.updated << ", unchanged " << r.unchanged << ", missing " << r.missing << "\n";
        for (const auto& f : r.failures) std::cout << "  failed: " << f << "\n";
    } else {
        for (const auto& row : lib.locations())
            std::cout << (row.missing ? "[missing] " : "          ") << row.trackId.substr(0, 19) << "...  "
                      << row.root << "/" << row.relativePath << "\n";
        std::cout << lib.trackCount() << " tracks, database: " << dbFile.string() << "\n";
    }
    return 0;
}

int main(int argc, char** argv) try {
    if (argc < 2 || (argc < 3 && std::string(argv[1]) != "list")) {
        std::cerr << "usage: diskdive show|id <file> | set <file> KEY=VALUE... [--v23] | scan <folder> | list\n";
        return 2;
    }
    const std::string cmd = argv[1];
    if (cmd == "scan" || cmd == "list") return libraryCommand(cmd, argc, argv);
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
        file.reset();  // release the file first: Windows won't let us open it a second time while it's held open
        show(*openAudioFile(argv[2]));  // re-read from disk to confirm
    } else if (cmd == "id") {
        const auto region = file->audioRegion();
        std::cout << "Audio  : bytes " << region.offset << " .. " << region.offset + region.length
                  << " (" << region.length << " bytes)\n"
                  << "TrackId: " << computeTrackId(*file) << "\n";
    } else {
        throw TagError("Unknown command: " + cmd);
    }
    return 0;
} catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
}
