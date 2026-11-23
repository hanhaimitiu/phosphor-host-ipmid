
/**
 * @file whitelist-filter.cpp
 * @brief 命令过滤器：按白名单拦截未开放的 IPMI 命令。
 */
/*  @file whitelist-filter.cpp
 *  @brief 命令过滤器：按白名单拦截未开放的 IPMI 命令。 */
#include <ipmiallowlist.hpp>
#include <ipmid/api.hpp>
#include <ipmid/utils.hpp>
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/lg2.hpp>
#include <settings.hpp>
#include <xyz/openbmc_project/Common/error.hpp>
#include <xyz/openbmc_project/Control/Security/RestrictionMode/server.hpp>

#include <algorithm>
#include <array>

using namespace phosphor::logging;
using namespace sdbusplus::error::xyz::openbmc_project::common;

namespace ipmi
{

// put the filter provider in an unnamed namespace
// 将过滤器提供者（filter provider）放入匿名命名空间
namespace
{

/** @class AllowlistFilter
 *
 * Class that implements an IPMI message filter based
 * on incoming interface and a restriction mode setting
 */
/* @class AllowlistFilter
 *  基于传入接口和限制模式设置来实现 IPMI 消息过滤器的
 *  类。 */
class AllowlistFilter
{
  public:
    AllowlistFilter();
    ~AllowlistFilter() = default;
    AllowlistFilter(const AllowlistFilter&) = delete;
    AllowlistFilter(AllowlistFilter&&) = delete;
    AllowlistFilter& operator=(const AllowlistFilter&) = delete;
    AllowlistFilter& operator=(AllowlistFilter&&) = delete;

  private:
    void postInit();
    void cacheRestrictedMode(
        const std::vector<std::string>& devices,
        const std::map<std::string, std::string>& serviceMaps);
    void handleRestrictedModeChange(
        sdbusplus::message_t& m,
        const std::map<std::string, size_t>& deviceList);
    ipmi::Cc filterMessage(ipmi::message::Request::ptr request);

    std::vector<bool> restrictedMode;
    std::shared_ptr<sdbusplus::asio::connection> bus;
    std::unique_ptr<settings::Objects> objects;
    std::unique_ptr<sdbusplus::match> modeChangeMatch;

