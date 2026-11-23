
/**
 * @file dcmihandler.cpp
 * @brief DCMI（数据中心管理接口）扩展命令的实现。
 */
/* @file dcmihandler.cpp
 * @brief DCMI（数据中心管理接口）扩展命令的实现。 */
#include "config.h"

#include "dcmihandler.hpp"

#include "user_channel/channel_layer.hpp"

#include <ipmid/api.hpp>
#include <ipmid/utils.hpp>
#include <nlohmann/json.hpp>
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus.hpp>
#include <xyz/openbmc_project/Common/error.hpp>
#include <xyz/openbmc_project/Network/EthernetInterface/server.hpp>
#include <xyz/openbmc_project/Sensor/Value/common.hpp>

#include <bitset>
#include <cmath>
#include <fstream>
#include <variant>

using namespace phosphor::logging;
using sdbusplus::server::xyz::openbmc_project::network::EthernetInterface;

using InternalFailure =
    sdbusplus::error::xyz::openbmc_project::common::InternalFailure;

using SensorValue = sdbusplus::common::xyz::openbmc_project::sensor::Value;

void registerNetFnDcmiFunctions() __attribute__((constructor));

constexpr auto pcapInterface = "xyz.openbmc_project.Control.Power.Cap";

constexpr auto powerCapProp = "PowerCap";
constexpr auto powerCapEnableProp = "PowerCapEnable";
constexpr auto powerCapExceptActProp = "ExceptionAction";
constexpr auto powerCapSamplePeriodProp = "SamplingPeriod";
constexpr auto powerCapCorrectTimeProp = "CorrectionTime";

using namespace phosphor::logging;

namespace dcmi
{
constexpr auto assetTagMaxOffset = 62;
constexpr auto assetTagMaxSize = 63;
constexpr auto maxBytes = 16;
constexpr size_t maxCtrlIdStrLen = 64;

constexpr uint8_t parameterRevision = 2;
constexpr uint8_t specMajorVersion = 1;
constexpr uint8_t specMinorVersion = 5;
constexpr uint8_t configParameterRevision = 1;
constexpr auto option12Mask = 0x01;
constexpr auto activateDhcpReply = 0x00;
constexpr uint8_t activateDhcpValue = 0x01;
constexpr uint8_t dhcpTiming1 = 0x04;  // 4 sec
constexpr uint16_t dhcpTiming2 = 0x78; // 120 sec
constexpr uint16_t dhcpTiming3 = 0x40; // 60 sec
// When DHCP Option 12 is enabled the string "SendHostName=true" will be
// added into n/w configuration file and the parameter
// SendHostNameEnabled will set to true.
// 当启用 DHCP Option 12 时，字符串 "SendHostName=true" 将被
// 添加到网络配置文件中，并且参数
// SendHostNameEnabled 将被设为 true。
constexpr auto dhcpOpt12Enabled = "SendHostNameEnabled";

enum class DCMIConfigParameters : uint8_t
{
    ActivateDHCP = 1,
    DiscoveryConfig,
    DHCPTiming1,
    DHCPTiming2,
    DHCPTiming3,
};

/*
 * The list of Exception action options. Please refer to table 6-17 of DCMI
 * specification version 1.5 for more information.
 * https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/dcmi-v1-5-rev-spec.pdf
 */
/* 异常动作选项列表。更多信息请参阅 DCMI
 * 规范 1.5 版的表 6-17。
 * https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/dcmi-v1-5-rev-spec.pdf */
enum class ExceptActOptions : uint8_t
{
    NoAction = 0x00,
    HardPowerOff,
    Oem02,
    Oem03,
    Oem04,
    Oem05,
    Oem06,
    Oem07,
    Oem08,
    Oem09,
    Oem0A,
    Oem0B,
    Oem0C,
    Oem0D,
    Oem0E,
    Oem0F,
    Oem10,
    LogEventOnly,
};

/*
 * The PDIs only supports NoAction/HardPowerOff/LogEventOnly/Oem options for
 * exception action.
 */
/* PDI 仅支持 NoAction/HardPowerOff/LogEventOnly/Oem
 * 作为异常动作选项。 */
namespace DbusExceptAct
{
constexpr auto noAction =
    "xyz.openbmc_project.Control.Power.Cap.ExceptionActions.NoAction";
constexpr auto hardPowerOff =
    "xyz.openbmc_project.Control.Power.Cap.ExceptionActions.HardPowerOff";
constexpr auto logEventOnly =
    "xyz.openbmc_project.Control.Power.Cap.ExceptionActions.LogEventOnly";
constexpr auto oem =
    "xyz.openbmc_project.Control.Power.Cap.ExceptionActions.Oem";
} // namespace DbusExceptAct

// Refer Table 6-14, DCMI Entity ID Extension, DCMI v1.5 spec
// 请参阅 DCMI v1.5 规范中的表 6-14（DCMI Entity ID Extension）
static const std::map<uint8_t, std::string> entityIdToName{
    {0x40, "inlet"}, {0x37, "inlet"},     {0x41, "cpu"},
    {0x03, "cpu"},   {0x42, "baseboard"}, {0x07, "baseboard"}};

nlohmann::json parseJSONConfig(const std::string& configFile)
{
    std::ifstream jsonFile(configFile);
    if (!jsonFile.is_open())
    {
        lg2::error("Temperature readings JSON file not found");
        elog<InternalFailure>();
    }

    auto data = nlohmann::json::parse(jsonFile, nullptr, false);
    if (data.is_discarded())
    {
        lg2::error("Temperature readings JSON parser failure");
        elog<InternalFailure>();
    }

    return data;
}

bool isDCMIPowerMgmtSupported()
{
    static bool parsed = false;
    static bool supported = false;
    if (!parsed)
    {
        auto data = parseJSONConfig(gDCMICapabilitiesConfig);

        supported = (gDCMIPowerMgmtSupported ==
                     data.value(gDCMIPowerMgmtCapability, 0));
    }
    return supported;
}

std::optional<ipmi::DbusObjectInfo> getPCapObject(ipmi::Context::ptr& ctx)
{
    static std::optional<ipmi::DbusObjectInfo> pcapObject = std::nullopt;

    if (pcapObject != std::nullopt)
    {
        return pcapObject;
    }

    ipmi::DbusObjectInfo objectPath{};
    boost::system::error_code ec =
        ipmi::getDbusObject(ctx, pcapInterface, objectPath);

    if (ec.value())
    {
        return std::nullopt;
    }

    pcapObject = objectPath;

    return pcapObject;
}

std::optional<uint32_t> getPcap(ipmi::Context::ptr& ctx)
{
    std::optional<ipmi::DbusObjectInfo> pcapObject = getPCapObject(ctx);

    if (pcapObject == std::nullopt)
    {
        return std::nullopt;
    }

    uint32_t pcap{};
    boost::system::error_code ec = ipmi::getDbusProperty(
        ctx, pcapObject.value().second.c_str(),
        pcapObject.value().first.c_str(), pcapInterface, powerCapProp, pcap);

    if (ec.value())
    {
        lg2::error("Error in getPcap prop: {ERROR}", "ERROR", ec.message());
        elog<InternalFailure>();
        return std::nullopt;
    }

    return pcap;
}

std::optional<bool> getPcapEnabled(ipmi::Context::ptr& ctx)
{
    std::optional<ipmi::DbusObjectInfo> pcapObject = getPCapObject(ctx);

    if (pcapObject == std::nullopt)
    {
        return std::nullopt;
    }

    bool pcapEnabled{};
    boost::system::error_code ec =
        ipmi::getDbusProperty(ctx, pcapObject.value().second.c_str(),
                              pcapObject.value().first.c_str(), pcapInterface,
                              powerCapEnableProp, pcapEnabled);

    if (ec.value())
    {
        lg2::error("Error in getPcapEnabled prop: {ERROR}", "ERROR",
                   ec.message());
        elog<InternalFailure>();
        return std::nullopt;
    }

    return pcapEnabled;
}

std::optional<std::string> getPcapExceptAction(ipmi::Context::ptr& ctx)
{
    std::optional<ipmi::DbusObjectInfo> pcapObject = getPCapObject(ctx);

    if (pcapObject == std::nullopt)
    {
        return std::nullopt;
    }

    std::string exceptActStr{};

    boost::system::error_code ec =
        ipmi::getDbusProperty(ctx, pcapObject.value().second.c_str(),
                              pcapObject.value().first.c_str(), pcapInterface,
                              powerCapExceptActProp, exceptActStr);

    if (ec.value())
    {
        lg2::error("Error in getPcapExceptAction prop: {ERROR}", "ERROR",
                   ec.message());
        elog<InternalFailure>();
        return std::nullopt;
    }

    return exceptActStr;
}

std::optional<uint32_t> getPcapCorrectTime(ipmi::Context::ptr& ctx)
{
    std::optional<ipmi::DbusObjectInfo> pcapObject = getPCapObject(ctx);

    if (pcapObject == std::nullopt)
    {
        return std::nullopt;
    }

    uint64_t pcapCorrectTimeUs{};
    boost::system::error_code ec =
        ipmi::getDbusProperty(ctx, pcapObject.value().second.c_str(),
                              pcapObject.value().first.c_str(), pcapInterface,
                              powerCapCorrectTimeProp, pcapCorrectTimeUs);
    if (ec.value())
    {
        lg2::error("Error in getPcapCorrectTime prop: {ERROR}", "ERROR",
                   ec.message());
        elog<InternalFailure>();
        return std::nullopt;
    }

    return (uint32_t)(std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::microseconds(pcapCorrectTimeUs)))
        .count();
}

