#include <array>
#include <iostream>
#include <algorithm>
#include <string>
#include <chrono>
#include <cstdio>

#include "misc.hpp"
#include "gfx.hpp"
#include "machine.hpp"
#include "sdl.hpp"

const auto transparent_pixel = char(0xff);
const auto scanline_length = 341u;
const auto scanline_count = 262u;
const auto prerender_line = scanline_count - 1;

const auto sprite_width = 8u;
const auto sprite_height = 8u;

namespace {
    char read_mem(unsigned);
    char get_sprite_pattern_table_entry(unsigned);
    char get_palette_entry(unsigned);
}

using t_sprite_data = std::array<char, sprite_width * sprite_height>;

class t_sprite {
    unsigned sx;
    unsigned sy;
    unsigned tile_index;
    unsigned attr;

public:
    unsigned get_y() {
        return sy;
    }

    void set_y(unsigned val) {
        sy = val;
    }

    unsigned get_x() {
        return sx;
    }

    void set_x(unsigned val) {
        sx = val;
    }

    unsigned get_tile_index() {
        return tile_index;
    }

    void set_tile_index(unsigned val) {
        tile_index = val;
    }

    void set_attr(unsigned val) {
        attr = val;
    }

    unsigned get_priority() {
        return get_bit(attr, 5);
    }

    bool contains(unsigned x, unsigned y) {
        bool cx = in_range(x, sx, sx + sprite_width);
        bool cy = in_range(y, sy, sy + sprite_height);
        return cx and cy;
    }

    char get_pixel(unsigned x, unsigned y) {
        if (get_bit(attr, 6)) {
            x = 7 - x;
        }
        if (get_bit(attr, 7)) {
            y = 7 - y;
        }
        auto pt_idx = tile_index;
        pt_idx *= 16;
        pt_idx += y;
        x = 7 - x;
        auto b0 = get_bit(get_sprite_pattern_table_entry(pt_idx), x);
        auto b1 = get_bit(get_sprite_pattern_table_entry(pt_idx + 8), x);
        auto b2 = get_bit(attr, 0);
        auto b3 = get_bit(attr, 1);
        if (b0 == 0 and b1 == 0) {
            return transparent_pixel;
        }
        return get_palette_entry(bin_num({b0, b1, b2, b3, 1}));
    }
};

namespace {
    bool started;

    bool in_vblank;
    unsigned long frame_idx;

    unsigned hor_cnt;
    unsigned ver_cnt;

    unsigned set_adr;
    char set_val;
    unsigned long set_delay;
    bool set_delay_active;

    unsigned address;
    char oam_address;
    char control_reg;
    char data_read_buffer;

    class {
        bool latch;
    public:
        bool get_value() { return latch; }
        void flip() { latch ^= 1; }
        void reset() { latch = 1; }
    } address_latch;

    std::array<char, 0x0800> memory;
    std::array<char, 0x2000> pattern_table;
    std::array<char, 0x20> palette;

    std::array<t_sprite, 64> sprite_list;
    std::vector<t_sprite> line_sprite_list;
    std::vector<t_sprite_data> line_sprite_data_list;

    bool sprite_0_hit;

    char scroll_x_start;
    char scroll_y_start;
    char scroll_nametable_start;

    char scroll_x;
    char scroll_y;
    char scroll_nametable;

    bool mirroring;

    bool get_nmi_output_flag() {
        return get_bit(control_reg, 7);
    }

    void gen_nmi() {
        if (in_vblank and get_nmi_output_flag()) {
            machine::set_nmi_flag();
        }
    }

