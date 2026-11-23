
/**
 * @file apphandler.cpp
 * @brief ARM OEM APP 命令的实现。
 */
/*  @file apphandler.cpp
 *  @brief ARM OEM APP 命令的实现。 */
/*
 * SPDX-FileCopyrightText: Copyright OpenBMC Authors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ipmid/api.hpp>

#include <cstdint>

void registerArmNetFnAppFunctions() __attribute__((constructor));

namespace
{
constexpr uint8_t ssifSystemInterfaceSelector = 0x00;
constexpr uint8_t reservedResponseByte = 0x00;
constexpr uint8_t ssifVersion = 0x00;
constexpr uint8_t ssifVersionMask = 0x07;
constexpr uint8_t supportsMultiPartReadsWrites = 0x80;
constexpr uint8_t supportsPec = 0x08;
constexpr uint8_t supportedInterfaceMask = 0x0F;
constexpr uint8_t reservedInterfaceBitsMask = 0xF0;

constexpr uint8_t ssifInputMessageSize = 0xFF;
// IPMI v2.0 section 22.9 reports the SSIF output size as IPMI message bytes.
// Use 0xFF - 3 to leave room for the response NetFn/LUN, command, and
// completion code bytes in a 255-byte SSIF transfer.
// IPMI v2.0 第 22.9 节将 SSIF 输出大小按 IPMI 消息字节数报告。
// 使用 0xFF - 3，为 255 字节的 SSIF 传输中的响应 NetFn/LUN、命令
// 和完成码字节留出空间。
constexpr uint8_t ssifOutputMessageSize = 0xFF - 3;
constexpr uint8_t ssifCapabilities =
    supportsMultiPartReadsWrites | supportsPec |
    (ssifVersion & ssifVersionMask);
} // namespace

ipmi::RspType<uint8_t, uint8_t, uint8_t, uint8_t>
    ipmiAppGetSystemInterfaceCapabilities(uint8_t interfaceSelector)
{
    if ((interfaceSelector & reservedInterfaceBitsMask) != 0)
    {
        // Reserved upper bits make the selector field itself invalid.
        // 保留的高位会使 selector 字段本身无效。
        return ipmi::responseInvalidFieldRequest();
    }

    if ((interfaceSelector & supportedInterfaceMask) !=
        ssifSystemInterfaceSelector)
    {
        // The interface type field is valid, but only SSIF (0x00) is
        // supported by this provider.
        // 接口类型字段有效，但本 provider 仅支持
        // SSIF (0x00)。
        return ipmi::responseParmOutOfRange();
    }

    return ipmi::responseSuccess(reservedResponseByte, ssifCapabilities,
                                 ssifInputMessageSize, ssifOutputMessageSize);
}

void registerArmNetFnAppFunctions()
{
    ipmi::registerHandler(ipmi::prioOemBase, ipmi::netFnApp,
                          ipmi::app::cmdGetSystemIfCapabilities,
                          ipmi::Privilege::User,
                          ipmiAppGetSystemInterfaceCapabilities);
}