std::optional<uint16_t> getPcapSamplePeriod(ipmi::Context::ptr& ctx)
{
    std::optional<ipmi::DbusObjectInfo> pcapObject = getPCapObject(ctx);

    if (pcapObject == std::nullopt)
    {
        return std::nullopt;
    }

    uint64_t pcapSamplePeriodUs{};
    boost::system::error_code ec =
        ipmi::getDbusProperty(ctx, pcapObject.value().second.c_str(),
                              pcapObject.value().first.c_str(), pcapInterface,
                              powerCapSamplePeriodProp, pcapSamplePeriodUs);
    if (ec.value())
    {
        lg2::error("Error in getPcapSamplePeriod prop: {ERROR}", "ERROR",
                   ec.message());
        elog<InternalFailure>();
        return std::nullopt;
    }

    return (uint16_t)(std::chrono::duration_cast<std::chrono::seconds>(
                          std::chrono::microseconds(pcapSamplePeriodUs)))
        .count();
}

bool setPcap(ipmi::Context::ptr& ctx, const uint32_t powerCap)
{
    std::optional<ipmi::DbusObjectInfo> pcapObject = getPCapObject(ctx);

    if (pcapObject == std::nullopt)
    {
        return false;
    }

    boost::system::error_code ec =
        ipmi::setDbusProperty(ctx, pcapObject.value().second.c_str(),
                              pcapObject.value().first.c_str(), pcapInterface,
                              powerCapProp, powerCap);

    if (ec.value())
    {
        lg2::error("Error in setPcap property: {ERROR}", "ERROR", ec.message());
        elog<InternalFailure>();
        return false;
    }

    return true;
}

bool setPcapEnable(ipmi::Context::ptr& ctx, bool enabled)
{
    std::optional<ipmi::DbusObjectInfo> pcapObject = getPCapObject(ctx);

    if (pcapObject == std::nullopt)
    {
        return false;
    }

    boost::system::error_code ec =
        ipmi::setDbusProperty(ctx, pcapObject.value().second.c_str(),
                              pcapObject.value().first.c_str(), pcapInterface,
                              powerCapEnableProp, enabled);

    if (ec.value())
    {
        lg2::error("Error in setPcapEnabled property: {ERROR}", "ERROR",
                   ec.message());
        elog<InternalFailure>();
        return false;
    }
    return true;
}

bool setPcapExceptAction(ipmi::Context::ptr& ctx,
                         const std::string& pcapExceptAct)
{
    std::optional<ipmi::DbusObjectInfo> pcapObject = getPCapObject(ctx);

    if (pcapObject == std::nullopt)
    {
        return false;
    }

    boost::system::error_code ec =
        ipmi::setDbusProperty(ctx, pcapObject.value().second.c_str(),
                              pcapObject.value().first.c_str(), pcapInterface,
                              powerCapExceptActProp, pcapExceptAct);
    if (ec.value())
    {
        lg2::error("Error in setPcapExceptAction property: {ERROR}", "ERROR",
                   ec.message());
        elog<InternalFailure>();
        return false;
    }

    return true;
}

bool setPcapCorrectTime(ipmi::Context::ptr& ctx, uint32_t pcapCorrectTime)
{
    std::optional<ipmi::DbusObjectInfo> pcapObject = getPCapObject(ctx);

    if (pcapObject == std::nullopt)
    {
        return false;
    }

    /*
     * Dbus is storing Correction time in microseconds unit.
     * Therefore, we have to convert it from millisecond to microseconds.
     */
    /* Dbus 以微秒为单位存储 Correction time。
     * 因此，需要将其从毫秒转换为微秒。 */
    uint64_t pcapCorrectTimeUs =
        (uint64_t)(std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::milliseconds(pcapCorrectTime)))
            .count();
    boost::system::error_code ec =
        ipmi::setDbusProperty(ctx, pcapObject.value().second.c_str(),
                              pcapObject.value().first.c_str(), pcapInterface,
                              powerCapCorrectTimeProp, pcapCorrectTimeUs);
    if (ec.value())
    {
        lg2::error("Error in setPcapCorrectTime property: {ERROR}", "ERROR",
                   ec.message());
        elog<InternalFailure>();
        return false;
    }

    return true;
}

bool setPcapSamplePeriod(ipmi::Context::ptr& ctx, uint16_t pcapSamplePeriod)
{
    std::optional<ipmi::DbusObjectInfo> pcapObject = getPCapObject(ctx);

    if (pcapObject == std::nullopt)
    {
        return false;
    }

    /*
     * Dbus is storing Sampling period in microseconds unit.
     * Therefore, we have to convert it from seconds to microseconds unit.
     */
    /* Dbus 以微秒为单位存储 Sampling period。
     * 因此，需要将其从秒转换为微秒。 */
    uint64_t pcapSamplePeriodUs =
        (uint64_t)(std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::seconds(pcapSamplePeriod)))
            .count();
    boost::system::error_code ec =
        ipmi::setDbusProperty(ctx, pcapObject.value().second.c_str(),
                              pcapObject.value().first.c_str(), pcapInterface,
                              powerCapSamplePeriodProp, pcapSamplePeriodUs);
    if (ec.value())
    {
        lg2::error("Error in setPcapSamplePeriod property: {ERROR}", "ERROR",
                   ec.message());
        elog<InternalFailure>();
        return false;
    }

    return true;
}