    void write_mem(unsigned adr, char val) {
        //std::cout << "gfx write_mem "; print_hex(adr); std::cout << "\n";
        adr &= 0x3fffu;
        if (adr == 0x3f10 || adr == 0x3f14 || adr == 0x3f18 || adr == 0x3f1c) {
            adr -= 0x10;
        }
        bool bad = false;
        if (adr < 0x2000u) {
            memory[adr] = val;
        } else if (adr < 0x3000u) {
            adr -= 0x2000u;
            if (mirroring == 0) {
                if (in_range(adr, 0x400, 0x800)) {
                    adr -= 0x400;
                } else if (in_range(adr, 0x800, 0xc00)) {
                    adr -= 0x400;
                } else if (in_range(adr, 0xc00, 0x1000)) {
                    adr -= 0x800;
                }
            } else {
                if (in_range(adr, 0x800, 0xc00)) {
                    adr -= 0x800;
                } else if (in_range(adr, 0xc00, 0x1000)) {
                    adr -= 0x800;
                }
            }
            memory[adr] = val;
        } else if (adr < 0x3effu) {
            bad = true;
        } else {
            char idx = get_last_bits(adr, 5);
            palette[idx] = val;
            if (get_last_bits(idx, 2) == 0) {
                flip_bit(idx, 4);
                palette[idx] = val;
            }
        }
        if (bad) {
            std::cout << "gfx bad write_mem "; print_hex(adr); std::cout << "\n";
        }
    }

    char read_mem(unsigned adr) {
        adr &= 0x3fffu;
        if (adr == 0x3f10 || adr == 0x3f14 || adr == 0x3f18 || adr == 0x3f1c) {
            adr -= 0x10;
        }
        char res = 0;
        bool bad = false;
        if (adr < 0x2000u) {
            res = pattern_table[adr];
        } else if (adr < 0x3000u) {
            adr -= 0x2000u;
            if (mirroring == 0) {
                if (in_range(adr, 0x400, 0x800)) {
                    adr -= 0x400;
                } else if (in_range(adr, 0x800, 0xc00)) {
                    adr -= 0x400;
                } else if (in_range(adr, 0xc00, 0x1000)) {
                    adr -= 0x800;
                }
            } else {
                if (in_range(adr, 0x800, 0xc00)) {
                    adr -= 0x800;
                } else if (in_range(adr, 0xc00, 0x1000)) {
                    adr -= 0x800;
                }
            }
            res = memory[adr];
        } else if (adr < 0x3effu) {
            bad = true;
        } else {
            res = palette[get_last_bits(adr, 5)];
        }
        if (bad) {
            std::cout << "gfx bad read_mem "; print_hex(adr); std::cout << "\n";
        }
        return res;
    }

    bool pixel_is_opaque(char pixel) {
        return pixel != transparent_pixel;
    }

    void evaluate_sprites(unsigned y) {
        line_sprite_list.clear();
        line_sprite_data_list.clear();
        for (auto& spr : sprite_list) {
            auto sy = spr.get_y();
            if (in_range(y, sy, sy + sprite_height)) {
                line_sprite_list.push_back(spr);
                t_sprite_data sd;
                for (auto i = 0u; i < sprite_height * sprite_width; i++) {
                    auto x = i % sprite_width;
                    auto y = i / sprite_width;
                    sd[i] = spr.get_pixel(x, y);
                }
                line_sprite_data_list.push_back(sd);
                if (line_sprite_list.size() >= 8) {
                    break;
                }
            }
        }
    }

    char get_palette_entry(unsigned idx) {
        return read_mem(0x3f00u + idx);
    }

    char get_background_pattern_table_entry(unsigned idx) {
        if (get_bit(control_reg, 4)) {
            return read_mem(0x1000u + idx);
        } else {
            return read_mem(idx);
        }
    }

    // unsigned get_scroll_nametable_address() {
    //     unsigned arr[] = {0x2000, 0x2400, 0x2800, 0x2c00};
    //     return arr[scroll_nametable];
    // }

    // char nametable_fetch() {
    //     auto idx = (scroll_y / 8) * 32 + (scroll_x / 8);
    //     return read_mem(get_scroll_nametable_address() + idx);
    // }

