#pragma once

#include <fstream>

namespace gfx {
    int init();
    void load_pattern_table(std::ifstream&);
    bool is_running();
    bool should_poll();
    void set(unsigned, char);
    void set_with_delay(unsigned, char);
    char get(unsigned);
    void poll();
    void cycle();
    void print_info();
    void set_frames_per_second(unsigned);
    void close();
    void oam_write(char);
    void set_mirroring(bool);
}
