// Writes golden reference outputs of the clean Specification profile, so
// later changes (such as the M4 analog models) can be proven not to alter
// it. Values are hexadecimal doubles, one per line.
//
//     ak4522_golden_dump <output-dir>
//
// Regenerate only on a deliberate, reviewed change to the clean signal path.

#include "golden_stimulus.hpp"

#include <cstdio>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <output-dir>\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    auto write = [&](const char* name, const std::vector<double>& values) {
        const std::string path = dir + "/" + name;
        std::FILE* f = std::fopen(path.c_str(), "w");
        if (f == nullptr) {
            std::perror(path.c_str());
            return false;
        }
        for (const double v : values) {
            std::fprintf(f, "%a\n", v);
        }
        return std::fclose(f) == 0;
    };
    const auto core = ak4522::golden::runCore();
    const auto host = ak4522::golden::runHost();
    return write("golden_core.txt", core) && write("golden_host.txt", host) ? 0 : 1;
}