    // char attribute_table_fetch() {
    //     auto idx = (scroll_y / 32) * 8 + (scroll_x / 32);
    //     return read_mem(get_scroll_nametable_address() + 0x03c0u + idx);
    // }

    char background_get_pixel(unsigned x, unsigned y, unsigned n) {
        auto bh = x % 8;
        auto bv = y % 8;
        auto abx = x % 32;
        auto aby = y % 32;
        unsigned arr[] = {0x2000, 0x2400, 0x2800, 0x2c00};
        auto adr = arr[n];
        unsigned pt_idx = read_mem(adr + ((y / 8) * 32 + (x / 8)));
        auto at = read_mem(adr + 0x03c0u + (y / 32) * 8 + (x / 32));

        pt_idx *= 16;
        pt_idx += bv;
        bh = 7 - bh;
        auto b0 = get_bit(get_background_pattern_table_entry(pt_idx), bh);
        auto b1 = get_bit(get_background_pattern_table_entry(pt_idx + 8), bh);

        abx /= 16;
        aby /= 16;
        auto bb = (abx + aby * 2) * 2;
        auto b2 = get_bit(at, bb);
        auto b3 = get_bit(at, bb + 1);

        char res;
        if (b0 == 0 and b1 == 0) {
            res = transparent_pixel;
        } else {
            res = get_palette_entry(bin_num({b0, b1, b2, b3}));
        }

        return res;
    }

    char background_get_pixel(unsigned x, unsigned y) {
        unsigned n = 0;
        if (x >= 256) {
            set_bit(n, 0, 1);
        }
        if (y >= 240) {
            set_bit(n, 1, 1);
        }
        x %= 256;
        y %= 240;
        char res = background_get_pixel(x, y, n);
        if (res == transparent_pixel) {
            res = get_palette_entry(0);
        }
        return res;
    }

    char background_fetch_pixel() {
        char res = background_get_pixel(scroll_x, scroll_y, scroll_nametable);
        // auto bh = scroll_x % 8;
        // auto bv = scroll_y % 8;
        // auto abx = scroll_x % 32;
        // auto aby = scroll_y % 32;
        // unsigned pt_idx = nametable_fetch();
        // auto at = attribute_table_fetch();

        // pt_idx *= 16;
        // pt_idx += bv;
        // bh = 7 - bh;
        // auto b0 = get_bit(get_background_pattern_table_entry(pt_idx), bh);
        // auto b1 = get_bit(get_background_pattern_table_entry(pt_idx + 8), bh);

        // abx /= 16;
        // aby /= 16;
        // auto bb = (abx + aby * 2) * 2;
        // auto b2 = get_bit(at, bb);
        // auto b3 = get_bit(at, bb + 1);

        scroll_x++;
        if (scroll_x == scroll_x_start) {
            scroll_y++;
            if (scroll_y == 240) {
                scroll_y = 0;
                flip_bit(scroll_nametable, 1);
            }
            if (scroll_x_start != 0) {
                flip_bit(scroll_nametable, 0);
            }
        } else if (scroll_x == 0) {
            flip_bit(scroll_nametable, 0);
        }

        // char res;
        // if (b0 == 0 and b1 == 0) {
        //     res = transparent_pixel;
        // } else {
        //     res = get_palette_entry(bin_num({b0, b1, b2, b3}));
        // }

        return res;
    }

    void increment_address() {
        if (get_bit(control_reg, 2)) {
            address += 32;
        } else {
            address += 1;
        }
    }

    char get_sprite_pattern_table_entry(unsigned idx) {
        if (get_bit(control_reg, 3)) {
            return read_mem(0x1000u + idx);
        } else {
            return read_mem(idx);
        }
    }
}

void gfx::load_pattern_table(std::ifstream& ifs) {
    ifs.read(&pattern_table[0], pattern_table.size());
}

