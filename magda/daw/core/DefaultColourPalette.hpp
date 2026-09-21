#pragma once

#include <array>
#include <cstdint>

namespace magda {

struct DefaultColourEntry {
    std::uint32_t colour;
    const char* name;
};

inline constexpr std::array<DefaultColourEntry, 8> kDefaultColourPalette{{
    {0xFF5588AA, "Blue"},
    {0xFF55AA88, "Teal"},
    {0xFF88AA55, "Green"},
    {0xFFAAAA55, "Yellow"},
    {0xFFAA8855, "Orange"},
    {0xFFAA5555, "Red"},
    {0xFFAA55AA, "Purple"},
    {0xFF5555AA, "Indigo"},
}};

}  // namespace magda
