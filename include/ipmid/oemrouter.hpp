#pragma once

/**
 * @file oemrouter.hpp
 * @brief OEM 命令路由器：按 IANA 企业号分发到注册的 OEM handler。
 */
/*  @file oemrouter.hpp
 *  @brief OEM 命令路由器：按 IANA 企业号分发到注册的 OEM handler。 */

#include <ipmid/api.h>

#include <ipmid/iana.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace oem
{
constexpr std::size_t groupMagicSize = 3;

using Group = std::array<std::uint8_t, groupMagicSize>;

// Handler signature includes ipmi cmd to support wildcard cmd match.
// Buffers and lengths exclude the OemGroup bytes in the IPMI message.
// dataLen supplies length of reqBuf upon call, and should be set to the
// length of replyBuf upon return - conventional in this code base.
// Handler 签名包含 ipmi cmd，以支持通配 cmd 匹配。
// 缓冲区和长度不包含 IPMI 消息中的 OemGroup 字节。
// dataLen 在调用时提供 reqBuf 的长度，返回时应设置为
// replyBuf 的长度——这是本代码库中的惯例。
using Handler = std::function<ipmi_ret_t(ipmi_cmd_t,          // cmd byte
                                         const std::uint8_t*, // reqBuf
                                         std::uint8_t*,       // replyBuf
                                         std::size_t*)>;      // dataLen

/// Router Interface class.
/// @brief Abstract Router Interface
// / Router Interface 类。
// / @brief 抽象的 Router 接口
class Router
{
  public:
    virtual ~Router() {}

    /// Enable message routing to begin.
    // / 启用消息路由。
    virtual void activate() = 0;

    /// Register a handler for given OEMNumber & cmd.
    /// Use cmdWildcard to catch any unregistered cmd
    /// for the given OEMNumber.
    ///
    /// @param[in] oen - the OEM Number.
    /// @param[in] cmd - the Command.
    /// @param[in] handler - the handler to call given that OEN and
    ///                      command.
    // / 为给定的 OEMNumber 和 cmd 注册一个 handler。
    // / 使用 cmdWildcard 捕获给定 OEMNumber 下
    // / 任何未注册的 cmd。
    // /
    // / @param[in] oen - OEM Number。
    // / @param[in] cmd - 命令。
    // / @param[in] handler - 给定 OEN 和命令时调用的
    // /                      handler。
    virtual void registerHandler(Number oen, ipmi_cmd_t cmd,
                                 Handler handler) = 0;
};

/// Expose mutable Router for configuration & activation.
///
/// @returns pointer to OEM Router to use.
// / 暴露可变的 Router，用于配置和激活。
// /
// / @returns 要使用的 OEM Router 指针。
Router* mutableRouter();

/// Convert a group to an OEN.
///
/// @param[in] oeg - request buffer for IPMI command.
/// @return the OEN.
// / 将 group 转换为 OEN。
// /
// / @param[in] oeg - IPMI 命令的请求缓冲区。
// / @return OEN。
constexpr Number toOemNumber(const std::uint8_t oeg[groupMagicSize])
{
    return (oeg[2] << 16) | (oeg[1] << 8) | oeg[0];
}

/// Given a Group convert to an OEN.
///
/// @param[in] oeg - OEM Group reference.
/// @return the OEN.
// / 将给定的 Group 转换为 OEN。
// /
// / @param[in] oeg - OEM Group 引用。
// / @return OEN。
constexpr Number toOemNumber(const Group& oeg)
{
    return (oeg[2] << 16) | (oeg[1] << 8) | oeg[0];
}

/// Given an OEN, convert to the OEM Group.
///
/// @param[in] oen - the OEM Number.
/// @return the OEM Group.
// / 给定 OEN，转换为 OEM Group。
// /
// / @param[in] oen - OEM Number。
// / @return OEM Group。
constexpr Group toOemGroup(Number oen)
{
    return Group{static_cast<std::uint8_t>(oen),
                 static_cast<std::uint8_t>(oen >> 8),
                 static_cast<std::uint8_t>(oen >> 16)};
}

} // namespace oem