void gfx::set_with_delay(unsigned adr, char val) {
    set_adr = adr;
    set_val = val;
    set_delay = 3 * machine::get_cycle_counter() - 2;
    set_delay_active = true;
}

void gfx::set(unsigned adr, char val) {
    if (not (adr == 0x2007 and address >= 0x3f00)) {
        if (not (adr == 0x2007 and val == 0x20)) {
            std::cout << "gfx set "; print_hex(adr);
            std::cout << " "; print_hex(val);
            std::cout << "\n";
        }
    }
    // machine::print_info();
    switch (adr) {

    case 0x2000:
        control_reg = val;
        if (get_bit(val, 5)) {
            std::cout << "ppu ctrl bit 5 unimplemented\n";
        }
        if (get_bit(val, 6)) {
            std::cout << "ppu ctrl bit 6 unimplemented\n";
        }
        scroll_nametable = get_last_bits(control_reg, 2);
        break;

    case 0x2003:
        oam_address = val;
        break;

    case 0x2004:
        oam_write(val);
        break;

    case 0x2005:
        switch (address_latch.get_value()) {
        case 1:
            // std::cout << "scroll_x = " << unsigned(val) << "\n";
            scroll_x = val;
            scroll_x_start = val;
            break;
        case 0:
            // std::cout << "scroll_y = " << unsigned(val) << "\n";
            scroll_y_start = val;
            break;
        }
        address_latch.flip();
        break;

    case 0x2006:
        set_octet(address, address_latch.get_value(), val);
        address_latch.flip();
        break;

    case 0x2007:
        write_mem(address, val);
        increment_address();
        break;

    }
}

char gfx::get(unsigned adr) {
    char res = 0;

    switch (adr) {

    case 0x2002:
        set_bit(res, 6, sprite_0_hit);
        set_bit(res, 7, in_vblank);
        in_vblank = false;
        address_latch.reset();
        break;

    case 0x2007:
        if (address < 0x3f00) {
            res = data_read_buffer;
            data_read_buffer = read_mem(address);
        } else {
            res = read_mem(address);
        }
        increment_address();
        break;

    }

    if (adr != 0x2002) {
        std::cout << "gfx get "; print_hex(adr);
        std::cout << " "; print_hex(res);
        std::cout << "\n";
        // machine::print_info();
    }

    return res;
}

int gfx::init() {
    started = false;

    hor_cnt = 0;
    ver_cnt = 0;

    in_vblank = false;
    frame_idx = 0;

    set_delay_active = false;

    oam_address = 0;
    control_reg = 0;
    sprite_0_hit = false;

    scroll_x_start = 0;
    scroll_y_start = 0;
    scroll_nametable_start = 0;

    scroll_x = 0;
    scroll_y = 0;
    scroll_nametable = 0;

    mirroring = 0;

    auto ret = sdl::init();

    return ret;
}

void gfx::close() {
    sdl::close();
}

void gfx::poll() {
    sdl::poll();
}

bool gfx::is_running() {
    return sdl::is_running();
}

bool gfx::is_waiting() {
    return sdl::is_waiting();
}

void gfx::set_frames_per_second(unsigned val) {
    sdl::set_frames_per_second(val);
}

void gfx::print_info() {
    std::cout.flush();
    printf("h %3u  v %3u\n", hor_cnt, ver_cnt);
    std::fflush(stdout);
}

void gfx::oam_write(char val) {
    auto m = oam_address / 4;
    auto& spr = sprite_list[m];
    auto n = oam_address % 4;
    if (n == 0) {
        spr.set_y(val + 1);
    } else if (n == 1) {
        spr.set_tile_index(val);
    } else if (n == 2) {
        spr.set_attr(val);
    } else if (n == 3) {
        spr.set_x(val);
    }
    oam_address++;
}

void gfx::set_mirroring(bool val) {
    mirroring = val;
}

