
/**
 * @file transporthandler.cpp
 * @brief Transport/LAN IPMI 命令：网络参数（IP/网关/MAC/VLAN）配置。
 */
/*  @file transporthandler.cpp
 *  @brief Transport/LAN IPMI 命令：网络参数（IP/网关/MAC/VLAN）配置。 */
#include "transporthandler.hpp"

#include <ipmid/utils.hpp>
#include <org/freedesktop/DBus/Error/event.hpp>
#include <phosphor-logging/lg2.hpp>
#include <stdplus/net/addr/subnet.hpp>
#include <stdplus/raw.hpp>

#include <array>
#include <fstream>

using phosphor::logging::elog;
using sdbusplus::error::org::freedesktop::d_bus::Error::UnknownObject;
using sdbusplus::error::xyz::openbmc_project::common::InternalFailure;
using sdbusplus::error::xyz::openbmc_project::common::InvalidArgument;
using sdbusplus::error::xyz::openbmc_project::common::NotAllowed;
using sdbusplus::server::xyz::openbmc_project::network::EthernetInterface;
using sdbusplus::server::xyz::openbmc_project::network::IP;
using sdbusplus::server::xyz::openbmc_project::network::Neighbor;

namespace cipher
{

std::vector<uint8_t> getCipherList()
{
    std::vector<uint8_t> cipherList;

    std::ifstream jsonFile(cipher::configFile);
    if (!jsonFile.is_open())
    {
        lg2::error("Channel Cipher suites file not found");
        elog<InternalFailure>();
    }

    auto data = Json::parse(jsonFile, nullptr, false);
    if (data.is_discarded())
    {
        lg2::error("Parsing channel cipher suites JSON failed");
        elog<InternalFailure>();
    }

    // Byte 1 is reserved
    // 字节 1 为保留位
    cipherList.push_back(0x00);

    for (const auto& record : data)
    {
        cipherList.push_back(record.value(cipher, 0));
    }

    return cipherList;
}
} // namespace cipher