std::optional<std::string> readAssetTag(ipmi::Context::ptr& ctx)
{
    // Read the object tree with the inventory root to figure out the object
    // that has implemented the Asset tag interface.
    // 以 inventory root 读取对象树，找出
    // 实现了 Asset tag 接口的对象。
    ipmi::DbusObjectInfo objectInfo;
    boost::system::error_code ec = getDbusObject(
        ctx, dcmi::assetTagIntf, ipmi::sensor::inventoryRoot, "", objectInfo);
    if (ec.value())
    {
        return std::nullopt;
    }

    std::string assetTag{};
    ec =
        ipmi::getDbusProperty(ctx, objectInfo.second, objectInfo.first,
                              dcmi::assetTagIntf, dcmi::assetTagProp, assetTag);
    if (ec.value())
    {
        lg2::error("Error in reading asset tag: {ERROR}", "ERROR",
                   ec.message());
        elog<InternalFailure>();
        return std::nullopt;
    }

    return assetTag;
}

bool writeAssetTag(ipmi::Context::ptr& ctx, const std::string& assetTag)
{
    // Read the object tree with the inventory root to figure out the object
    // that has implemented the Asset tag interface.
    // 以 inventory root 读取对象树，找出
    // 实现了 Asset tag 接口的对象。
    ipmi::DbusObjectInfo objectInfo;
    boost::system::error_code ec = getDbusObject(
        ctx, dcmi::assetTagIntf, ipmi::sensor::inventoryRoot, "", objectInfo);
    if (ec.value())
    {
        return false;
    }

    ec =
        ipmi::setDbusProperty(ctx, objectInfo.second, objectInfo.first,
                              dcmi::assetTagIntf, dcmi::assetTagProp, assetTag);
    if (ec.value())
    {
        lg2::error("Error in writing asset tag: {ERROR}", "ERROR",
                   ec.message());
        elog<InternalFailure>();
        return false;
    }
    return true;
}

std::optional<std::string> getHostName(ipmi::Context::ptr& ctx)
{
    std::string service{};
    boost::system::error_code ec =
        ipmi::getService(ctx, networkConfigIntf, networkConfigObj, service);
    if (ec.value())
    {
        return std::nullopt;
    }
    std::string hostname{};
    ec = ipmi::getDbusProperty(ctx, service, networkConfigObj,
                               networkConfigIntf, hostNameProp, hostname);
    if (ec.value())
    {
        lg2::error("Error fetching hostname");
        elog<InternalFailure>();
        return std::nullopt;
    }
    return hostname;
}

std::optional<EthernetInterface::DHCPConf> getDHCPEnabled(
    ipmi::Context::ptr& ctx)
{
    auto ethdevice = ipmi::getChannelName(ethernetDefaultChannelNum);
    if (ethdevice.empty())
    {
        lg2::error("Channel name does not exist for channel {CHANNEL}",
                   "CHANNEL", ethernetDefaultChannelNum);
        return std::nullopt;
    }
    ipmi::DbusObjectInfo ethernetObj{};
    boost::system::error_code ec = ipmi::getDbusObject(
        ctx, ethernetIntf, networkRoot, ethdevice, ethernetObj);
    if (ec.value())
    {
        return std::nullopt;
    }
    std::string service{};
    ec = ipmi::getService(ctx, ethernetIntf, ethernetObj.first, service);
    if (ec.value())
    {
        return std::nullopt;
    }
    std::string dhcpVal{};
    ec = ipmi::getDbusProperty(ctx, service, ethernetObj.first, ethernetIntf,
                               "DHCPEnabled", dhcpVal);
    if (ec.value())
    {
        return std::nullopt;
    }

    return EthernetInterface::convertDHCPConfFromString(dhcpVal);
}

std::optional<bool> getDHCPOption(ipmi::Context::ptr& ctx,
                                  const std::string& prop)
{
    ipmi::ObjectTree objectTree;
    if (ipmi::getAllDbusObjects(ctx, networkRoot, dhcpIntf, objectTree))
    {
        return std::nullopt;
    }

    for (const auto& [path, serviceMap] : objectTree)
    {
        for (const auto& [service, object] : serviceMap)
        {
            bool value{};
            if (ipmi::getDbusProperty(ctx, service, path, dhcpIntf, prop,
                                      value))
            {
                return std::nullopt;
            }

            if (value)
            {
                return true;
            }
        }
    }

    return false;
}

bool setDHCPOption(ipmi::Context::ptr& ctx, std::string prop, bool value)
{
    ipmi::ObjectTree objectTree;
    if (ipmi::getAllDbusObjects(ctx, networkRoot, dhcpIntf, objectTree))
    {
        return false;
    }

    for (const auto& [path, serviceMap] : objectTree)
    {
        for (const auto& [service, object] : serviceMap)
        {
            if (ipmi::setDbusProperty(ctx, service, path, dhcpIntf, prop,
                                      value))
            {
                return false;
            }
        }
    }

    return true;
}

} // namespace dcmi

ipmi::RspType<uint16_t, // reserved
              uint8_t,  // exception actions
              uint16_t, // power limit requested in watts
              uint32_t, // correction time in milliseconds
              uint16_t, // reserved
              uint16_t  // statistics sampling period in seconds
              >
    getPowerLimit(ipmi::Context::ptr ctx, uint16_t reserved)
{
    if (!dcmi::isDCMIPowerMgmtSupported())
    {
        return ipmi::responseInvalidCommand();
    }
    if (reserved)
    {
        return ipmi::responseInvalidFieldRequest();
    }

    std::optional<uint16_t> pcapValue = dcmi::getPcap(ctx);
    std::optional<bool> pcapEnable = dcmi::getPcapEnabled(ctx);
    std::optional<uint32_t> pcapCorrectTime = dcmi::getPcapCorrectTime(ctx);
    std::optional<uint16_t> pcapSamplePeriod = dcmi::getPcapSamplePeriod(ctx);
    std::optional<std::string> pcapExceptAct = dcmi::getPcapExceptAction(ctx);

    if (!pcapValue || !pcapEnable || !pcapCorrectTime || !pcapSamplePeriod ||
        !pcapExceptAct)
    {
        return ipmi::responseUnspecifiedError();
    }

    constexpr uint16_t reserved1{};
    constexpr uint16_t reserved2{};
    uint8_t exception;

    std::string exceptAct = pcapExceptAct.value();

    if (exceptAct == dcmi::DbusExceptAct::noAction)
    {
        exception = static_cast<uint8_t>(dcmi::ExceptActOptions::NoAction);
    }
    else if (exceptAct == dcmi::DbusExceptAct::hardPowerOff)
    {
        exception = static_cast<uint8_t>(dcmi::ExceptActOptions::HardPowerOff);
    }
    else if (exceptAct == dcmi::DbusExceptAct::logEventOnly)
    {
        exception = static_cast<uint8_t>(dcmi::ExceptActOptions::LogEventOnly);
    }
    else if (exceptAct == dcmi::DbusExceptAct::oem)
    {
        exception = static_cast<uint8_t>(dcmi::ExceptActOptions::Oem02);
    }
    else
    {
        return ipmi::responseUnspecifiedError();
    }

    if (!pcapEnable.value())
    {
        constexpr ipmi::Cc responseNoPowerLimitSet = 0x80;
        return ipmi::response(responseNoPowerLimitSet, reserved1, exception,
                              pcapValue.value(), pcapCorrectTime.value(),
                              reserved2, pcapSamplePeriod.value());
    }

    return ipmi::responseSuccess(reserved1, exception, pcapValue.value(),
                                 pcapCorrectTime.value(), reserved2,
                                 pcapSamplePeriod.value());
}