void gfx::cycle() {
    if (not started and frame_idx == 2) {
        started = true;
        frame_idx = 0;
        sdl::begin_drawing();
    }
    if (ver_cnt < 240) {
        if (ver_cnt == 0 and hor_cnt == 0)  {
            // std::cout << "frame begin\n";
            // scroll_nametable = scroll_nametable_start;
            // std::cout << "scroll_nametable = " << unsigned(scroll_nametable) << "\n";
            // scroll_x = scroll_x_start;
            scroll_y = scroll_y_start;
        }
        if (hor_cnt == 0) {
            evaluate_sprites(ver_cnt);
        } else if (hor_cnt < 257) {
            auto x = hor_cnt - 1;
            auto y = ver_cnt;

            auto spr_pixel = transparent_pixel;
            unsigned pixel_priority = 0;
            for (auto i = 0u; i < line_sprite_list.size(); i++) {
                auto& spr = line_sprite_list[i];
                auto& spr_dat = line_sprite_data_list[i];
                auto x0 = spr.get_x();
                auto y0 = spr.get_y();
                if (in_range(x, x0, x0 + sprite_width)) {
                    auto sx = x - x0;
                    auto sy = y - y0;
                    auto idx = sy * sprite_width + sx;
                    spr_pixel = spr_dat[idx];
                    pixel_priority = spr.get_priority();
                    if (pixel_is_opaque(spr_pixel)) {
                        break;
                    }
                }
            }

            auto background_pixel = background_fetch_pixel();

            auto sprite_0_pixel = transparent_pixel;
            auto& s0 = sprite_list[0];
            auto s0x = s0.get_x();
            auto s0y = s0.get_y();
            if (in_range(x, s0x, s0x + sprite_width)) {
                if (in_range(y, s0y, s0y + sprite_height)) {
                    sprite_0_pixel = s0.get_pixel(x - s0x, y - s0y);
                }
            }
            if (pixel_is_opaque(background_pixel)) {
                if (pixel_is_opaque(sprite_0_pixel)) {
                    sprite_0_hit = true;
                }
            }

            auto pixel = background_pixel;
            if (background_pixel == transparent_pixel) {
                pixel = spr_pixel;
            }
            if (pixel_is_opaque(spr_pixel) and pixel_priority == 0) {
                pixel = spr_pixel;
            }
            if (pixel == transparent_pixel) {
                pixel = get_palette_entry(0);
            }

            // if ((x % 32) == 0 || (y % 32) == 0) {
            //     pixel = 0x14;
            // }

            sdl::send_pixel(pixel);
            if (hor_cnt == 256 and ver_cnt == 239) {
                for (auto y = 0u; y < 2 * 240; y++) {
                    for (auto x = 0u; x < 2 * 256; x++) {
                        // sdl::debug_send_pixel(background_get_pixel(x, y));
                    }
                }
                // sdl::debug_render();
                // std::cout << "frame end\n";
                sdl::render();
            }
        }
    }

    if (ver_cnt == prerender_line) {
        if (hor_cnt == 1) {
            sprite_0_hit = false;
        }
    }

    if (ver_cnt == 241) {
        if (hor_cnt == 1) {
            in_vblank = true;
            // std::cout << "in_vblak = true\n";
            gen_nmi();
        }
    }

    if ((frame_idx % 2) == 1 and ver_cnt == 261 and hor_cnt == 339) {
        hor_cnt += 2;
    } else {
        hor_cnt++;
    }
    if (hor_cnt == scanline_length) {
        hor_cnt = 0;
        ver_cnt++;
        if (ver_cnt == scanline_count) {
            ver_cnt = 0;
            frame_idx++;
            in_vblank = false;
            // std::cout << "in_vblank = false\n";
        }
        // std::cout << "ver_cnt = " << ver_cnt << "\n";uo
    }
    if (set_delay_active) {
        if (set_delay == 0) {
            set(set_adr, set_val);
            set_delay_active = false;
        } else {
            set_delay--;
        }
    }
}
