// The modes' names, both ways.
#include "settings.hpp"

#include <cassert>
#include <cstdio>

int main() {
    for (sb::Mode m : {sb::Mode::Off, sb::Mode::On, sb::Mode::Verify, sb::Mode::Startup}) {
        assert(sb::ParseMode(sb::ModeName(m)) == m);
    }
    assert(sb::ParseMode("VERIFY") == sb::Mode::Verify);
    // config.toml of 0.7.0 and earlier also took 1/0/true/false; Bellows 2.0 reads no config.toml.
    assert(!sb::ParseMode("1") && !sb::ParseMode("true") && !sb::ParseMode(""));
    std::printf("settings ok\n");
    return 0;
}
