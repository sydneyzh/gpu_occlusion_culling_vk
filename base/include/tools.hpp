#pragma once
#include "path.h"
#include <string>
#include <fstream>
#include <iomanip>

namespace base
{
inline bool file_exists(const std::string& full_path)
{
    std::ifstream f(full_path.c_str());
    return !f.fail();
}

inline bool ends_with(std::string& str, std::string ending)
{
    if (ending.size() > str.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), str.rbegin());
}

inline std::string data_dir()
{
    return DATA_DIR;
}

inline std::string timestamp_str(uint32_t timestamp, std::streamsize precision = 4)
{
    std::stringstream ss;
    ss << std::fixed << std::setprecision(precision) << (static_cast<float>(timestamp) / 1000000.f);
    return ss.str();
}

inline uint32_t get_mip_levels(uint32_t width, uint32_t height)
{
    return floor(log2(std::max(width, height))) + 1;
}
} // namespace base