ipmi::RspType<> setPowerLimit(ipmi::Context::ptr& ctx, uint16_t reserved1,
                              uint8_t reserved2, uint8_t exceptionAction,
                              uint16_t powerLimit, uint32_t correctionTime,
                              uint16_t reserved3, uint16_t statsPeriod)
{
    if (!dcmi::isDCMIPowerMgmtSupported())
    {
        lg2::error("DCMI Power management is unsupported!");
        return ipmi::responseInvalidCommand();
    }

    if (reserved1 || reserved2 || reserved3)
    {
        return ipmi::responseInvalidFieldRequest();
    }

    if (!dcmi::setPcap(ctx, powerLimit))
    {
        return ipmi::responseUnspecifiedError();
    }

    /*
     * As defined in table 6-18 of DCMI specification version 1.5.
     * The Exception action value is mapped as below
     *  00h - No Action
     *  01h - Hard Power Off system and log events to SEL
     *  02h - 10h OEM defined actions
     *  11h - Log event to SEL only
     *  12h-FFh Reserved
     */
    /* 如 DCMI 规范 1.5 版表 6-18 所定义。
     * 异常动作值映射如下：
     *  00h - 无动作
     *  01h - 硬断电关机并向 SEL 记录事件
     *  02h - 10h OEM 自定义动作
     *  11h - 仅向 SEL 记录事件
     *  12h-FFh 保留 */
    if (exceptionAction >= 0x12)
    {
        return ipmi::responseUnspecifiedError();
    }

    std::string exceptActStr;

    switch (static_cast<dcmi::ExceptActOptions>(exceptionAction))
    {
        case dcmi::ExceptActOptions::NoAction:
        {
            exceptActStr = dcmi::DbusExceptAct::noAction;
            break;
        }
        case dcmi::ExceptActOptions::HardPowerOff:
        {
            exceptActStr = dcmi::DbusExceptAct::hardPowerOff;
            break;
        }
        case dcmi::ExceptActOptions::LogEventOnly:
        {
            exceptActStr = dcmi::DbusExceptAct::logEventOnly;
            break;
        }
        default:
        {
            exceptActStr = dcmi::DbusExceptAct::oem;
            break;
        }
    }

    if (!dcmi::setPcapExceptAction(ctx, exceptActStr))
    {
        return ipmi::responseUnspecifiedError();
    }

    if (!dcmi::setPcapCorrectTime(ctx, correctionTime))
    {
        return ipmi::responseUnspecifiedError();
    }

    if (!dcmi::setPcapSamplePeriod(ctx, statsPeriod))
    {
        return ipmi::responseUnspecifiedError();
    }

    return ipmi::responseSuccess();
}

ipmi::RspType<> applyPowerLimit(ipmi::Context::ptr& ctx, bool enabled,
                                uint7_t reserved1, uint16_t reserved2)
{
    if (!dcmi::isDCMIPowerMgmtSupported())
    {
        lg2::error("DCMI Power management is unsupported!");
        return ipmi::responseInvalidCommand();
    }
    if (reserved1 || reserved2)
    {
        return ipmi::responseInvalidFieldRequest();
    }

    if (!dcmi::setPcapEnable(ctx, enabled))
    {
        return ipmi::responseUnspecifiedError();
    }

    lg2::info("Set Power Cap Enable: {POWERCAPENABLE}", "POWERCAPENABLE",
              enabled);

    return ipmi::responseSuccess();
}

ipmi::RspType<uint8_t,          // total tag length
              std::vector<char> // tag data
              >
    getAssetTag(ipmi::Context::ptr& ctx, uint8_t offset, uint8_t count)
{
    // Verify offset to read and number of bytes to read are not exceeding
    // the range.
    // 校验读取偏移和读取字节数未超出
    // 范围。
    if ((offset > dcmi::assetTagMaxOffset) || (count > dcmi::maxBytes) ||
        ((offset + count) > dcmi::assetTagMaxSize))
    {
        return ipmi::responseParmOutOfRange();
    }

    std::optional<std::string> assetTagResp = dcmi::readAssetTag(ctx);
    if (!assetTagResp)
    {
        return ipmi::responseUnspecifiedError();
    }

    std::string& assetTag = assetTagResp.value();
    // If the asset tag is longer than 63 bytes, restrict it to 63 bytes to
    // suit Get Asset Tag command.
    // 如果 asset tag 超过 63 字节，则将其限制为 63 字节，
    // 以适配 Get Asset Tag 命令。
    if (assetTag.size() > dcmi::assetTagMaxSize)
    {
        assetTag.resize(dcmi::assetTagMaxSize);
    }

    if (offset > 0 && offset >= assetTag.size())
    {
        return ipmi::responseParmOutOfRange();
    }

    // silently truncate reads beyond the end of assetTag
    // 静默截断超出 assetTag 末尾的读取
    if ((offset + count) >= assetTag.size())
    {
        count = assetTag.size() - offset;
    }

    auto totalTagSize = static_cast<uint8_t>(assetTag.size());
    std::vector<char> data{assetTag.begin() + offset,
                           assetTag.begin() + offset + count};

    return ipmi::responseSuccess(totalTagSize, data);
}

ipmi::RspType<uint8_t // new asset tag length
              >
    setAssetTag(ipmi::Context::ptr& ctx, uint8_t offset, uint8_t count,
                const std::vector<char>& data)
{
    // Verify offset to read and number of bytes to read are not exceeding
    // the range.
    // 校验读取偏移和读取字节数未超出
    // 范围。
    if ((offset > dcmi::assetTagMaxOffset) || (count > dcmi::maxBytes) ||
        ((offset + count) > dcmi::assetTagMaxSize))
    {
        return ipmi::responseParmOutOfRange();
    }
    if (data.size() != count)
    {
        return ipmi::responseReqDataLenInvalid();
    }

    std::optional<std::string> assetTagResp = dcmi::readAssetTag(ctx);
    if (!assetTagResp)
    {
        return ipmi::responseUnspecifiedError();
    }

    std::string& assetTag = assetTagResp.value();

    if (offset > assetTag.size())
    {
        return ipmi::responseParmOutOfRange();
    }

    // operation is to truncate at offset and append new data
    // 该操作是在 offset 处截断并追加新数据
    assetTag.resize(offset);
    assetTag.append(data.begin(), data.end());

    if (!dcmi::writeAssetTag(ctx, assetTag))
    {
        return ipmi::responseUnspecifiedError();
    }

    auto totalTagSize = static_cast<uint8_t>(assetTag.size());
    return ipmi::responseSuccess(totalTagSize);
}

