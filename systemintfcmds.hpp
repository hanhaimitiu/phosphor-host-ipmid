#pragma once

/**
 * @file systemintfcmds.hpp
 * @brief System Interface 命令的声明。
 */
/*  @file systemintfcmds.hpp
 *  @brief System Interface 命令的声明。 */

#include <stdint.h>

// These are per skiboot ipmi-sel code
// 这些是遵循 skiboot 的 ipmi-sel 代码

// Minor command for soft shutdown
// 软关机（soft shutdown）的次级命令（Minor command）
#define SOFT_OFF 0x00
// Major command for Any kind of power ops
// 任意电源操作（power ops）的主命令（Major command）
#define CMD_POWER 0x04
// Major command for the heartbeat operation (verify host is alive)
// 心跳操作的主命令（用于确认主机是否存活）
#define CMD_HEARTBEAT 0xFF
