#pragma once

/**
 * @file chassishandler.hpp
 * @brief 机箱 IPMI 命令的声明。
 */
/* @file chassishandler.hpp
 * @brief 机箱 IPMI 命令的声明。 */

#include <stdint.h>

#include <cstddef>

// IPMI Command for a Net Function number as specified by IPMI V2.0 spec.
// IPMI V2.0 规范中规定的对应某 Net Function 编号的 IPMI 命令。
using Cmd = uint8_t;

// Various Chassis operations under a single command.
// 在单条命令下执行多种机箱操作。
constexpr Cmd cmdPowerOff = 0x00;
constexpr Cmd cmdPowerOn = 0x01;
constexpr Cmd cmdPowerCycle = 0x02;
constexpr Cmd cmdHardReset = 0x03;
constexpr Cmd cmdPulseDiagnosticInterrupt = 0x04;
constexpr Cmd cmdSoftOffViaOverTemp = 0x05;

enum class BootOptionParameter : size_t
{
    setInProgress = 0x0,
    bootFlagValidClr = 0x3,
    bootInfo = 0x4,
    bootFlags = 0x5,
    opalNetworkSettings = 0x61
};

enum class BootOptionResponseSize : size_t
{
    setInProgress = 3,
    bootFlags = 5,
    opalNetworkSettings = 50
};

enum class ChassisIDState : uint8_t
{
    off = 0x0,
    temporaryOn = 0x1,
    indefiniteOn = 0x2,
    reserved = 0x3
};
