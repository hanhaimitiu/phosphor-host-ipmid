#pragma once

/**
 * @file transportconstants.hpp
 * @brief Transport/LAN 命令的常量定义。
 */
/*  @file transportconstants.hpp
 *  @brief Transport/LAN 命令的常量定义。 */

#include <ipmid/api-types.hpp>
#include <stdplus/zstring_view.hpp>
#include <xyz/openbmc_project/Network/IP/common.hpp>

#include <cstdint>

using NetworkIP = sdbusplus::common::xyz::openbmc_project::network::IP;

namespace ipmi
{
namespace transport
{

using stdplus::operator""_zsv;

// D-Bus Network Daemon definitions
// D-Bus 网络守护进程（Network Daemon）的定义
constexpr auto PATH_ROOT = "/xyz/openbmc_project/network"_zsv;
constexpr auto INTF_ETHERNET = "xyz.openbmc_project.Network.EthernetInterface";
constexpr auto INTF_IP_CREATE = "xyz.openbmc_project.Network.IP.Create";
constexpr auto INTF_MAC = "xyz.openbmc_project.Network.MACAddress";
constexpr auto INTF_NEIGHBOR = "xyz.openbmc_project.Network.Neighbor";
constexpr auto INTF_NEIGHBOR_CREATE_STATIC =
    "xyz.openbmc_project.Network.Neighbor.CreateStatic";
constexpr auto INTF_VLAN = "xyz.openbmc_project.Network.VLAN";
constexpr auto INTF_VLAN_CREATE = "xyz.openbmc_project.Network.VLAN.Create";

/** @brief IPMI LAN Parameters */
/* @brief IPMI LAN 参数 */
enum class LanParam : uint8_t
{
    SetStatus = 0,
    AuthSupport = 1,
    AuthEnables = 2,
    IP = 3,
    IPSrc = 4,
    MAC = 5,
    SubnetMask = 6,
    Gateway1 = 12,
    Gateway1MAC = 13,
    VLANId = 20,
    CiphersuiteSupport = 22,
    CiphersuiteEntries = 23,
    cipherSuitePrivilegeLevels = 24,
    IPFamilySupport = 50,
    IPFamilyEnables = 51,
    IPv6Status = 55,
    IPv6StaticAddresses = 56,
    IPv6DynamicAddresses = 59,
    IPv6RouterControl = 64,
    IPv6StaticRouter1IP = 65,
    IPv6StaticRouter1MAC = 66,
    IPv6StaticRouter1PrefixLength = 67,
    IPv6StaticRouter1PrefixValue = 68,
};

/** @brief IPMI IP Origin Types */
/* @brief IPMI IP 来源类型（Origin Types） */
enum class IPSrc : uint8_t
{
    Unspecified = 0,
    Static = 1,
    DHCP = 2,
    BIOS = 3,
    BMC = 4,
};

/** @brief IPMI Set Status */
/* @brief IPMI 设置状态（Set Status） */
enum class SetStatus : uint8_t
{
    Complete = 0,
    InProgress = 1,
    Commit = 2,
};

/** @brief IPMI Family Support Bits */
/* @brief IPMI 地址族支持位（Family Support Bits） */
namespace IPFamilySupportFlag
{
constexpr uint8_t IPv6Only = 0;
constexpr uint8_t DualStack = 1;
constexpr uint8_t IPv6Alerts = 2;
} // namespace IPFamilySupportFlag

/** @brief IPMI IPFamily Enables Flag */
/* @brief IPMI IPFamily 使能标志 */
enum class IPFamilyEnables : uint8_t
{
    IPv4Only = 0,
    IPv6Only = 1,
    DualStack = 2,
};

/** @brief IPMI IPv6 Dynamic Status Bits */
/* @brief IPMI IPv6 动态状态位 */
namespace IPv6StatusFlag
{
constexpr uint8_t DHCP = 0;
constexpr uint8_t SLAAC = 1;
}; // namespace IPv6StatusFlag

/** @brief IPMI IPv6 Source */
/* @brief IPMI IPv6 来源 */
enum class IPv6Source : uint8_t
{
    Static = 0,
    SLAAC = 1,
    DHCP = 2,
};

/** @brief IPMI IPv6 Address Status */
/* @brief IPMI IPv6 地址状态 */
enum class IPv6AddressStatus : uint8_t
{
    Active = 0,
    Disabled = 1,
};

namespace IPv6RouterControlFlag
{
constexpr uint8_t Static = 0;
constexpr uint8_t Dynamic = 1;
}; // namespace IPv6RouterControlFlag

// VLANs are a 12-bit value
// VLAN 是一个 12 位的值
constexpr uint16_t VLAN_VALUE_MASK = 0x0fff;
constexpr uint16_t VLAN_ENABLE_FLAG = 0x8000;

// Arbitrary v4 Address Limits
// 自行设定的 IPv4 地址数量上限
constexpr uint8_t MAX_IPV4_ADDRESSES = 2;

// Arbitrary v6 Address Limits to prevent too much output in ipmitool
// 自行设定的 IPv6 地址数量上限，以避免 ipmitool 输出过多
constexpr uint8_t MAX_IPV6_STATIC_ADDRESSES = 15;
constexpr uint8_t MAX_IPV6_DYNAMIC_ADDRESSES = 15;

// Prefix length limits of phosphor-networkd
// phosphor-networkd 的前缀长度限制
constexpr uint8_t MIN_IPV4_PREFIX_LENGTH = 1;
constexpr uint8_t MAX_IPV4_PREFIX_LENGTH = 32;
constexpr uint8_t MIN_IPV6_PREFIX_LENGTH = 1;
constexpr uint8_t MAX_IPV6_PREFIX_LENGTH = 128;

/** @enum SolConfParam
 *
 *  using for Set/Get SOL configuration parameters command.
 */
/*  @enum SolConfParam
 *   用于 Set/Get SOL configuration parameters 命令。 */
enum class SolConfParam : uint8_t
{
    Progress,       //!< Set In Progress.
    Enable,         //!< SOL Enable.
    Authentication, //!< SOL Authentication.
    Accumulate,     //!< Character Accumulate Interval & Send Threshold.
    Retry,          //!< SOL Retry.
    NonVbitrate,    //!< SOL non-volatile bit rate.
    Vbitrate,       //!< SOL volatile bit rate.
    Channel,        //!< SOL payload channel.
    Port,           //!< SOL payload port.
};

} // namespace transport
} // namespace ipmi
