#include "vsqcore.h"
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: test_main <input.vsqx> <track> <output.vmd>\n";
        return 1;
    }
    try {
        vsq::SongData song = vsq::ParseSongFile(argv[1]);
        int track = std::stoi(argv[2]);
        if (track >= (int)song.tracks.size()) { std::cerr << "track out of range\n"; return 1; }
        vsq::TempoMap tempoMap(song.resolution, song.tempos);
        vsq::MorphParams params; // デフォルト値
        auto tracks = vsq::BuildMorphTracks(song.tracks[track], tempoMap, params,
            song.pitchBendCurves[track], song.dynamicsCurves[track]);
        vsq::MorphNames names;
        vsq::WriteVmd(argv[3], tracks, names, "LipSync");

        size_t totalKf = 0; long lastFrame = 0;
        for (auto& kv : tracks) { totalKf += kv.second.size(); for (auto& fr : kv.second) lastFrame = std::max(lastFrame, fr.first); }
        std::cout << "OK notes=" << song.tracks[track].size() << " keyframes=" << totalKf << " lastFrame=" << lastFrame << "\n";
    }
    catch (std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
    return 0;
}