    static constexpr const char restrictionModeIntf[] =
        "xyz.openbmc_project.Control.Security.RestrictionMode";
};

AllowlistFilter::AllowlistFilter()
{
    bus = getSdBus();

    lg2::info("Loading allowlist filter");
    ipmi::registerFilter(ipmi::prioOpenBmcBase,
                         [this](ipmi::message::Request::ptr request) {
                             return filterMessage(request);
                         });

    // wait until io->run is going to fetch RestrictionMode
    // 等待 io->run 开始获取 RestrictionMode
    post_work([this]() { postInit(); });
}

/** @brief Get RestrictionMode of the devices which has RestrictionMode support
 * enabled
 *  @param[in] devices - vector of devices object path
 *  @returns void.
 */
/* @brief 获取那些支持并启用了 RestrictionMode 的设备的
 *  RestrictionMode
 *  @param[in] devices - 设备对象路径的 vector
 *  @returns void。 */

void AllowlistFilter::cacheRestrictedMode(
    const std::vector<std::string>& devices,
    const std::map<std::string, std::string>& serviceMaps)
{
    using namespace sdbusplus::server::xyz::openbmc_project::control::security;
    std::string restrictionModeSetting;
    std::string restrictionModeService;

    for (auto& dev : devices)
    {
        try
        {
            restrictionModeSetting = dev;
            restrictionModeService = serviceMaps.at(dev);
        }
        catch (const std::out_of_range& e)
        {
            lg2::error(
                "Could not look up restriction mode interface from cache");
            return;
        }

        std::string mode;
        try
        {
            auto propValue = ipmi::getDbusProperty(
                *bus, restrictionModeService, restrictionModeSetting,
                restrictionModeIntf, "RestrictionMode");
            mode = std::get<std::string>(propValue);
        }
        catch (const std::exception& e)
        {
            lg2::error("Error in RestrictionMode Get");
            // Fail-safe to true.
            // 故障安全（Fail-safe）设为 true。
            size_t index = std::distance(&*std::begin(devices), &dev);
            restrictedMode[index] = true;
        }

        auto restrictionMode = RestrictionMode::convertModesFromString(mode);

        bool restrictMode =
            (restrictionMode == RestrictionMode::Modes::Allowlist);
        restrictedMode.emplace_back(restrictMode);

        lg2::info("Set restrictedMode = {RESTRICTED_MODE}", "RESTRICTED_MODE",
                  restrictMode);
    }
}

/** @brief Update RestrictionMode if any changes in RestrictionMode
 *  @param[in] m - sdbusplus message. Using this to get Updated Mode dbus path
 *  @param[in] deviceList - map to store devices path and their index
 *  @returns void.
 */
/* @brief 当 RestrictionMode 发生变化时更新它
 *  @param[in] m - sdbusplus 消息，用于获取更新后模式的 dbus 路径
 *  @param[in] deviceList - 用于存储设备路径及其索引的 map
 *  @returns void。 */

void AllowlistFilter::handleRestrictedModeChange(
    sdbusplus::message_t& m, const std::map<std::string, size_t>& deviceList)
{
    using namespace sdbusplus::server::xyz::openbmc_project::control::security;
    std::string intf;
    std::vector<std::pair<std::string, ipmi::Value>> propertyList;
    m.read(intf, propertyList);

    std::string path = m.get_path();
    size_t hostId = 0;
    auto it = deviceList.find(path);

    if (it == deviceList.end())
    {
        lg2::error("Key not found in deviceList ");
    }
    else
    {
        hostId = it->second;
    }

    for (const auto& property : propertyList)
    {
        if (property.first == "RestrictionMode")
        {
            RestrictionMode::Modes restrictionMode =
                RestrictionMode::convertModesFromString(
                    std::get<std::string>(property.second));
            bool restrictMode =
                (restrictionMode == RestrictionMode::Modes::Allowlist);
            restrictedMode[hostId] = restrictMode;

            lg2::info("Updated restrictedMode = {RESTRICTED_MODE}",
                      "RESTRICTED_MODE", restrictMode);
        }
    }
}

/** @brief Get and Update RestrictionModes of supported devices
 *  @param[in] void
 *  @returns void.
 */
/* @brief 获取并更新受支持设备的 RestrictionMode
 *  @param[in] void
 *  @returns void。 */

void AllowlistFilter::postInit()
{
    objects = std::make_unique<settings::Objects>(
        *bus, std::vector<settings::Interface>({restrictionModeIntf}));
    if (!objects)
    {
        lg2::error(
            "Failed to create settings object; defaulting to restricted mode");
        return;
    }

    std::vector<std::string> devices;
    try
    {
        devices = objects->map.at(restrictionModeIntf);
    }
    catch (const std::out_of_range& e)
    {
        lg2::error("Could not look up restriction mode interface from cache");
        return;
    }

    // Initialize restricted mode
    // 初始化受限模式（restricted mode）
    cacheRestrictedMode(devices, objects->serviceMap);
    // Wait for changes on Restricted mode
    // 等待 Restricted mode 发生变化
    std::map<std::string, size_t> deviceList;

    for (size_t index = 0; index < devices.size(); index++)
    {
        deviceList.emplace(devices[index], index);
    }

    std::string filterStr;
    std::string devicesDbusPath{"/xyz/openbmc_project/control"};

    filterStr = sdbusplus::match_rules::propertiesChangedNamespace(
        devicesDbusPath, restrictionModeIntf);

    modeChangeMatch = std::make_unique<sdbusplus::match>(
        *bus, filterStr, [this, deviceList](sdbusplus::message_t& m) {
            handleRestrictedModeChange(m, deviceList);
        });
}

/** @brief Filter IPMI messages with RestrictedMode
 *  @param[in] request - IPMI message request
 *  @returns IPMI completion code success or error.
 */
/* @brief 使用 RestrictedMode 过滤 IPMI 消息
 *  @param[in] request - IPMI 消息请求
 *  @returns IPMI 完成码：成功或错误。 */

ipmi::Cc AllowlistFilter::filterMessage(ipmi::message::Request::ptr request)
{
    /* Getting hostIdx for all IPMI devices like hosts, debugcard and other
   devices from ipmi::message::Request and call postInit() to get the
   restriction mode for all the IPMI commands */
    /* 从 ipmi::message::Request 中获取所有 IPMI 设备（如主机、
     * 调试卡等）的 hostIdx，并调用 postInit() 为所有 IPMI 命令
     * 获取限制模式 */

    size_t hostIdx = request->ctx->hostIdx;

    if (request->ctx->channel == ipmi::channelSystemIface &&
        restrictedMode[hostIdx])
    {
        if (!std::binary_search(
                allowlist.cbegin(), allowlist.cend(),
                std::make_pair(request->ctx->netFn, request->ctx->cmd)))
        {
            lg2::error("Net function not allowlisted, "
                       "NetFn: {NETFN}, Cmd: {CMD}",
                       "NETFN", lg2::hex, request->ctx->netFn, "CMD", lg2::hex,
                       request->ctx->cmd);

            return ipmi::ccInsufficientPrivilege;
        }
    }
    return ipmi::ccSuccess;
}

// instantiate the AllowlistFilter when this shared object is loaded
// 在加载该共享库时实例化 AllowlistFilter
AllowlistFilter allowlistFilter;

} // namespace

} // namespace ipmi
