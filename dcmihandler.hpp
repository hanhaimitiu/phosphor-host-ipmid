#pragma once

/**
 * @file dcmihandler.hpp
 * @brief DCMI 扩展命令的声明。
 */
/* @file dcmihandler.hpp
 * @brief DCMI 扩展命令的声明。 */

#include "nlohmann/json.hpp"

#include <sdbusplus/bus.hpp>

#include <map>
#include <string>
#include <vector>

namespace dcmi
{

static constexpr auto propIntf = "org.freedesktop.DBus.Properties";
static constexpr auto assetTagIntf =
    "xyz.openbmc_project.Inventory.Decorator.AssetTag";
static constexpr auto assetTagProp = "AssetTag";
static constexpr auto networkServiceName = "xyz.openbmc_project.Network";
static constexpr auto networkConfigObj = "/xyz/openbmc_project/network/config";
static constexpr auto networkConfigIntf =
    "xyz.openbmc_project.Network.SystemConfiguration";
static constexpr auto hostNameProp = "HostName";
static constexpr auto temperatureSensorType = 0x01;
static constexpr size_t maxInstances = 255;
static constexpr uint8_t maxRecords = 8;
static constexpr auto gDCMISensorsConfig =
    "/usr/share/ipmi-providers/dcmi_sensors.json";
static constexpr auto ethernetIntf =
    "xyz.openbmc_project.Network.EthernetInterface";
static constexpr auto ethernetDefaultChannelNum = 0x1;
static constexpr auto networkRoot = "/xyz/openbmc_project/network";
static constexpr auto dhcpIntf =
    "xyz.openbmc_project.Network.DHCPConfiguration";
static constexpr auto systemBusName = "org.freedesktop.systemd1";
static constexpr auto systemPath = "/org/freedesktop/systemd1";
static constexpr auto systemIntf = "org.freedesktop.systemd1.Manager";
static constexpr auto gDCMICapabilitiesConfig =
    "/usr/share/ipmi-providers/dcmi_cap.json";
static constexpr auto gDCMIPowerMgmtCapability = "PowerManagement";
static constexpr auto gDCMIPowerMgmtSupported = 0x1;
static constexpr auto gMaxSELEntriesMask = 0xFFF;
static constexpr auto gByteBitSize = 8;

/** @brief Check whether DCMI power management is supported
 *         in the DCMI Capabilities config file.
 *
 *  @return True if DCMI power management is supported
 */
/* @brief 检查 DCMI Capabilities 配置文件中是否支持
 *         DCMI 电源管理。
 *  @return 若支持 DCMI 电源管理则返回 True */
bool isDCMIPowerMgmtSupported();

} // namespace dcmi