ipmi::RspType<uint8_t,          // length
              std::vector<char> // data
              >
    getMgmntCtrlIdStr(ipmi::Context::ptr& ctx, uint8_t offset, uint8_t count)
{
    if (count > dcmi::maxBytes || offset + count > dcmi::maxCtrlIdStrLen)
    {
        return ipmi::responseParmOutOfRange();
    }

    std::optional<std::string> hostnameResp = dcmi::getHostName(ctx);
    if (!hostnameResp)
    {
        return ipmi::responseUnspecifiedError();
    }

    std::string& hostname = hostnameResp.value();
    // If the id string is longer than 63 bytes, restrict it to 63 bytes to
    // suit set management ctrl str  command.
    // 如果 id 字符串超过 63 字节，则将其限制为 63 字节，
    // 以适配 set management ctrl str 命令。
    if (hostname.size() > dcmi::maxCtrlIdStrLen)
    {
        hostname.resize(dcmi::maxCtrlIdStrLen);
    }

    if (offset >= hostname.size())
    {
        return ipmi::responseParmOutOfRange();
    }

    // silently truncate reads beyond the end of hostname
    // 静默截断超出 hostname 末尾的读取
    if ((offset + count) >= hostname.size())
    {
        count = hostname.size() - offset;
    }

    auto nameSize = static_cast<uint8_t>(hostname.size());
    std::vector<char> data{hostname.begin() + offset,
                           hostname.begin() + offset + count};

    return ipmi::responseSuccess(nameSize, data);
}

ipmi::RspType<uint8_t> setMgmntCtrlIdStr(ipmi::Context::ptr& ctx,
                                         uint8_t offset, uint8_t count,
                                         std::vector<char> data)
{
    if ((offset > dcmi::maxCtrlIdStrLen) || (count > dcmi::maxBytes) ||
        ((offset + count) > dcmi::maxCtrlIdStrLen))
    {
        return ipmi::responseParmOutOfRange();
    }
    if (data.size() != count)
    {
        return ipmi::responseReqDataLenInvalid();
    }
    bool terminalWrite{data.back() == '\0'};
    if (terminalWrite)
    {
        // remove the null termination from the data (no need with std::string)
        // 从数据中去掉 null 终止符（std::string 不需要）
        data.resize(count - 1);
    }
    else if ((offset + count) == dcmi::maxCtrlIdStrLen)
    {
        return ipmi::responseParmOutOfRange();
    }

    static std::string hostname{};
    // read in the current value if not starting at offset 0
    // 若不是从 offset 0 开始，则先读入当前值
    if (hostname.size() == 0 && offset != 0)
    {
        /* read old ctrlIdStr */
        /* 读取旧的 ctrlIdStr */
        std::optional<std::string> hostnameResp = dcmi::getHostName(ctx);
        if (!hostnameResp)
        {
            return ipmi::responseUnspecifiedError();
        }
        hostname = hostnameResp.value();
        hostname.resize(offset);
    }

    // operation is to truncate at offset and append new data
    // 该操作是在 offset 处截断并追加新数据
    hostname.append(data.begin(), data.end());

    // do the update if this is the last write
    // 若是最后一次写入则执行更新
    if (terminalWrite)
    {
        boost::system::error_code ec = ipmi::setDbusProperty(
            ctx, dcmi::networkServiceName, dcmi::networkConfigObj,
            dcmi::networkConfigIntf, dcmi::hostNameProp, hostname);
        hostname.clear();
        if (ec.value())
        {
            return ipmi::responseUnspecifiedError();
        }
    }

    auto totalIdSize = static_cast<uint8_t>(offset + count);
    return ipmi::responseSuccess(totalIdSize);
}

ipmi::RspType<ipmi::message::Payload> getDCMICapabilities(uint8_t parameter)
{
    std::ifstream dcmiCapFile(dcmi::gDCMICapabilitiesConfig);
    if (!dcmiCapFile.is_open())
    {
        lg2::error("DCMI Capabilities file not found");
        return ipmi::responseUnspecifiedError();
    }

    auto data = nlohmann::json::parse(dcmiCapFile, nullptr, false);
    if (data.is_discarded())
    {
        lg2::error("DCMI Capabilities JSON parser failure");
        return ipmi::responseUnspecifiedError();
    }

    constexpr bool reserved1{};
    constexpr uint5_t reserved5{};
    constexpr uint7_t reserved7{};
    constexpr uint8_t reserved8{};
    constexpr uint16_t reserved16{};

    ipmi::message::Payload payload;
    payload.pack(dcmi::specMajorVersion, dcmi::specMinorVersion,
                 dcmi::parameterRevision);

    enum class DCMICapParameters : uint8_t
    {
        SupportedDcmiCaps = 0x01,             // Supported DCMI Capabilities
        MandatoryPlatAttributes = 0x02,       // Mandatory Platform Attributes
        OptionalPlatAttributes = 0x03,        // Optional Platform Attributes
        ManageabilityAccessAttributes = 0x04, // Manageability Access Attributes
    };

    switch (static_cast<DCMICapParameters>(parameter))
    {
        case DCMICapParameters::SupportedDcmiCaps:
        {
            bool powerManagement = data.value("PowerManagement", 0);
            bool oobSecondaryLan = data.value("OOBSecondaryLan", 0);
            bool serialTMode = data.value("SerialTMODE", 0);
            bool inBandSystemInterfaceChannel =
                data.value("InBandSystemInterfaceChannel", 0);
            payload.pack(reserved8, powerManagement, reserved7,
                         inBandSystemInterfaceChannel, serialTMode,
                         oobSecondaryLan, reserved5);
            break;
        }
            // Mandatory Platform Attributes
            // 强制平台属性
        case DCMICapParameters::MandatoryPlatAttributes:
        {
            bool selAutoRollOver = data.value("SELAutoRollOver", 0);
            bool flushEntireSELUponRollOver =
                data.value("FlushEntireSELUponRollOver", 0);
            bool recordLevelSELFlushUponRollOver =
                data.value("RecordLevelSELFlushUponRollOver", 0);
            uint12_t numberOfSELEntries =
                data.value("NumberOfSELEntries", 0xcac);
            uint8_t tempMonitoringSamplingFreq =
                data.value("TempMonitoringSamplingFreq", 0);
            payload.pack(numberOfSELEntries, reserved1,
                         recordLevelSELFlushUponRollOver,
                         flushEntireSELUponRollOver, selAutoRollOver,
                         reserved16, tempMonitoringSamplingFreq);
            break;
        }
        // Optional Platform Attributes
        // 可选平台属性
        case DCMICapParameters::OptionalPlatAttributes:
        {
            uint7_t powerMgmtDeviceTargetAddress =
                data.value("PowerMgmtDeviceSlaveAddress", 0);
            uint4_t bmcChannelNumber = data.value("BMCChannelNumber", 0);
            uint4_t deviceRevision = data.value("DeviceRevision", 0);
            payload.pack(powerMgmtDeviceTargetAddress, reserved1,
                         deviceRevision, bmcChannelNumber);
            break;
        }
        // Manageability Access Attributes
        // 可管理性访问属性
        case DCMICapParameters::ManageabilityAccessAttributes:
        {
            uint8_t mandatoryPrimaryLanOOBSupport =
                data.value("MandatoryPrimaryLanOOBSupport", 0xff);
            uint8_t optionalSecondaryLanOOBSupport =
                data.value("OptionalSecondaryLanOOBSupport", 0xff);
            uint8_t optionalSerialOOBMTMODECapability =
                data.value("OptionalSerialOOBMTMODECapability", 0xff);
            payload.pack(mandatoryPrimaryLanOOBSupport,
                         optionalSecondaryLanOOBSupport,
                         optionalSerialOOBMTMODECapability);
            break;
        }
        default:
        {
            lg2::error("Invalid input parameter");
            return ipmi::responseInvalidFieldRequest();
        }
    }

    return ipmi::responseSuccess(payload);
}

