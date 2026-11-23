#pragma once

/**
 * @file ipmiallowlist.hpp
 * @brief 命令白名单（不同固件特性下开放的命令集合）。
 */
/* @file ipmiallowlist.hpp
 * @brief 命令白名单（不同固件特性下开放的命令集合）。 */

#include <utility>
#include <vector>

using netfncmd_pair = std::pair<unsigned char, unsigned char>;

extern const std::vector<netfncmd_pair> allowlist;
