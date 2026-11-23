#pragma once

/**
 * @file iana.hpp
 * @brief IANA 企业号（Enterprise ID）常量表。
 */
/*  @file iana.hpp
 *  @brief IANA 企业号（Enterprise ID）常量表。 */

#include <cstdint>

namespace oem
{
using Number = std::uint32_t; // smallest standard size >= 24.

/*
 * This is the OpenBMC IANA OEM Number
 */
/* 这是 OpenBMC 的 IANA OEM 号 */
constexpr Number obmcOemNumber = 49871;

/*
 * This is the Google IANA OEM Number
 */
/* 这是 Google 的 IANA OEM 号 */
constexpr Number googOemNumber = 11129;

} // namespace oem