namespace dcmi
{
namespace temp_readings
{

std::tuple<bool, bool, uint8_t> readTemp(ipmi::Context::ptr& ctx,
                                         const std::string& dbusService,
                                         const std::string& dbusPath)
{
    // Read the temperature value from d-bus object. Need some conversion.
    // As per the interface xyz.openbmc_project.Sensor.Value, the
    // temperature is an double and in degrees C. It needs to be scaled by
    // using the formula Value * 10^Scale. The ipmi spec has the temperature
    // as a uint8_t, with a separate single bit for the sign.
    // 从 d-bus 对象读取温度值。需要进行一些转换。
    // 按照 xyz.openbmc_project.Sensor.Value 接口，
    // 温度是摄氏度单位的 double，需按 Value * 10^Scale 公式
    // 进行换算。IPMI 规范中温度为 uint8_t，
    // 符号由单独一个位表示。

    ipmi::PropertyMap result{};
    boost::system::error_code ec = ipmi::getAllDbusProperties(
        ctx, dbusService, dbusPath, SensorValue::interface, result);
    if (ec.value())
    {
        return std::make_tuple(false, false, 0);
    }
    auto temperature =
        std::visit(ipmi::VariantToDoubleVisitor(),
                   result.at(SensorValue::property_names::value));
    double absTemp = std::abs(temperature);

    auto findFactor = result.find("Scale");
    double factor = 0.0;
    if (findFactor != result.end())
    {
        factor = std::visit(ipmi::VariantToDoubleVisitor(), findFactor->second);
    }
    double scale = std::pow(10, factor);

    auto tempDegrees = absTemp * scale;
    // Max absolute temp as per ipmi spec is 127.
    // 按 IPMI 规范，最大绝对温度为 127。
    constexpr auto maxTemp = 127;
    if (tempDegrees > maxTemp)
    {
        tempDegrees = maxTemp;
    }

    return std::make_tuple(true, (temperature < 0),
                           static_cast<uint8_t>(tempDegrees));
}

std::tuple<std::vector<std::tuple<uint7_t, bool, uint8_t>>, uint8_t> read(
    ipmi::Context::ptr& ctx, const std::string& type, uint8_t instance,
    size_t count)
{
    std::vector<std::tuple<uint7_t, bool, uint8_t>> response{};

    auto data = parseJSONConfig(gDCMISensorsConfig);
    static const std::vector<nlohmann::json> empty{};
    std::vector<nlohmann::json> readings = data.value(type, empty);
    for (const auto& j : readings)
    {
        // Max of 8 response data sets
        // 最多 8 组响应数据
        if (response.size() == count)
        {
            break;
        }

        uint8_t instanceNum = j.value("instance", 0);
        // Not in the instance range we're interested in
        // 不在我们关心的实例范围内
        if (instanceNum < instance)
        {
            continue;
        }

        std::string path = j.value("dbus", "");
        std::string service{};
        boost::system::error_code ec =
            ipmi::getService(ctx, SensorValue::interface, path, service);
        if (ec.value())
        {
            // not found on dbus
            // 在 dbus 上未找到
            continue;
        }

        const auto& [ok, sign, temp] = readTemp(ctx, service, path);
        if (ok)
        {
            response.emplace_back(uint7_t{temp}, sign, instanceNum);
        }
    }

    auto totalInstances =
        static_cast<uint8_t>(std::min(readings.size(), maxInstances));
    return std::make_tuple(response, totalInstances);
}

} // namespace temp_readings
} // namespace dcmi

ipmi::RspType<uint8_t,                // total instances for entity id
              uint8_t,                // number of instances in this reply
              std::vector<            // zero or more of the following two bytes
                  std::tuple<uint7_t, // temperature value
                             bool,    // sign bit
                             uint8_t  // entity instance
                             >>>
    getTempReadings(ipmi::Context::ptr& ctx, uint8_t sensorType,
                    uint8_t entityId, uint8_t entityInstance,
                    uint8_t instanceStart)
{
    auto it = dcmi::entityIdToName.find(entityId);
    if (it == dcmi::entityIdToName.end())
    {
        lg2::error("Unknown Entity ID: {ENTITY_ID}", "ENTITY_ID", entityId);
        return ipmi::responseInvalidFieldRequest();
    }

    if (sensorType != dcmi::temperatureSensorType)
    {
        lg2::error("Invalid sensor type: {SENSOR_TYPE}", "SENSOR_TYPE",
                   sensorType);
        return ipmi::responseInvalidFieldRequest();
    }

    uint8_t requestedRecords = (entityInstance == 0) ? dcmi::maxRecords : 1;

    // Read requested instances
    // 读取请求的实例
    const auto& [temps, totalInstances] = dcmi::temp_readings::read(
        ctx, it->second, instanceStart, requestedRecords);

    auto numInstances = static_cast<uint8_t>(temps.size());

    return ipmi::responseSuccess(totalInstances, numInstances, temps);
}

ipmi::RspType<> setDCMIConfParams(ipmi::Context::ptr& ctx, uint8_t parameter,
                                  uint8_t setSelector,
                                  ipmi::message::Payload& payload)
{
    if (setSelector)
    {
        return ipmi::responseInvalidFieldRequest();
    }
    // Take action based on the Parameter Selector
    // 根据 Parameter Selector 采取相应动作
    switch (static_cast<dcmi::DCMIConfigParameters>(parameter))
    {
        case dcmi::DCMIConfigParameters::ActivateDHCP:
        {
            uint8_t activateByte{};
            if (payload.unpack(activateByte) || !payload.fullyUnpacked())
            {
                return ipmi::responseReqDataLenInvalid();
            }
            // Only 0x01 is defined by the DCMI spec; reject any other value
            // DCMI 规范仅定义 0x01；拒绝其他任何值
            if (activateByte != dcmi::activateDhcpValue)
            {
                return ipmi::responseInvalidFieldRequest();
            }
            std::optional<EthernetInterface::DHCPConf> dhcpEnabled =
                dcmi::getDHCPEnabled(ctx);
            if (!dhcpEnabled)
            {
                return ipmi::responseUnspecifiedError();
            }
            if (dhcpEnabled.value() != EthernetInterface::DHCPConf::none)
            {
                // When these conditions are met we have to trigger DHCP
                // protocol restart using the latest parameter settings,
                // but as per n/w manager design, each time when we
                // update n/w parameters, n/w service is restarted. So
                // we no need to take any action in this case.
                // 满足这些条件时，我们本应使用最新的参数设置
                // 触发 DHCP 协议重启，但根据网络管理器的设计，
                // 每次更新网络参数时网络服务都会重启。
                // 因此这种情况下
                // 无需采取任何动作。
            }
            break;
        }
        case dcmi::DCMIConfigParameters::DiscoveryConfig:
        {
            bool option12{};
            bool option60{};
            uint5_t reserved1{};
            bool randBackOff{};
            if (payload.unpack(option12, option60, reserved1, randBackOff) ||
                !payload.fullyUnpacked())
            {
                return ipmi::responseReqDataLenInvalid();
            }
            // Systemd-networkd doesn't support Random Back off
            // Systemd-networkd 不支持 Random Back off（随机退避）
            if (reserved1 || randBackOff)
            {
                return ipmi::responseInvalidFieldRequest();
            }
            if (option60)
            {
                return ipmi::responseCommandNotAvailable();
            }
            dcmi::setDHCPOption(ctx, dcmi::dhcpOpt12Enabled, option12);
            break;
        }
        // Systemd-networkd doesn't allow to configure DHCP timings
        // Systemd-networkd 不允许配置 DHCP 时序
        case dcmi::DCMIConfigParameters::DHCPTiming1:
        case dcmi::DCMIConfigParameters::DHCPTiming2:
        case dcmi::DCMIConfigParameters::DHCPTiming3:
        default:
            return ipmi::responseInvalidFieldRequest();
    }
    return ipmi::responseSuccess();
}