namespace ipmi
{
namespace transport
{

// LAN Handler specific response codes
// LAN Handler 专用的响应码
constexpr Cc ccParamNotSupported = 0x80;
constexpr Cc ccParamSetLocked = 0x81;
constexpr Cc ccParamReadOnly = 0x82;
constexpr Cc ccWriteReadParameter = 0x82;

static inline auto responseParamNotSupported()
{
    return response(ccParamNotSupported);
}

static inline auto responseParamSetLocked()
{
    return response(ccParamSetLocked);
}

static inline auto responseParamReadOnly()
{
    return response(ccParamReadOnly);
}

static inline auto responseWriteReadParameter()
{
    return response(ccWriteReadParameter);
}

/** @brief Valid address origins for IPv4 */
/* @brief IPv4 的有效地址来源 */
const std::unordered_set<IP::AddressOrigin> originsV4 = {
    IP::AddressOrigin::Static,
    IP::AddressOrigin::DHCP,
};

static constexpr uint8_t oemCmdStart = 192;

// Checks if the ifname is part of the networkd path
// This assumes the path came from the network subtree PATH_ROOT
// 检查 ifname 是否属于 networkd 路径的一部分。
// 这里假设该路径来自 network 子树 PATH_ROOT。
bool ifnameInPath(std::string_view ifname, std::string_view path)
{
    constexpr auto rs = PATH_ROOT.size() + 1; // ROOT + separator
    const auto is = rs + ifname.size();       // ROOT + sep + ifname
    return path.size() > rs && path.substr(rs).starts_with(ifname) &&
           (path.size() == is || path[is] == '/' ||
            path[is] == '_'); // handle VLAN IF uses '_' e.g.
                              // /xyz/openbmc_project/network/eth0_2
                              // 路径示例：/xyz/openbmc_project/network/eth0_2
}

std::optional<ChannelParams> maybeGetChannelParams(sdbusplus::bus_t& bus,
                                                   uint8_t channel)
{
    auto ifname = getChannelName(channel);
    if (ifname.empty())
    {
        return std::nullopt;
    }

    // Enumerate all VLAN + ETHERNET interfaces
    // 枚举所有 VLAN 和 ETHERNET 接口
    std::vector<std::string> interfaces = {INTF_VLAN, INTF_ETHERNET};
    ipmi::ObjectTree objs =
        ipmi::getSubTree(bus, interfaces, std::string{PATH_ROOT});

    ChannelParams params;
    for (const auto& [path, impls] : objs)
    {
        if (!ifnameInPath(ifname, path))
        {
            continue;
        }
        for (const auto& [service, intfs] : impls)
        {
            bool vlan = false;
            bool ethernet = false;
            for (const auto& intf : intfs)
            {
                if (intf == INTF_VLAN)
                {
                    vlan = true;
                }
                else if (intf == INTF_ETHERNET)
                {
                    ethernet = true;
                }
            }
            if (params.service.empty() && (vlan || ethernet))
            {
                params.service = service;
            }
            if (params.ifPath.empty() && !vlan && ethernet)
            {
                params.ifPath = path;
            }
            if (params.logicalPath.empty() && vlan)
            {
                params.logicalPath = path;
            }
        }
    }

    // We must have a path for the underlying interface
    // 底层接口必须有一个路径
    if (params.ifPath.empty())
    {
        return std::nullopt;
    }
    // We don't have a VLAN so the logical path is the same
    // 没有 VLAN，因此逻辑路径（logical path）相同
    if (params.logicalPath.empty())
    {
        params.logicalPath = params.ifPath;
    }

    params.id = channel;
    params.ifname = std::move(ifname);
    return params;
}

ChannelParams getChannelParams(sdbusplus::bus_t& bus, uint8_t channel)
{
    auto params = maybeGetChannelParams(bus, channel);
    if (!params)
    {
        lg2::error("Failed to get channel params: {CHANNEL}", "CHANNEL",
                   channel);
        elog<InternalFailure>();
    }
    return std::move(*params);
}

/** @brief Get / Set the Property value from phosphor-networkd EthernetInterface
 */
/* @brief 从 phosphor-networkd 的 EthernetInterface 获取 / 设置属性值。 */
template <typename T>
static T getEthProp(sdbusplus::bus_t& bus, const ChannelParams& params,
                    const std::string& prop)
{
    return std::get<T>(getDbusProperty(bus, params.service, params.logicalPath,
                                       INTF_ETHERNET, prop));
}
template <typename T>
static void setEthProp(sdbusplus::bus_t& bus, const ChannelParams& params,
                       const std::string& prop, const T& t)
{
    return setDbusProperty(bus, params.service, params.logicalPath,
                           INTF_ETHERNET, prop, t);
}

/** @brief Determines the MAC of the ethernet interface
 *
 *  @param[in] bus    - The bus object used for lookups
 *  @param[in] params - The parameters for the channel
 *  @return The configured mac address
 */
/* @brief 确定以太网接口的 MAC 地址。
 *  @param[in] bus - 用于查找的总线对象
 *  @param[in] params - 通道的参数
 *  @return 配置的 MAC 地址 */
stdplus::EtherAddr getMACProperty(sdbusplus::bus_t& bus,
                                  const ChannelParams& params)
{
    auto prop = getDbusProperty(bus, params.service, params.ifPath, INTF_MAC,
                                "MACAddress");
    return stdplus::fromStr<stdplus::EtherAddr>(std::get<std::string>(prop));
}

/** @brief Sets the system value for MAC address on the given interface
 *
 *  @param[in] bus    - The bus object used for lookups
 *  @param[in] params - The parameters for the channel
 *  @param[in] mac    - MAC address to apply
 */
/* @brief 在指定接口上设置系统的 MAC 地址值。
 *  @param[in] bus - 用于查找的总线对象
 *  @param[in] params - 通道的参数
 *  @param[in] mac - 要应用的 MAC 地址 */
void setMACProperty(sdbusplus::bus_t& bus, const ChannelParams& params,
                    stdplus::EtherAddr mac)
{
    std::string macStr = stdplus::toStr(mac);
    setDbusProperty(bus, params.service, params.ifPath, INTF_MAC, "MACAddress",
                    macStr);
}

void deleteObjectIfExists(sdbusplus::bus_t& bus, const std::string& service,
                          const std::string& path)
{
    if (path.empty())
    {
        return;
    }
    try
    {
        auto req = bus.new_method_call(service.c_str(), path.c_str(),
                                       ipmi::DELETE_INTERFACE, "Delete");
        bus.call_noreply(req);
    }
    catch (const UnknownObject&)
    {
        // Object already gone; nothing to do.
        // 对象已不存在，无需处理。
    }
    catch (const sdbusplus::exception_t& e)
    {
        // InternalFailure and NotAllowed are legacy errors, so match them
        // by name.
        // InternalFailure 和 NotAllowed 是遗留错误类型，因此通过
        // 名称来匹配它们。
        if (std::string_view{InternalFailure::errName} != e.name() &&
            std::string_view{NotAllowed::errName} != e.name())
        {
            // We want to rethrow real errors
            // 我们希望将真正的错误重新抛出
            throw;
        }
    }
}

/** @brief Sets the address info configured for the interface
 *         If a previous address path exists then it will be removed
 *         before the new address is added.
 *
 *  @param[in] bus     - The bus object used for lookups
 *  @param[in] params  - The parameters for the channel
 *  @param[in] address - The address of the new IP
 *  @param[in] prefix  - The prefix of the new IP
 */
/* @brief 设置接口上配置的地址信息；
 *         如果之前存在某个地址路径，会在添加新地址之前
 *         将其删除。
 *   @param[in] bus - 用于查找的总线对象
 *   @param[in] params - 通道的参数
 *   @param[in] address - 新 IP 的地址
 *   @param[in] prefix - 新 IP 的前缀 */
template <int family>
void createIfAddr(sdbusplus::bus_t& bus, const ChannelParams& params,
                  typename AddrFamily<family>::addr address, uint8_t prefix)
{
    auto newreq =
        bus.new_method_call(params.service.c_str(), params.logicalPath.c_str(),
                            INTF_IP_CREATE, "IP");
    std::string protocol =
        sdbusplus::common::xyz::openbmc_project::network::convertForMessage(
            AddrFamily<family>::protocol);
    stdplus::ToStrHandle<stdplus::ToStr<typename AddrFamily<family>::addr>> tsh;
    newreq.append(protocol, tsh(address), prefix, "");
    bus.call_noreply(newreq);
}

/** @brief Trivial helper for getting the IPv4 address from getIfAddrs()
 *
 *  @param[in] bus    - The bus object used for lookups
 *  @param[in] params - The parameters for the channel
 *  @return The address and prefix if found
 */
/* @brief 从 getIfAddrs() 获取 IPv4 地址的简易辅助函数。
 *  @param[in] bus - 用于查找的总线对象
 *  @param[in] params - 通道的参数
 *  @return 找到的地址和前缀 */
auto getIfAddr4(sdbusplus::bus_t& bus, const ChannelParams& params)
{
    std::optional<IfAddr<AF_INET>> ifaddr4 = std::nullopt;
    IP::AddressOrigin src;

    try
    {
        src = std::get<bool>(
                  getDbusProperty(bus, params.service, params.logicalPath,
                                  INTF_ETHERNET, "DHCP4"))
                  ? IP::AddressOrigin::DHCP
                  : IP::AddressOrigin::Static;
    }
    catch (const sdbusplus::exception_t& e)
    {
        lg2::error("Failed to get IPv4 source");
        return ifaddr4;
    }

    for (uint8_t i = 0; i < MAX_IPV4_ADDRESSES; ++i)
    {
        ifaddr4 = getIfAddr<AF_INET>(bus, params, i, originsV4);
        if (ifaddr4 && src == ifaddr4->origin)
        {
            break;
        }
        else
        {
            ifaddr4 = std::nullopt;
        }
    }
    return ifaddr4;
}

/** @brief Reconfigures the IPv4 address info configured for the interface
 *
 *  @param[in] bus     - The bus object used for lookups
 *  @param[in] params  - The parameters for the channel
 *  @param[in] address - The new address if specified
 *  @param[in] prefix  - The new address prefix if specified
 */
/* @brief 重新配置接口上配置的 IPv4 地址信息
 *   @param[in] bus - 用于查找的总线对象
 *   @param[in] params - 通道的参数
 *   @param[in] address - 新地址（如果指定）
 *   @param[in] prefix - 新地址前缀（如果指定） */
void reconfigureIfAddr4(sdbusplus::bus_t& bus, const ChannelParams& params,
                        std::optional<stdplus::In4Addr> address,
                        std::optional<uint8_t> prefix)
{
    auto ifaddr = getIfAddr4(bus, params);
    if (!ifaddr && !address)
    {
        lg2::error("Missing address for IPv4 assignment");
        elog<InternalFailure>();
    }
    uint8_t fallbackPrefix = AddrFamily<AF_INET>::defaultPrefix;
    auto addr = stdplus::In4Addr{};
    if (ifaddr)
    {
        addr = ifaddr->address;
        fallbackPrefix = ifaddr->prefix;
        deleteObjectIfExists(bus, params.service, ifaddr->path);
    }
    addr = address.value_or(addr);
    if (addr != stdplus::In4Addr{})
    {
        createIfAddr<AF_INET>(bus, params, addr,
                              prefix.value_or(fallbackPrefix));
    }
}

template <int family>
std::optional<IfNeigh<family>> findGatewayNeighbor(sdbusplus::bus_t& bus,
                                                   const ChannelParams& params,
                                                   ObjectLookupCache& neighbors)
{
    auto gateway = getGatewayProperty<family>(bus, params);
    if (!gateway)
    {
        return std::nullopt;
    }

    return findStaticNeighbor<family>(bus, params, *gateway, neighbors);
}

template <int family>
std::optional<IfNeigh<family>> getGatewayNeighbor(sdbusplus::bus_t& bus,
                                                  const ChannelParams& params)
{
    ObjectLookupCache neighbors(bus, params, INTF_NEIGHBOR);
    return findGatewayNeighbor<family>(bus, params, neighbors);
}

template <int family>
void reconfigureGatewayMAC(sdbusplus::bus_t& bus, const ChannelParams& params,
                           stdplus::EtherAddr mac)
{
    auto gateway = getGatewayProperty<family>(bus, params);
    if (!gateway)
    {
        lg2::error("Tried to set Gateway MAC without Gateway");
        elog<InternalFailure>();
    }

    ObjectLookupCache neighbors(bus, params, INTF_NEIGHBOR);
    auto neighbor =
        findStaticNeighbor<family>(bus, params, *gateway, neighbors);
    if (neighbor)
    {
        deleteObjectIfExists(bus, params.service, neighbor->path);
    }

    createNeighbor<family>(bus, params, *gateway, mac);
}

/** @brief Deconfigures the IPv6 address info configured for the interface
 *
 *  @param[in] bus     - The bus object used for lookups
 *  @param[in] params  - The parameters for the channel
 *  @param[in] idx     - The address index to operate on
 */
/* @brief 取消配置接口上配置的 IPv6 地址信息
 *   @param[in] bus - 用于查找的总线对象
 *   @param[in] params - 通道的参数
 *   @param[in] idx - 要操作的地址索引 */
void deconfigureIfAddr6(sdbusplus::bus_t& bus, const ChannelParams& params,
                        uint8_t idx)
{
    auto ifaddr = getIfAddr<AF_INET6>(bus, params, idx, originsV6Static);
    if (ifaddr)
    {
        deleteObjectIfExists(bus, params.service, ifaddr->path);
    }
}

/** @brief Reconfigures the IPv6 address info configured for the interface
 *
 *  @param[in] bus     - The bus object used for lookups
 *  @param[in] params  - The parameters for the channel
 *  @param[in] idx     - The address index to operate on
 *  @param[in] address - The new address
 *  @param[in] prefix  - The new address prefix
 */
/* @brief 重新配置接口上配置的 IPv6 地址信息
 *   @param[in] bus - 用于查找的总线对象
 *   @param[in] params - 通道的参数
 *   @param[in] idx - 要操作的地址索引
 *   @param[in] address - 新地址
 *   @param[in] prefix - 新地址前缀 */
void reconfigureIfAddr6(sdbusplus::bus_t& bus, const ChannelParams& params,
                        uint8_t idx, stdplus::In6Addr address, uint8_t prefix)
{
    deconfigureIfAddr6(bus, params, idx);
    createIfAddr<AF_INET6>(bus, params, address, prefix);
}

/** @brief Converts the AddressOrigin into an IPv6Source
 *
 *  @param[in] origin - The DBus Address Origin to convert
 *  @return The IPv6Source version of the origin
 */
/* @brief 将 AddressOrigin 转换为 IPv6Source
 *   @param[in] origin - 要转换的 DBus Address Origin
 *   @return 转换后的 IPv6Source 版本 origin */
IPv6Source originToSourceType(IP::AddressOrigin origin)
{
    switch (origin)
    {
        case IP::AddressOrigin::Static:
            return IPv6Source::Static;
        case IP::AddressOrigin::DHCP:
            return IPv6Source::DHCP;
        case IP::AddressOrigin::SLAAC:
            return IPv6Source::SLAAC;
        default:
        {
            auto originStr = sdbusplus::common::xyz::openbmc_project::network::
                convertForMessage(origin);
            lg2::error("Invalid IP::AddressOrigin conversion to IPv6Source, "
                       "origin: {ORIGIN}",
                       "ORIGIN", originStr);
            elog<InternalFailure>();
        }
    }
}

/** @brief Packs the IPMI message response with IPv6 address data
 *
 *  @param[out] ret     - The IPMI response payload to be packed
 *  @param[in]  channel - The channel id corresponding to an ethernet interface
 *  @param[in]  set     - The set selector for determining address index
 *  @param[in]  origins - Set of valid origins for address filtering
 */
/* @brief 用 IPv6 地址数据打包 IPMI 消息响应
 *   @param[out] ret - 要打包的 IPMI 响应负载
 *   @param[in] channel - 对应以太网接口的通道 id
 *   @param[in] set - 用于确定地址索引的 set selector
 *   @param[in] origins - 用于地址过滤的有效 origin 集合 */
void getLanIPv6Address(message::Payload& ret, uint8_t channel, uint8_t set,
                       const std::unordered_set<IP::AddressOrigin>& origins)
{
    auto source = IPv6Source::Static;
    bool enabled = false;
    stdplus::In6Addr addr{};
    uint8_t prefix{};
    auto status = IPv6AddressStatus::Disabled;

    auto ifaddr = channelCall<getIfAddr<AF_INET6>>(channel, set, origins);
    if (ifaddr)
    {
        source = originToSourceType(ifaddr->origin);
        enabled = (origins == originsV6Static);
        addr = ifaddr->address;
        prefix = ifaddr->prefix;
        status = IPv6AddressStatus::Active;
    }

    ret.pack(set);
    ret.pack(types::enum_cast<uint4_t>(source), uint3_t{}, enabled);
    ret.pack(stdplus::raw::asView<char>(addr));
    ret.pack(prefix);
    ret.pack(types::enum_cast<uint8_t>(status));
}

/** @brief Gets the vlan ID configured on the interface
 *
 *  @param[in] bus    - The bus object used for lookups
 *  @param[in] params - The parameters for the channel
 *  @return VLAN id or the standard 0 for no VLAN
 */
/* @brief 获取接口上配置的 vlan ID
 *   @param[in] bus - 用于查找的总线对象
 *   @param[in] params - 通道的参数
 *   @return VLAN id；无 VLAN 时为标准的 0 */
uint16_t getVLANProperty(sdbusplus::bus_t& bus, const ChannelParams& params)
{
    // VLAN devices will always have a separate logical object
    // VLAN 设备始终拥有独立的逻辑对象
    if (params.ifPath == params.logicalPath)
    {
        return 0;
    }

    auto vlan = std::get<uint32_t>(getDbusProperty(
        bus, params.service, params.logicalPath, INTF_VLAN, "Id"));
    if ((vlan & VLAN_VALUE_MASK) != vlan)
    {
        lg2::error("networkd returned an invalid vlan: {VLAN} "
                   "(CH={CHANNEL}, IF={IFNAME})",
                   "CHANNEL", params.id, "IFNAME", params.ifname, "VLAN", vlan);
        elog<InternalFailure>();
    }
    return vlan;
}

/** @brief Deletes all of the possible configuration parameters for a channel
 *
 *  @param[in] bus    - The bus object used for lookups
 *  @param[in] params - The parameters for the channel
 */
/* @brief 删除某个通道所有可能的配置参数
 *   @param[in] bus - 用于查找的总线对象
 *   @param[in] params - 通道的参数 */
void deconfigureChannel(sdbusplus::bus_t& bus, ChannelParams& params)
{
    // Delete all objects associated with the interface
    // 删除与该接口关联的所有对象
    ObjectTree objs =
        ipmi::getSubTree(bus, std::vector<std::string>{DELETE_INTERFACE},
                         std::string{PATH_ROOT});
    for (const auto& [path, impls] : objs)
    {
        if (!ifnameInPath(params.ifname, path))
        {
            continue;
        }
        for (const auto& [service, intfs] : impls)
        {
            deleteObjectIfExists(bus, service, path);
        }
        // Update params to reflect the deletion of vlan
        // 更新 params 以反映 VLAN 已被删除
        if (path == params.logicalPath)
        {
            params.logicalPath = params.ifPath;
        }
    }

    // Clear out any settings on the lower physical interface
    // 清除底层物理接口上的所有设置
    setEthProp(bus, params, "DHCP4", false);
    setEthProp(bus, params, "DHCP6", false);
    setEthProp(bus, params, "IPv6AcceptRA", false);
}

/** @brief Deletes all of the possible configuration parameters for VLAN
 * interface
 *
 *  @param[in] bus    - The bus object used for lookups
 *  @param[in] params - The parameters for the vlan interface
 */
/* @brief 删除 VLAN 接口所有可能的
 * 配置参数。
 *  @param[in] bus - 用于查找的总线对象
 *  @param[in] params - VLAN 接口的参数 */
void deconfigureVLAN(sdbusplus::bus_t& bus, ChannelParams& params)
{
    std::vector<std::string> interfaces = {INTF_VLAN};
    ObjectTree objs = ipmi::getSubTree(bus, interfaces, std::string{PATH_ROOT});

    for (const auto& [path, impls] : objs)
    {
        if (!ifnameInPath(params.ifname, path))
        {
            continue;
        }
        for (const auto& [service, intfs] : impls)
        {
            deleteObjectIfExists(bus, service, path);
        }
        if (path == params.logicalPath)
        {
            params.logicalPath = params.ifPath;
        }
    }
}

/** @brief Creates a new VLAN on the specified interface
 *
 *  @param[in] bus    - The bus object used for lookups
 *  @param[in] params - The parameters for the channel
 *  @param[in] vlan   - The id of the new vlan
 */
/* @brief 在指定接口上创建新的 VLAN
 *   @param[in] bus - 用于查找的总线对象
 *   @param[in] params - 通道的参数
 *   @param[in] vlan - 新 vlan 的 id */
void createVLAN(sdbusplus::bus_t& bus, ChannelParams& params, uint16_t vlan)
{
    if (vlan == 0)
    {
        return;
    }

    auto req = bus.new_method_call(params.service.c_str(), PATH_ROOT.c_str(),
                                   INTF_VLAN_CREATE, "VLAN");
    req.append(params.ifname, static_cast<uint32_t>(vlan));
    auto reply = bus.call(req);
    auto newPath = reply.unpack<sdbusplus::object_path>();

    params.logicalPath = std::move(newPath);
}

/** @brief Performs the necessary reconfiguration to change the VLAN
 *
 *  @param[in] bus    - The bus object used for lookups
 *  @param[in] params - The parameters for the channel
 *  @param[in] vlan   - The new vlan id to use
 */
/* @brief 执行更改 VLAN 所需的重新配置
 *   @param[in] bus - 用于查找的总线对象
 *   @param[in] params - 通道的参数
 *   @param[in] vlan - 要使用的新 vlan id */
void reconfigureVLAN(sdbusplus::bus_t& bus, ChannelParams& params,
                     uint16_t vlan)
{
    // If the VLAN id is unchanged, no reconfiguration is needed
    // 如果 VLAN id 未变化，则无需重新配置
    uint16_t currentVlan = getVLANProperty(bus, params);
    if (currentVlan == vlan)
    {
        lg2::info("VLAN id {VLAN} is already configured on {IFNAME}, "
                  "skipping reconfiguration",
                  "VLAN", vlan, "IFNAME", params.ifname);
        return;
    }

    // Unfortunately we don't have built-in functions to migrate our interface
    // customizations to new VLAN interfaces, or have some kind of decoupling.
    // We therefore must retain all of our old information, setup the new VLAN
    // configuration, then restore the old info.
    // 遗憾的是，我们没有内置函数可以把接口的自定义设置
    // 迁移到新的 VLAN 接口上，也没有某种解耦机制。
    // 因此我们只能保留所有旧信息，建立新的 VLAN 配置，
    // 然后再恢复旧信息。

    // Save info from the old logical interface
    // 保存旧逻辑接口上的信息
    bool dhcp4 = getEthProp<bool>(bus, params, "DHCP4");
    bool dhcp6 = getEthProp<bool>(bus, params, "DHCP6");
    bool ra = getEthProp<bool>(bus, params, "IPv6AcceptRA");
    ObjectLookupCache ips(bus, params, NetworkIP::interface);
    auto ifaddr4 = findIfAddr<AF_INET>(bus, params, 0, originsV4, ips);
    std::vector<IfAddr<AF_INET6>> ifaddrs6;
    for (uint8_t i = 0; i < MAX_IPV6_STATIC_ADDRESSES; ++i)
    {
        auto ifaddr6 =
            findIfAddr<AF_INET6>(bus, params, i, originsV6Static, ips);
        if (!ifaddr6)
        {
            break;
        }
        ifaddrs6.push_back(std::move(*ifaddr6));
    }
    ObjectLookupCache neighbors(bus, params, INTF_NEIGHBOR);
    auto neighbor4 = findGatewayNeighbor<AF_INET>(bus, params, neighbors);
    auto neighbor6 = findGatewayNeighbor<AF_INET6>(bus, params, neighbors);

    if (params.logicalPath != params.ifPath)
    {
        deconfigureVLAN(bus, params);
    }
    createVLAN(bus, params, vlan);

    setEthProp(bus, params, "DHCP4", dhcp4);
    setEthProp(bus, params, "DHCP6", dhcp6);
    setEthProp(bus, params, "IPv6AcceptRA", ra);
    if (ifaddr4)
    {
        createIfAddr<AF_INET>(bus, params, ifaddr4->address, ifaddr4->prefix);
    }
    for (const auto& ifaddr6 : ifaddrs6)
    {
        createIfAddr<AF_INET6>(bus, params, ifaddr6.address, ifaddr6.prefix);
    }
    if (neighbor4)
    {
        createNeighbor<AF_INET>(bus, params, neighbor4->ip, neighbor4->mac);
    }
    if (neighbor6)
    {
        createNeighbor<AF_INET6>(bus, params, neighbor6->ip, neighbor6->mac);
    }
}

// We need to store this value so it can be returned to the client
// It is volatile so safe to store in daemon memory.
// 我们需要存储这个值，以便返回给客户端。
// 它是易失的，因此存放在守护进程内存中是安全的。
static std::unordered_map<uint8_t, SetStatus> setStatus;

// Until we have good support for fixed versions of IPMI tool
// we need to return the VLAN id for disabled VLANs. The value is only
// used for verification that a disable operation succeeded and will only
// be sent if our system indicates that vlans are disabled.
// 在获得对固定版本 IPMI 工具的良好支持之前，
// 我们需要为禁用 VLAN 的情况返回 VLAN id。
// 该值仅用于验证禁用操作是否成功，且只有在系统
// 指示 VLAN 已禁用时才会发送。
static std::unordered_map<uint8_t, uint16_t> lastDisabledVlan;

/** @brief Gets the set status for the channel if it exists
 *         Otherwise populates and returns the default value.
 *
 *  @param[in] channel - The channel id corresponding to an ethernet interface
 *  @return A reference to the SetStatus for the channel
 */
/* @brief 获取通道的 set 状态（若存在），
 *         否则填充并返回默认值。
 *   @param[in] channel - 对应以太网接口的通道 id
 *   @return 指向该通道 SetStatus 的引用 */
SetStatus& getSetStatus(uint8_t channel)
{
    auto it = setStatus.find(channel);
    if (it != setStatus.end())
    {
        return it->second;
    }
    return setStatus[channel] = SetStatus::Complete;
}

/** @brief Unpacks the trivially copyable type from the message */
/* @brief 从消息中解包可平凡复制（trivially copyable）的类型 */
template <typename T>
static T unpackT(message::Payload& req)
{
    std::array<uint8_t, sizeof(T)> bytes;
    if (req.unpack(bytes) != 0)
    {
        throw ccReqDataLenInvalid;
    }
    return stdplus::raw::copyFrom<T>(bytes);
}

/** @brief Ensure the message is fully unpacked */
/* @brief 确保消息已被完全解包 */
static void unpackFinal(message::Payload& req)
{
    if (!req.fullyUnpacked())
    {
        throw ccReqDataTruncated;
    }
}

/**
 * Define placeholder command handlers for the OEM Extension bytes for the Set
 * LAN Configuration Parameters and Get LAN Configuration Parameters
 * commands. Using "weak" linking allows the placeholder setLanOem/getLanOem
 * functions below to be overridden.
 * To create handlers for your own proprietary command set:
 *   Create/modify a phosphor-ipmi-host Bitbake append file within your Yocto
 *   recipe
 *   Create C++ file(s) that define IPMI handler functions matching the
 *     function names below (i.e. setLanOem). The default name for the
 *     transport IPMI commands is transporthandler_oem.cpp.
 *   Add:
 *      EXTRA_OEMESON:append = "-Dtransport-oem=enabled"
 *   Create a do_configure:prepend()/do_install:append() method in your
 *   bbappend file to copy the file to the build directory.
 *   Add:
 *   PROJECT_SRC_DIR := "${THISDIR}/${PN}"
 *   # Copy the "strong" functions into the working directory, overriding the
 *   # placeholder functions.
 *   do_configure:prepend(){
 *      cp -f ${PROJECT_SRC_DIR}/transporthandler_oem.cpp ${S}
 *   }
 *
 *   # Clean up after compilation has completed
 *   do_install:append(){
 *      rm -f ${S}/transporthandler_oem.cpp
 *   }
 *
 */
/* 为 Set LAN Configuration Parameters 和 Get LAN Configuration
 * Parameters 命令的 OEM Extension 字节定义占位命令处理函数。
 * 使用 "weak"（弱）链接可以让下面的占位函数 setLanOem/getLanOem
 * 被覆盖。
 * 若要为你自己的专有命令集创建处理函数：
 *   在你的 Yocto recipe 中创建/修改一个 phosphor-ipmi-host 的
 *   Bitbake append 文件（bbappend）
 *   创建 C++ 文件，定义与下面函数名匹配的 IPMI 处理函数
 *     （即 setLanOem）。传输层 IPMI 命令的默认文件名为
 *     transporthandler_oem.cpp。
 *   添加：
 *      EXTRA_OEMESON:append = "-Dtransport-oem=enabled"
 *   在你的 bbappend 文件中创建 do_configure:prepend()/
 *   do_install:append() 方法，把该文件复制到构建目录。
 *   添加：
 *   PROJECT_SRC_DIR := "${THISDIR}/${PN}"
 *   # 将 "strong"（强）函数复制到工作目录，覆盖
 *   # 占位函数。
 *   do_configure:prepend(){
 *      cp -f ${PROJECT_SRC_DIR}/transporthandler_oem.cpp ${S}
 *   }
 *   # 编译完成后清理
 *   do_install:append(){
 *      rm -f ${S}/transporthandler_oem.cpp
 *   } */

/**
 * Define the placeholder OEM commands as having weak linkage. Create
 * setLanOem, and getLanOem functions in the transporthandler_oem.cpp
 * file. The functions defined there must not have the "weak" attribute
 * applied to them.
 */
/*  将这些占位 OEM 命令定义为弱链接。
 *  在 transporthandler_oem.cpp 文件中创建 setLanOem
 *  和 getLanOem 函数。其中定义的函数不能带有
 *  "weak" 属性。 */
RspType<> setLanOem(uint8_t channel, uint8_t parameter, message::Payload& req)
    __attribute__((weak));
RspType<message::Payload> getLanOem(uint8_t channel, uint8_t parameter,
                                    uint8_t set, uint8_t block)
    __attribute__((weak));

RspType<> setLanOem(uint8_t, uint8_t, message::Payload& req)
{
    req.trailingOk = true;
    return responseParamNotSupported();
}

RspType<message::Payload> getLanOem(uint8_t, uint8_t, uint8_t, uint8_t)
{
    return responseParamNotSupported();
}

/**
 * @brief is a valid LAN channel.
 *
 * This function checks whether the input channel is a valid LAN channel or not.
 *
 * @param[in] channel: the channel number.
 * @return nullopt if the channel is invalid, false if the channel is not a LAN
 * channel, true if the channel is a LAN channel.
 **/
/*  @brief 判断是否为有效的 LAN 通道。
 *  本函数检查输入通道是否为有效的 LAN 通道。
 *  @param[in] channel: 通道号。
 *  @return 通道无效时返回 nullopt，不是 LAN 通道时返回 false，
 *  是 LAN 通道时返回 true。 */
std::optional<bool> isLanChannel(uint8_t channel)
{
    ChannelInfo chInfo;
    auto cc = getChannelInfo(channel, chInfo);
    if (cc != ccSuccess)
    {
        return std::nullopt;
    }

    return chInfo.mediumType ==
           static_cast<uint8_t>(EChannelMediumType::lan8032);
}

RspType<> setLanInt(Context::ptr ctx, uint4_t channelBits, uint4_t reserved1,
                    uint8_t parameter, message::Payload& req)
{
    const uint8_t channel = convertCurrentChannelNum(
        static_cast<uint8_t>(channelBits), ctx->channel);
    if (reserved1 || !isValidChannel(channel))
    {
        lg2::error("Set Lan - Invalid field in request");
        req.trailingOk = true;
        return responseInvalidFieldRequest();
    }

    if (!isLanChannel(channel).value_or(false))
    {
        lg2::error("Set Lan - Not a LAN channel");
        return responseInvalidFieldRequest();
    }

    switch (static_cast<LanParam>(parameter))
    {
        case LanParam::SetStatus:
        {
            uint2_t flag;
            uint6_t rsvd;
            if (req.unpack(flag, rsvd) != 0 || !req.fullyUnpacked())
            {
                return responseReqDataLenInvalid();
            }
            if (rsvd)
            {
                return responseInvalidFieldRequest();
            }
            auto status = static_cast<SetStatus>(static_cast<uint8_t>(flag));
            switch (status)
            {
                case SetStatus::Complete:
                {
                    getSetStatus(channel) = status;
                    return responseSuccess();
                }
                case SetStatus::InProgress:
                {
                    auto& storedStatus = getSetStatus(channel);
                    if (storedStatus == SetStatus::InProgress)
                    {
                        return responseParamSetLocked();
                    }
                    storedStatus = status;
                    return responseSuccess();
                }
                case SetStatus::Commit:
                    if (getSetStatus(channel) != SetStatus::InProgress)
                    {
                        return responseInvalidFieldRequest();
                    }
                    return responseSuccess();
                default:
                    return responseInvalidFieldRequest();
            }
        }
        case LanParam::AuthSupport:
        {
            req.trailingOk = true;
            return responseParamReadOnly();
        }
        case LanParam::AuthEnables:
        {
            req.trailingOk = true;
            return responseParamReadOnly();
        }
        case LanParam::IP:
        {
            if (channelCall<getEthProp<bool>>(channel, "DHCP4"))
            {
                return responseCommandNotAvailable();
            }
            auto ip = unpackT<stdplus::In4Addr>(req);
            unpackFinal(req);
            channelCall<reconfigureIfAddr4>(channel, ip, std::nullopt);
            return responseSuccess();
        }
        case LanParam::IPSrc:
        {
            uint4_t flag;
            uint4_t rsvd;
            if (req.unpack(flag, rsvd) != 0)
            {
                return responseReqDataLenInvalid();
            }
            unpackFinal(req);
            if (rsvd)
            {
                return responseInvalidFieldRequest();
            }
            switch (static_cast<IPSrc>(static_cast<uint8_t>(flag)))
            {
                case IPSrc::DHCP:
                    // The IPSrc IPMI command is only for IPv4
                    // management. Modifying IPv6 state is done using
                    // a completely different Set LAN Configuration
                    // subcommand.
                    // IPSrc IPMI 命令仅用于 IPv4 管理。
                    // 修改 IPv6 状态需要通过一个完全不同的
                    // Set LAN Configuration 子命令
                    // 来完成。
                    channelCall<setEthProp<bool>>(channel, "DHCP4", true);
                    return responseSuccess();
                case IPSrc::Unspecified:
                case IPSrc::Static:
                    channelCall<setEthProp<bool>>(channel, "DHCP4", false);
                    return responseSuccess();
                case IPSrc::BIOS:
                case IPSrc::BMC:
                    return responseInvalidFieldRequest();
            }
            return responseParamNotSupported();
        }
        case LanParam::MAC:
        {
            auto mac = unpackT<stdplus::EtherAddr>(req);
            unpackFinal(req);
            channelCall<setMACProperty>(channel, mac);
            return responseSuccess();
        }
        case LanParam::SubnetMask:
        {
            if (channelCall<getEthProp<bool>>(channel, "DHCP4"))
            {
                return responseCommandNotAvailable();
            }
            uint8_t pfx;
            try
            {
                pfx = stdplus::maskToPfx(unpackT<stdplus::In4Addr>(req));
            }
            catch (const std::invalid_argument&)
            {
                return responseInvalidFieldRequest();
            }
            unpackFinal(req);
            channelCall<reconfigureIfAddr4>(channel, std::nullopt, pfx);
            return responseSuccess();
        }
        case LanParam::Gateway1:
        {
            if (channelCall<getEthProp<bool>>(channel, "DHCP4"))
            {
                return responseCommandNotAvailable();
            }
            auto gateway = unpackT<stdplus::In4Addr>(req);
            unpackFinal(req);
            channelCall<setGatewayProperty<AF_INET>>(channel, gateway);
            return responseSuccess();
        }
        case LanParam::Gateway1MAC:
        {
            auto gatewayMAC = unpackT<stdplus::EtherAddr>(req);
            unpackFinal(req);
            channelCall<reconfigureGatewayMAC<AF_INET>>(channel, gatewayMAC);
            return responseSuccess();
        }
        case LanParam::VLANId:
        {
            uint12_t vlanData;
            uint3_t rsvd;
            bool vlanEnable;

            if (req.unpack(vlanData, rsvd, vlanEnable) != 0)
            {
                return responseReqDataLenInvalid();
            }
            unpackFinal(req);

            if (rsvd)
            {
                return responseInvalidFieldRequest();
            }

            uint16_t vlan = static_cast<uint16_t>(vlanData);

            if (!vlanEnable)
            {
                lastDisabledVlan[channel] = vlan;
                vlan = 0;
            }
            else if (vlan == 0 || vlan == VLAN_VALUE_MASK)
            {
                return responseInvalidFieldRequest();
            }

            channelCall<reconfigureVLAN>(channel, vlan);
            return responseSuccess();
        }
        case LanParam::CiphersuiteSupport:
        case LanParam::CiphersuiteEntries:
        case LanParam::IPFamilySupport:
        {
            req.trailingOk = true;
            return responseParamReadOnly();
        }
        case LanParam::IPFamilyEnables:
        {
            uint8_t enables;
            if (req.unpack(enables) != 0)
            {
                return responseReqDataLenInvalid();
            }
            unpackFinal(req);
            switch (static_cast<IPFamilyEnables>(enables))
            {
                case IPFamilyEnables::DualStack:
                    return responseSuccess();
                case IPFamilyEnables::IPv4Only:
                case IPFamilyEnables::IPv6Only:
                    return responseParamNotSupported();
            }
            return responseParamNotSupported();
        }
        case LanParam::IPv6Status:
        {
            req.trailingOk = true;
            return responseParamReadOnly();
        }
        case LanParam::IPv6StaticAddresses:
        {
            uint8_t set;
            uint7_t rsvd;
            bool enabled;
            uint8_t prefix;
            uint8_t status;
            if (req.unpack(set, rsvd, enabled) != 0)
            {
                return responseReqDataLenInvalid();
            }
            auto ip = unpackT<stdplus::In6Addr>(req);
            if (req.unpack(prefix, status) != 0)
            {
                return responseReqDataLenInvalid();
            }
            unpackFinal(req);
            if (rsvd)
            {
                return responseInvalidFieldRequest();
            }
            if (set >= MAX_IPV6_STATIC_ADDRESSES)
            {
                return responseParmOutOfRange();
            }
            if (enabled)
            {
                if (prefix < MIN_IPV6_PREFIX_LENGTH ||
                    prefix > MAX_IPV6_PREFIX_LENGTH)
                {
                    return responseParmOutOfRange();
                }
                channelCall<reconfigureIfAddr6>(channel, set, ip, prefix);
            }
            else
            {
                channelCall<deconfigureIfAddr6>(channel, set);
            }
            return responseSuccess();
        }
        case LanParam::IPv6DynamicAddresses:
        {
            req.trailingOk = true;
            return responseParamReadOnly();
        }
        case LanParam::IPv6RouterControl:
        {
            std::bitset<8> control;
            constexpr uint8_t reservedRACCBits = 0xfc;
            if (req.unpack(control) != 0)
            {
                return responseReqDataLenInvalid();
            }
            unpackFinal(req);
            if (std::bitset<8> expected(
                    control & std::bitset<8>(reservedRACCBits));
                expected.any())
            {
                return responseParamNotSupported();
            }

            bool enableRA = control[IPv6RouterControlFlag::Dynamic];
            channelCall<setEthProp<bool>>(channel, "IPv6AcceptRA", enableRA);
            channelCall<setEthProp<bool>>(channel, "DHCP6", enableRA);
            return responseSuccess();
        }
        case LanParam::IPv6StaticRouter1IP:
        {
            auto gateway = unpackT<stdplus::In6Addr>(req);
            unpackFinal(req);
            channelCall<setGatewayProperty<AF_INET6>>(channel, gateway);
            return responseSuccess();
        }
        case LanParam::IPv6StaticRouter1MAC:
        {
            auto mac = unpackT<stdplus::EtherAddr>(req);
            unpackFinal(req);
            channelCall<reconfigureGatewayMAC<AF_INET6>>(channel, mac);
            return responseSuccess();
        }
        case LanParam::IPv6StaticRouter1PrefixLength:
        {
            uint8_t prefix;
            if (req.unpack(prefix) != 0)
            {
                return responseReqDataLenInvalid();
            }
            unpackFinal(req);
            if (prefix != 0)
            {
                return responseInvalidFieldRequest();
            }
            return responseSuccess();
        }
        case LanParam::IPv6StaticRouter1PrefixValue:
        {
            unpackT<stdplus::In6Addr>(req);
            unpackFinal(req);
            // Accept any prefix value since our prefix length has to be 0
            // 接受任意前缀值，因为我们的前缀长度必须为 0
            return responseSuccess();
        }
        case LanParam::cipherSuitePrivilegeLevels:
        {
            uint8_t rsvd;
            std::array<uint4_t, ipmi::maxCSRecords> cipherSuitePrivs;

            if (req.unpack(rsvd, cipherSuitePrivs))
            {
                return responseReqDataLenInvalid();
            }
            unpackFinal(req);

            if (rsvd)
            {
                return responseInvalidFieldRequest();
            }

            uint8_t resp =
                getCipherConfigObject(csPrivFileName, csPrivDefaultFileName)
                    .setCSPrivilegeLevels(channel, cipherSuitePrivs);
            if (!resp)
            {
                return responseSuccess();
            }
            else
            {
                req.trailingOk = true;
                return response(resp);
            }
        }
    }

    if (parameter >= oemCmdStart)
    {
        return setLanOem(channel, parameter, req);
    }

    req.trailingOk = true;
    return responseParamNotSupported();
}

RspType<> setLan(Context::ptr ctx, uint4_t channelBits, uint4_t reserved1,
                 uint8_t parameter, message::Payload& req)
{
    try
    {
        return setLanInt(ctx, channelBits, reserved1, parameter, req);
    }
    catch (ipmi::Cc cc)
    {
        return response(cc);
    }
    catch (const sdbusplus::exception_t& e)
    {
        if (std::string_view{InvalidArgument::errName} == e.name())
        {
            return responseInvalidFieldRequest();
        }
        throw;
    }
}

RspType<message::Payload> getLan(Context::ptr ctx, uint4_t channelBits,
                                 uint3_t reserved, bool revOnly,
                                 uint8_t parameter, uint8_t set, uint8_t block)
{
    message::Payload ret;
    constexpr uint8_t current_revision = 0x11;
    ret.pack(current_revision);

    if (revOnly)
    {
        return responseSuccess(std::move(ret));
    }

    const uint8_t channel = convertCurrentChannelNum(
        static_cast<uint8_t>(channelBits), ctx->channel);
    if (reserved || !isValidChannel(channel))
    {
        lg2::error("Get Lan - Invalid field in request");
        return responseInvalidFieldRequest();
    }

    if (!isLanChannel(channel).value_or(false))
    {
        lg2::error("Get Lan - Not a LAN channel");
        return responseInvalidFieldRequest();
    }

    static std::vector<uint8_t> cipherList;
    static bool listInit = false;
    if (!listInit)
    {
        try
        {
            cipherList = cipher::getCipherList();
            listInit = true;
        }
        catch (const std::exception& e)
        {}
    }

    switch (static_cast<LanParam>(parameter))
    {
        case LanParam::SetStatus:
        {
            SetStatus status;
            try
            {
                status = setStatus.at(channel);
            }
            catch (const std::out_of_range&)
            {
                status = SetStatus::Complete;
            }
            ret.pack(types::enum_cast<uint2_t>(status), uint6_t{});
            return responseSuccess(std::move(ret));
        }
        case LanParam::AuthSupport:
        {
            std::bitset<6> support;
            ret.pack(support, uint2_t{});
            return responseSuccess(std::move(ret));
        }
        case LanParam::AuthEnables:
        {
            std::bitset<6> enables;
            ret.pack(enables, uint2_t{}); // Callback
            ret.pack(enables, uint2_t{}); // User
            ret.pack(enables, uint2_t{}); // Operator
            ret.pack(enables, uint2_t{}); // Admin
            ret.pack(enables, uint2_t{}); // OEM
            return responseSuccess(std::move(ret));
        }
        case LanParam::IP:
        {
            auto ifaddr = channelCall<getIfAddr4>(channel);
            stdplus::In4Addr addr{};
            if (ifaddr)
            {
                addr = ifaddr->address;
            }
            ret.pack(stdplus::raw::asView<char>(addr));
            return responseSuccess(std::move(ret));
        }
        case LanParam::IPSrc:
        {
            auto src = channelCall<getEthProp<bool>>(channel, "DHCP4")
                           ? IPSrc::DHCP
                           : IPSrc::Static;
            ret.pack(types::enum_cast<uint4_t>(src), uint4_t{});
            return responseSuccess(std::move(ret));
        }
        case LanParam::MAC:
        {
            auto mac = channelCall<getMACProperty>(channel);
            ret.pack(stdplus::raw::asView<char>(mac));
            return responseSuccess(std::move(ret));
        }
        case LanParam::SubnetMask:
        {
            auto ifaddr = channelCall<getIfAddr4>(channel);
            uint8_t prefix = AddrFamily<AF_INET>::defaultPrefix;
            if (ifaddr)
            {
                prefix = ifaddr->prefix;
            }
            auto netmask = stdplus::pfxToMask<stdplus::In4Addr>(prefix);
            ret.pack(stdplus::raw::asView<char>(netmask));
            return responseSuccess(std::move(ret));
        }
        case LanParam::Gateway1:
        {
            auto gateway = channelCall<getGatewayProperty<AF_INET>>(channel);
            ret.pack(stdplus::raw::asView<char>(
                gateway.value_or(stdplus::In4Addr{})));
            return responseSuccess(std::move(ret));
        }
        case LanParam::Gateway1MAC:
        {
            stdplus::EtherAddr mac{};
            auto neighbor = channelCall<getGatewayNeighbor<AF_INET>>(channel);
            if (neighbor)
            {
                mac = neighbor->mac;
            }
            ret.pack(stdplus::raw::asView<char>(mac));
            return responseSuccess(std::move(ret));
        }
        case LanParam::VLANId:
        {
            uint16_t vlan = channelCall<getVLANProperty>(channel);
            if (vlan != 0)
            {
                vlan |= VLAN_ENABLE_FLAG;
            }
            else
            {
                vlan = lastDisabledVlan[channel];
            }
            ret.pack(vlan);
            return responseSuccess(std::move(ret));
        }
        case LanParam::CiphersuiteSupport:
        {
            if (getChannelSessionSupport(channel) ==
                EChannelSessSupported::none)
            {
                return responseInvalidFieldRequest();
            }
            if (!listInit)
            {
                return responseUnspecifiedError();
            }
            ret.pack(static_cast<uint8_t>(cipherList.size() - 1));
            return responseSuccess(std::move(ret));
        }
        case LanParam::CiphersuiteEntries:
        {
            if (getChannelSessionSupport(channel) ==
                EChannelSessSupported::none)
            {
                return responseInvalidFieldRequest();
            }
            if (!listInit)
            {
                return responseUnspecifiedError();
            }
            ret.pack(cipherList);
            return responseSuccess(std::move(ret));
        }
        case LanParam::IPFamilySupport:
        {
            std::bitset<8> support;
            support[IPFamilySupportFlag::IPv6Only] = 0;
            support[IPFamilySupportFlag::DualStack] = 1;
            support[IPFamilySupportFlag::IPv6Alerts] = 1;
            ret.pack(support);
            return responseSuccess(std::move(ret));
        }
        case LanParam::IPFamilyEnables:
        {
            ret.pack(static_cast<uint8_t>(IPFamilyEnables::DualStack));
            return responseSuccess(std::move(ret));
        }
        case LanParam::IPv6Status:
        {
            ret.pack(MAX_IPV6_STATIC_ADDRESSES);
            ret.pack(MAX_IPV6_DYNAMIC_ADDRESSES);
            std::bitset<8> support;
            support[IPv6StatusFlag::DHCP] = 1;
            support[IPv6StatusFlag::SLAAC] = 1;
            ret.pack(support);
            return responseSuccess(std::move(ret));
        }
        case LanParam::IPv6StaticAddresses:
        {
            if (set >= MAX_IPV6_STATIC_ADDRESSES)
            {
                return responseParmOutOfRange();
            }
            getLanIPv6Address(ret, channel, set, originsV6Static);
            return responseSuccess(std::move(ret));
        }
        case LanParam::IPv6DynamicAddresses:
        {
            if (set >= MAX_IPV6_DYNAMIC_ADDRESSES)
            {
                return responseParmOutOfRange();
            }
            getLanIPv6Address(ret, channel, set, originsV6Dynamic);
            return responseSuccess(std::move(ret));
        }
        case LanParam::IPv6RouterControl:
        {
            std::bitset<8> control;
            control[IPv6RouterControlFlag::Dynamic] =
                channelCall<getEthProp<bool>>(channel, "IPv6AcceptRA");
            control[IPv6RouterControlFlag::Static] = 1;
            ret.pack(control);
            return responseSuccess(std::move(ret));
        }
        case LanParam::IPv6StaticRouter1IP:
        {
            stdplus::In6Addr gateway{};
            if (!channelCall<getEthProp<bool>>(channel, "IPv6AcceptRA"))
            {
                gateway =
                    channelCall<getGatewayProperty<AF_INET6>>(channel).value_or(
                        stdplus::In6Addr{});
            }
            ret.pack(stdplus::raw::asView<char>(gateway));
            return responseSuccess(std::move(ret));
        }
        case LanParam::IPv6StaticRouter1MAC:
        {
            stdplus::EtherAddr mac{};
            auto neighbor = channelCall<getGatewayNeighbor<AF_INET6>>(channel);
            if (neighbor)
            {
                mac = neighbor->mac;
            }
            ret.pack(stdplus::raw::asView<char>(mac));
            return responseSuccess(std::move(ret));
        }
        case LanParam::IPv6StaticRouter1PrefixLength:
        {
            ret.pack(uint8_t{0});
            return responseSuccess(std::move(ret));
        }
        case LanParam::IPv6StaticRouter1PrefixValue:
        {
            ret.pack(stdplus::raw::asView<char>(stdplus::In6Addr{}));
            return responseSuccess(std::move(ret));
        }
        case LanParam::cipherSuitePrivilegeLevels:
        {
            std::array<uint4_t, ipmi::maxCSRecords> csPrivilegeLevels;

            uint8_t resp =
                getCipherConfigObject(csPrivFileName, csPrivDefaultFileName)
                    .getCSPrivilegeLevels(channel, csPrivilegeLevels);
            if (!resp)
            {
                constexpr uint8_t reserved1 = 0x00;
                ret.pack(reserved1, csPrivilegeLevels);
                return responseSuccess(std::move(ret));
            }
            else
            {
                return response(resp);
            }
        }
    }

    if (parameter >= oemCmdStart)
    {
        return getLanOem(channel, parameter, set, block);
    }

    return responseParamNotSupported();
}

constexpr const char* solInterface = "xyz.openbmc_project.Ipmi.SOL";
constexpr const char* solPath = "/xyz/openbmc_project/ipmi/sol/";
constexpr const uint16_t solDefaultPort = 623;

RspType<> setSolConfParams(Context::ptr ctx, uint4_t channelBits,
                           uint4_t /*reserved*/, uint8_t parameter,
                           message::Payload& req)
{
    const uint8_t channel = convertCurrentChannelNum(
        static_cast<uint8_t>(channelBits), ctx->channel);

    if (!isValidChannel(channel))
    {
        lg2::error("Set Sol Config - Invalid channel in request");
        return responseInvalidFieldRequest();
    }

    auto channelName = ipmi::getChannelName(channel);
    if (channelName.empty())
    {
        lg2::error("Channel name does not exist for channel {CHANNEL}",
                   "CHANNEL", channel);
        return responseInvalidFieldRequest();
    }

    std::string solService{};
    std::string solPathWithEthName = solPath + channelName;

    if (ipmi::getService(ctx, solInterface, solPathWithEthName, solService))
    {
        lg2::error("Set Sol Config - Invalid solInterface, service: {SERVICE}, "
                   "object path: {OBJPATH}, interface: {INTERFACE}",
                   "SERVICE", solService, "OBJPATH", solPathWithEthName,
                   "INTERFACE", solInterface);
        return responseInvalidFieldRequest();
    }

    switch (static_cast<SolConfParam>(parameter))
    {
        case SolConfParam::Progress:
        {
            uint8_t progress;
            if (req.unpack(progress) != 0 || !req.fullyUnpacked())
            {
                return responseReqDataLenInvalid();
            }

            if (ipmi::setDbusProperty(ctx, solService, solPathWithEthName,
                                      solInterface, "Progress", progress))
            {
                return responseUnspecifiedError();
            }
            break;
        }
        case SolConfParam::Enable:
        {
            bool enable;
            uint7_t reserved2;

            if (req.unpack(enable, reserved2) != 0 || !req.fullyUnpacked())
            {
                return responseReqDataLenInvalid();
            }

            if (ipmi::setDbusProperty(ctx, solService, solPathWithEthName,
                                      solInterface, "Enable", enable))
            {
                return responseUnspecifiedError();
            }
            break;
        }
        case SolConfParam::Authentication:
        {
            uint4_t privilegeBits{};
            uint2_t reserved2{};
            bool forceAuth = false;
            bool forceEncrypt = false;

            if (req.unpack(privilegeBits, reserved2, forceAuth, forceEncrypt) !=
                    0 ||
                !req.fullyUnpacked())
            {
                return responseReqDataLenInvalid();
            }

            uint8_t privilege = static_cast<uint8_t>(privilegeBits);
            if (privilege < static_cast<uint8_t>(Privilege::User) ||
                privilege > static_cast<uint8_t>(Privilege::Oem))
            {
                return ipmi::responseInvalidFieldRequest();
            }

            if (ipmi::setDbusProperty(ctx, solService, solPathWithEthName,
                                      solInterface, "Privilege", privilege))
            {
                return responseUnspecifiedError();
            }

            if (ipmi::setDbusProperty(ctx, solService, solPathWithEthName,
                                      solInterface, "ForceEncryption",
                                      forceEncrypt))
            {
                return responseUnspecifiedError();
            }

            if (ipmi::setDbusProperty(ctx, solService, solPathWithEthName,
                                      solInterface, "ForceAuthentication",
                                      forceAuth))
            {
                return responseUnspecifiedError();
            }
            break;
        }
        case SolConfParam::Accumulate:
        {
            uint8_t interval;
            uint8_t threshold;
            if (req.unpack(interval, threshold) != 0 || !req.fullyUnpacked())
            {
                return responseReqDataLenInvalid();
            }

            if (threshold == 0)
            {
                return responseInvalidFieldRequest();
            }

            if (ipmi::setDbusProperty(ctx, solService, solPathWithEthName,
                                      solInterface, "AccumulateIntervalMS",
                                      interval))
            {
                return responseUnspecifiedError();
            }

            if (ipmi::setDbusProperty(ctx, solService, solPathWithEthName,
                                      solInterface, "Threshold", threshold))
            {
                return responseUnspecifiedError();
            }
            break;
        }
        case SolConfParam::Retry:
        {
            uint3_t countBits;
            uint5_t reserved2;
            uint8_t interval;

            if (req.unpack(countBits, reserved2, interval) != 0 ||
                !req.fullyUnpacked())
            {
                return responseReqDataLenInvalid();
            }

            uint8_t count = static_cast<uint8_t>(countBits);
            if (ipmi::setDbusProperty(ctx, solService, solPathWithEthName,
                                      solInterface, "RetryCount", count))
            {
                return responseUnspecifiedError();
            }

            if (ipmi::setDbusProperty(ctx, solService, solPathWithEthName,
                                      solInterface, "RetryIntervalMS",
                                      interval))
            {
                return responseUnspecifiedError();
            }
            break;
        }
        case SolConfParam::Port:
        {
            return responseWriteReadParameter();
        }
        case SolConfParam::NonVbitrate:
        case SolConfParam::Vbitrate:
        case SolConfParam::Channel:
        default:
            return responseParamNotSupported();
    }
    return responseSuccess();
}

RspType<message::Payload> getSolConfParams(
    Context::ptr ctx, uint4_t channelBits, uint3_t /*reserved*/, bool revOnly,
    uint8_t parameter, uint8_t /*set*/, uint8_t /*block*/)
{
    message::Payload ret;
    constexpr uint8_t current_revision = 0x11;
    ret.pack(current_revision);
    if (revOnly)
    {
        return responseSuccess(std::move(ret));
    }

    const uint8_t channel = convertCurrentChannelNum(
        static_cast<uint8_t>(channelBits), ctx->channel);

    if (!isValidChannel(channel))
    {
        lg2::error("Get Sol Config - Invalid channel in request");
        return responseInvalidFieldRequest();
    }

    auto channelName = ipmi::getChannelName(channel);
    if (channelName.empty())
    {
        lg2::error("Channel name does not exist for channel {CHANNEL}",
                   "CHANNEL", channel);
        return responseInvalidFieldRequest();
    }

    std::string solService{};
    std::string solPathWithEthName = solPath + channelName;

    if (ipmi::getService(ctx, solInterface, solPathWithEthName, solService))
    {
        lg2::error("Get Sol Config - Invalid solInterface, service: {SERVICE}, "
                   "object path: {OBJPATH}, interface: {INTERFACE}",
                   "SERVICE", solService, "OBJPATH", solPathWithEthName,
                   "INTERFACE", solInterface);
        return responseInvalidFieldRequest();
    }

    switch (static_cast<SolConfParam>(parameter))
    {
        case SolConfParam::Progress:
        {
            uint8_t progress;
            if (ipmi::getDbusProperty(ctx, solService, solPathWithEthName,
                                      solInterface, "Progress", progress))
            {
                return responseUnspecifiedError();
            }
            ret.pack(progress);
            return responseSuccess(std::move(ret));
        }
        case SolConfParam::Enable:
        {
            bool enable{};
            if (ipmi::getDbusProperty(ctx, solService, solPathWithEthName,
                                      solInterface, "Enable", enable))
            {
                return responseUnspecifiedError();
            }
            ret.pack(enable, uint7_t{});
            return responseSuccess(std::move(ret));
        }
        case SolConfParam::Authentication:
        {
            // 4bits, cast when pack
            // 4 位，打包时进行强制转换
            uint8_t privilege;
            bool forceAuth = false;
            bool forceEncrypt = false;

            if (ipmi::getDbusProperty(ctx, solService, solPathWithEthName,
                                      solInterface, "Privilege", privilege))
            {
                return responseUnspecifiedError();
            }

            if (ipmi::getDbusProperty(ctx, solService, solPathWithEthName,
                                      solInterface, "ForceAuthentication",
                                      forceAuth))
            {
                return responseUnspecifiedError();
            }

            if (ipmi::getDbusProperty(ctx, solService, solPathWithEthName,
                                      solInterface, "ForceEncryption",
                                      forceEncrypt))
            {
                return responseUnspecifiedError();
            }
            ret.pack(uint4_t{privilege}, uint2_t{}, forceAuth, forceEncrypt);
            return responseSuccess(std::move(ret));
        }
        case SolConfParam::Accumulate:
        {
            uint8_t interval{}, threshold{};

            if (ipmi::getDbusProperty(ctx, solService, solPathWithEthName,
                                      solInterface, "AccumulateIntervalMS",
                                      interval))
            {
                return responseUnspecifiedError();
            }

            if (ipmi::getDbusProperty(ctx, solService, solPathWithEthName,
                                      solInterface, "Threshold", threshold))
            {
                return responseUnspecifiedError();
            }
            ret.pack(interval, threshold);
            return responseSuccess(std::move(ret));
        }
        case SolConfParam::Retry:
        {
            // 3bits, cast when pack
            // 3 位，打包时进行强制转换
            uint8_t count{};
            uint8_t interval{};

            if (ipmi::getDbusProperty(ctx, solService, solPathWithEthName,
                                      solInterface, "RetryCount", count))
            {
                return responseUnspecifiedError();
            }

            if (ipmi::getDbusProperty(ctx, solService, solPathWithEthName,
                                      solInterface, "RetryIntervalMS",
                                      interval))
            {
                return responseUnspecifiedError();
            }
            ret.pack(uint3_t{count}, uint5_t{}, interval);
            return responseSuccess(std::move(ret));
        }
        case SolConfParam::Port:
        {
            auto port = solDefaultPort;
            ret.pack(static_cast<uint16_t>(port));
            return responseSuccess(std::move(ret));
        }
        case SolConfParam::Channel:
        {
            ret.pack(channel);
            return responseSuccess(std::move(ret));
        }
        case SolConfParam::NonVbitrate:
        {
            uint64_t baudRate;
            uint8_t encodedBitRate = 0;
            if (ipmi::getDbusProperty(
                    ctx, "xyz.openbmc_project.Console.default",
                    "/xyz/openbmc_project/console/default",
                    "xyz.openbmc_project.Console.UART", "Baud", baudRate))
            {
                return responseParamNotSupported();
            }
            switch (baudRate)
            {
                case 9600:
                    encodedBitRate = 0x06;
                    break;
                case 19200:
                    encodedBitRate = 0x07;
                    break;
                case 38400:
                    encodedBitRate = 0x08;
                    break;
                case 57600:
                    encodedBitRate = 0x09;
                    break;
                case 115200:
                    encodedBitRate = 0x0a;
                    break;
                default:
                    break;
            }
            ret.pack(encodedBitRate);
            return responseSuccess(std::move(ret));
        }
        case SolConfParam::Vbitrate:
        default:
            return responseParamNotSupported();
    }

    return responseParamNotSupported();
}

} // namespace transport
} // namespace ipmi

void registerNetFnTransportFunctions() __attribute__((constructor));

void registerNetFnTransportFunctions()
{
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnTransport,
                          ipmi::transport::cmdSetLanConfigParameters,
                          ipmi::Privilege::Admin, ipmi::transport::setLan);
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnTransport,
                          ipmi::transport::cmdGetLanConfigParameters,
                          ipmi::Privilege::Operator, ipmi::transport::getLan);
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnTransport,
                          ipmi::transport::cmdSetSolConfigParameters,
                          ipmi::Privilege::Admin,
                          ipmi::transport::setSolConfParams);
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnTransport,
                          ipmi::transport::cmdGetSolConfigParameters,
                          ipmi::Privilege::User,
                          ipmi::transport::getSolConfParams);
}
