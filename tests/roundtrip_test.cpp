// Reads every .loo in a directory, writes it back and requires identical bytes.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

#include "loo/document.hpp"

namespace fs = std::filesystem;

static std::vector<std::uint8_t> slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: roundtrip_test <dir-with-.loo-files>\n";
        return 2;
    }
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(argv[1])) {
        if (e.path().extension() == ".loo") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());

    int failures = 0;
    for (const auto& p : files) {
        const auto name = p.filename().string();
        try {
            const auto original = slurp(p);
            const auto doc = loo::readDocument(original);
            const auto again = loo::writeDocument(doc);
            if (again == original) {
                std::cout << "ok    " << name << " (" << doc.objects.size() << " objects, " << original.size() << " bytes)\n";
            } else {
                ++failures;
                std::size_t i = 0;
                while (i < std::min(again.size(), original.size()) && again[i] == original[i]) ++i;
                std::cout << "FAIL  " << name << ": output differs at byte " << i << " (original " << original.size()
                          << " bytes, rewritten " << again.size() << " bytes)\n";
            }
        } catch (const std::exception& ex) {
            ++failures;
            std::cout << "FAIL  " << name << ": " << ex.what() << "\n";
        }
    }
    std::cout << (files.size() - failures) << "/" << files.size() << " files round-trip exactly\n";
    return failures == 0 ? 0 : 1;
}