ipmi::RspType<ipmi::message::Payload> getDCMIConfParams(
    ipmi::Context::ptr& ctx, uint8_t parameter, uint8_t setSelector)
{
    if (setSelector)
    {
        return ipmi::responseInvalidFieldRequest();
    }
    ipmi::message::Payload payload;
    payload.pack(dcmi::specMajorVersion, dcmi::specMinorVersion,
                 dcmi::configParameterRevision);

    // Take action based on the Parameter Selector
    // 根据 Parameter Selector 采取相应动作
    switch (static_cast<dcmi::DCMIConfigParameters>(parameter))
    {
        case dcmi::DCMIConfigParameters::ActivateDHCP:
            payload.pack(dcmi::activateDhcpReply);
            break;
        case dcmi::DCMIConfigParameters::DiscoveryConfig:
        {
            uint8_t discovery{};
            std::optional<bool> enabled =
                dcmi::getDHCPOption(ctx, dcmi::dhcpOpt12Enabled);
            if (!enabled.has_value())
            {
                return ipmi::responseUnspecifiedError();
            }
            if (enabled.value())
            {
                discovery = dcmi::option12Mask;
            }
            payload.pack(discovery);
            break;
        }
        // Get below values from Systemd-networkd source code
        // 从 Systemd-networkd 源代码中获取以下值
        case dcmi::DCMIConfigParameters::DHCPTiming1:
            payload.pack(dcmi::dhcpTiming1);
            break;
        case dcmi::DCMIConfigParameters::DHCPTiming2:
            payload.pack(dcmi::dhcpTiming2);
            break;
        case dcmi::DCMIConfigParameters::DHCPTiming3:
            payload.pack(dcmi::dhcpTiming3);
            break;
        default:
            return ipmi::responseInvalidFieldRequest();
    }

    return ipmi::responseSuccess(payload);
}

static std::optional<uint16_t> readPower(ipmi::Context::ptr& ctx)
{
    std::ifstream sensorFile(POWER_READING_SENSOR);
    std::string objectPath;
    if (!sensorFile.is_open())
    {
        lg2::error(
            "Power reading configuration file not found: {POWER_SENSOR_FILE}",
            "POWER_SENSOR_FILE", std::string_view{POWER_READING_SENSOR});
        return std::nullopt;
    }

    auto data = nlohmann::json::parse(sensorFile, nullptr, false);
    if (data.is_discarded())
    {
        lg2::error("Error in parsing configuration file: {POWER_SENSOR_FILE}",
                   "POWER_SENSOR_FILE", std::string_view{POWER_READING_SENSOR});
        return std::nullopt;
    }

    objectPath = data.value("path", "");
    if (objectPath.empty())
    {
        lg2::error(
            "Power sensor D-Bus object path is empty: {POWER_SENSOR_FILE}",
            "POWER_SENSOR_FILE", std::string_view{POWER_READING_SENSOR});
        return std::nullopt;
    }

    // Return default value if failed to read from D-Bus object
    // 若从 D-Bus 对象读取失败则返回默认值
    std::string service{};
    boost::system::error_code ec =
        ipmi::getService(ctx, SensorValue::interface, objectPath, service);
    if (ec.value())
    {
        lg2::error("Failed to fetch service for D-Bus object, "
                   "object path: {OBJECT_PATH}, interface: {INTERFACE}",
                   "OBJECT_PATH", objectPath, "INTERFACE",
                   SensorValue::interface);
        return std::nullopt;
    }

    // Read the sensor value and scale properties
    // 读取传感器值和 scale 属性
    double value{};
    ec = ipmi::getDbusProperty(ctx, service, objectPath, SensorValue::interface,
                               SensorValue::property_names::value, value);
    if (ec.value())
    {
        lg2::error("Failed to read power value from D-Bus object, "
                   "object path: {OBJECT_PATH}, interface: {INTERFACE}",
                   "OBJECT_PATH", objectPath, "INTERFACE",
                   SensorValue::interface);
        return std::nullopt;
    }
    auto power = static_cast<uint16_t>(value);
    return power;
}

ipmi::RspType<uint16_t, // current power
              uint16_t, // minimum power
              uint16_t, // maximum power
              uint16_t, // average power
              uint32_t, // timestamp
              uint32_t, // sample period ms
              uint6_t,  // reserved
              bool,     // power measurement active
              bool      // reserved
              >
    getPowerReading(ipmi::Context::ptr& ctx, uint8_t mode, uint8_t attributes,
                    uint8_t reserved)
{
    if (!dcmi::isDCMIPowerMgmtSupported())
    {
        lg2::error("DCMI Power management is unsupported!");
        return ipmi::responseInvalidCommand();
    }
    if (reserved)
    {
        return ipmi::responseInvalidFieldRequest();
    }

    enum class PowerMode : uint8_t
    {
        SystemPowerStatistics = 1,
        EnhancedSystemPowerStatistics = 2,
    };

    if (static_cast<PowerMode>(mode) != PowerMode::SystemPowerStatistics)
    {
        return ipmi::responseInvalidFieldRequest();
    }
    if (attributes)
    {
        return ipmi::responseInvalidFieldRequest();
    }

    std::optional<uint16_t> powerResp = readPower(ctx);
    if (!powerResp)
    {
        return ipmi::responseUnspecifiedError();
    }
    auto& power = powerResp.value();

    // TODO: openbmc/openbmc#2819
    // Minimum, Maximum, Average power, TimeFrame, TimeStamp,
    // PowerReadingState readings need to be populated
    // after Telemetry changes.
    // TODO: openbmc/openbmc#2819
    // Minimum、Maximum、Average power、TimeFrame、TimeStamp、
    // PowerReadingState 读数需要在
    // Telemetry 改动之后填充。
    constexpr uint32_t samplePeriod = 1;
    constexpr uint6_t reserved1 = 0;
    constexpr bool measurementActive = true;
    constexpr bool reserved2 = false;
    auto timestamp = static_cast<uint32_t>(time(nullptr));
    return ipmi::responseSuccess(power, power, power, power, timestamp,
                                 samplePeriod, reserved1, measurementActive,
                                 reserved2);
}

