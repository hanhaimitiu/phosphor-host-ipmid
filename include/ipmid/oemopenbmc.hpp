#pragma once

/**
 * @file oemopenbmc.hpp
 * @brief OpenBMC OEM 命令的声明。
 */
/*  @file oemopenbmc.hpp
 *  @brief OpenBMC OEM 命令的声明。 */

#include <ipmid/api.h>

#include <ipmid/oemrouter.hpp>

namespace oem
{
/*
 * OpenBMC OEM Extension IPMI Command codes.
 */
/* OpenBMC OEM Extension IPMI 命令码。 */
enum Cmd
{
    gpioCmd = 1,
    i2cCmd = 2,
    flashCmd = 3,
    fanManualCmd = 4,
    ethStatsCmd = 48,
    blobTransferCmd = 128,
};

} // namespace oem