namespace dcmi
{
namespace sensor_info
{

std::tuple<std::vector<uint16_t>, uint8_t> read(
    const std::string& type, uint8_t instance, const nlohmann::json& config,
    uint8_t count)
{
    std::vector<uint16_t> responses{};

    static const std::vector<nlohmann::json> empty{};
    std::vector<nlohmann::json> readings = config.value(type, empty);
    uint8_t totalInstances = std::min(readings.size(), maxInstances);
    for (const auto& reading : readings)
    {
        // limit to requested count
        // 限制为请求的数量
        if (responses.size() == count)
        {
            break;
        }

        uint8_t instanceNum = reading.value("instance", 0);
        // Not in the instance range we're interested in
        // 不在我们关心的实例范围内
        if (instanceNum < instance)
        {
            continue;
        }

        uint16_t recordId = reading.value("record_id", 0);
        responses.emplace_back(recordId);
    }

    return std::make_tuple(responses, totalInstances);
}

} // namespace sensor_info
} // namespace dcmi

ipmi::RspType<uint8_t,              // total available instances
              uint8_t,              // number of records in this response
              std::vector<uint16_t> // records
              >
    getSensorInfo(uint8_t sensorType, uint8_t entityId, uint8_t entityInstance,
                  uint8_t instanceStart)
{
    auto it = dcmi::entityIdToName.find(entityId);
    if (it == dcmi::entityIdToName.end())
    {
        lg2::error("Unknown Entity ID: {ENTITY_ID}", "ENTITY_ID", entityId);
        return ipmi::responseInvalidFieldRequest();
    }

    if (sensorType != dcmi::temperatureSensorType)
    {
        lg2::error("Invalid sensor type: {SENSOR_TYPE}", "SENSOR_TYPE",
                   sensorType);
        return ipmi::responseInvalidFieldRequest();
    }

    nlohmann::json config = dcmi::parseJSONConfig(dcmi::gDCMISensorsConfig);

    uint8_t requestedRecords = (entityInstance == 0) ? dcmi::maxRecords : 1;
    // Read requested instances
    // 读取请求的实例
    const auto& [sensors, totalInstances] = dcmi::sensor_info::read(
        it->second, instanceStart, config, requestedRecords);
    uint8_t numRecords = sensors.size();

    return ipmi::responseSuccess(totalInstances, numRecords, sensors);
}

void registerNetFnDcmiFunctions()
{
    // <Get Power Limit>
    // <Get Power Limit（获取功率限制）>
    registerGroupHandler(ipmi::prioOpenBmcBase, ipmi::groupDCMI,
                         ipmi::dcmi::cmdGetPowerLimit, ipmi::Privilege::User,
                         getPowerLimit);

    // <Set Power Limit>
    // <Set Power Limit（设置功率限制）>
    registerGroupHandler(ipmi::prioOpenBmcBase, ipmi::groupDCMI,
                         ipmi::dcmi::cmdSetPowerLimit,
                         ipmi::Privilege::Operator, setPowerLimit);

    // <Activate/Deactivate Power Limit>
    // <Activate/Deactivate Power Limit（激活/停用功率限制）>
    registerGroupHandler(ipmi::prioOpenBmcBase, ipmi::groupDCMI,
                         ipmi::dcmi::cmdActDeactivatePwrLimit,
                         ipmi::Privilege::Operator, applyPowerLimit);

    // <Get Asset Tag>
    // <Get Asset Tag（获取资产标签）>
    registerGroupHandler(ipmi::prioOpenBmcBase, ipmi::groupDCMI,
                         ipmi::dcmi::cmdGetAssetTag, ipmi::Privilege::User,
                         getAssetTag);

    // <Set Asset Tag>
    // <Set Asset Tag（设置资产标签）>
    registerGroupHandler(ipmi::prioOpenBmcBase, ipmi::groupDCMI,
                         ipmi::dcmi::cmdSetAssetTag, ipmi::Privilege::Operator,
                         setAssetTag);

    // <Get Management Controller Identifier String>
    // <Get Management Controller Identifier String（获取管理控制器标识字符串）>
    registerGroupHandler(ipmi::prioOpenBmcBase, ipmi::groupDCMI,
                         ipmi::dcmi::cmdGetMgmtCntlrIdString,
                         ipmi::Privilege::User, getMgmntCtrlIdStr);

    // <Set Management Controller Identifier String>
    // <Set Management Controller Identifier String（设置管理控制器标识字符串）>
    registerGroupHandler(ipmi::prioOpenBmcBase, ipmi::groupDCMI,
                         ipmi::dcmi::cmdSetMgmtCntlrIdString,
                         ipmi::Privilege::Admin, setMgmntCtrlIdStr);

    // <Get DCMI capabilities>
    // <Get DCMI capabilities（获取 DCMI 能力）>
    registerGroupHandler(ipmi::prioOpenBmcBase, ipmi::groupDCMI,
                         ipmi::dcmi::cmdGetDcmiCapabilitiesInfo,
                         ipmi::Privilege::User, getDCMICapabilities);

    // <Get Power Reading>
    // <Get Power Reading（获取功率读数）>
    registerGroupHandler(ipmi::prioOpenBmcBase, ipmi::groupDCMI,
                         ipmi::dcmi::cmdGetPowerReading, ipmi::Privilege::User,
                         getPowerReading);

// The Get sensor should get the sensor details dynamically when
// FEATURE_DYNAMIC_SENSORS is enabled.
// 当启用 FEATURE_DYNAMIC_SENSORS 时，Get sensor 应动态
// 获取传感器详情。
#ifndef FEATURE_DYNAMIC_SENSORS
    // <Get Sensor Info>
    // <Get Sensor Info（获取传感器信息）>
    registerGroupHandler(ipmi::prioOpenBmcBase, ipmi::groupDCMI,
                         ipmi::dcmi::cmdGetDcmiSensorInfo,
                         ipmi::Privilege::Operator, getSensorInfo);

    // <Get Temperature Readings>
    // <Get Temperature Readings（获取温度读数）>
    registerGroupHandler(ipmi::prioOpenBmcBase, ipmi::groupDCMI,
                         ipmi::dcmi::cmdGetTemperatureReadings,
                         ipmi::Privilege::User, getTempReadings);
#endif
    // <Get DCMI Configuration Parameters>
    // <Get DCMI Configuration Parameters（获取 DCMI 配置参数）>
    registerGroupHandler(ipmi::prioOpenBmcBase, ipmi::groupDCMI,
                         ipmi::dcmi::cmdGetDcmiConfigParameters,
                         ipmi::Privilege::User, getDCMIConfParams);

    // <Set DCMI Configuration Parameters>
    // <Set DCMI Configuration Parameters（设置 DCMI 配置参数）>
    registerGroupHandler(ipmi::prioOpenBmcBase, ipmi::groupDCMI,
                         ipmi::dcmi::cmdSetDcmiConfigParameters,
                         ipmi::Privilege::Admin, setDCMIConfParams);

    return;
}
