
/**
 * @file read_fru_data.cpp
 * @brief 分块读取 FRU 数据（含重试与缓存）的实现。
 */
/* @file read_fru_data.cpp
 * @brief 分块读取 FRU 数据（含重试与缓存）的实现。 */
#include "read_fru_data.hpp"

#include "fruread.hpp"

#include <ipmid/api.hpp>
#include <ipmid/types.hpp>
#include <ipmid/utils.hpp>
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/message/types.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

#include <algorithm>
#include <map>

extern const FruMap frus;
namespace ipmi
{
namespace fru
{

using namespace phosphor::logging;
using InternalFailure =
    sdbusplus::error::xyz::openbmc_project::common::InternalFailure;
std::unique_ptr<sdbusplus::match> matchPtr __attribute__((init_priority(101)));

namespace cache
{
// User initiate read FRU info area command followed by
// FRU read command. Also data is read in small chunks of
// the specified offset and count.
// Caching the data which will be invalidated when ever there
// is a change in FRU properties.
// 用户先发起 read FRU info area 命令，随后
// 发起 FRU read 命令。数据按指定的 offset
// 和 count 分小块读取。
// 数据会被缓存，当 FRU 属性发生变化时
// 缓存将失效。
FRUAreaMap fruMap;
} // namespace cache
/**
 * @brief Read all the property value's for the specified interface
 *  from Inventory.
 *
 * @param[in] intf Interface
 * @param[in] path Object path
 * @return map of properties
 */
/* @brief 从 Inventory 读取指定接口的
 *  所有属性值。
 * @param[in] intf 接口
 * @param[in] path 对象路径
 * @return 属性映射 */
ipmi::PropertyMap readAllProperties(const std::string& intf,
                                    const std::string& path)
{
    ipmi::PropertyMap properties;
    sdbusplus::bus_t bus{ipmid_get_sd_bus_connection()};
    std::string service;
    std::string objPath;

    // Is the path the full dbus path?
    // 该路径是否为完整的 dbus 路径？
    if (path.find(xyzPrefix) != std::string::npos)
    {
        service = ipmi::getService(bus, intf, path);
        objPath = path;
    }
    else
    {
        service = ipmi::getService(bus, invMgrInterface, invObjPath);
        objPath = invObjPath + path;
    }

    auto method = bus.new_method_call(service.c_str(), objPath.c_str(),
                                      propInterface, "GetAll");
    method.append(intf);
    try
    {
        auto reply = bus.call(method);
        reply.read(properties);
    }
    catch (const sdbusplus::exception_t& e)
    {
        // If property is not found simply return empty value
        // 若未找到属性，直接返回空值
        lg2::error("Error in reading property values: {ERROR}, path: {PATH}, "
                   "interface: {INTERFACE}",
                   "ERROR", e, "PATH", objPath, "INTERFACE", intf);
    }

    return properties;
}

void processFruPropChange(sdbusplus::message_t& msg)
{
    if (cache::fruMap.empty())
    {
        return;
    }
    std::string path = msg.get_path();
    // trim the object base path, if found at the beginning
    // 若在开头发现对象基础路径则将其去掉
    if (path.compare(0, strlen(invObjPath), invObjPath) == 0)
    {
        path.erase(0, strlen(invObjPath));
    }
    for (const auto& [fruId, instanceList] : frus)
    {
        auto found = std::find_if(
            instanceList.begin(), instanceList.end(),
            [&path](const auto& iter) { return (iter.path == path); });

        if (found != instanceList.end())
        {
            cache::fruMap.erase(fruId);
            break;
        }
    }
}

// register for fru property change
// 注册 FRU 属性变更监听
int registerCallbackHandler()
{
    if (matchPtr == nullptr)
    {
        using namespace sdbusplus::match_rules;
        sdbusplus::bus_t bus{ipmid_get_sd_bus_connection()};
        matchPtr = std::make_unique<sdbusplus::match>(
            bus,
            path_namespace(invObjPath) + type::signal() +
                member("PropertiesChanged") + interface(propInterface),
            std::bind(processFruPropChange, std::placeholders::_1));
    }
    return 0;
}

/**
 * @brief Read FRU property values from Inventory
 *
 * @param[in] fruNum  FRU id
 * @return populate FRU Inventory data
 */
/* @brief 从 Inventory 读取 FRU 属性值
 * @param[in] fruNum  FRU id
 * @return 填充 FRU Inventory 数据 */
FruInventoryData readDataFromInventory(const FRUId& fruNum)
{
    auto iter = frus.find(fruNum);
    if (iter == frus.end())
    {
        lg2::error("Unsupported FRU ID: {FRUID}", "FRUID", fruNum);
        elog<InternalFailure>();
    }

    FruInventoryData data;
    auto& instanceList = iter->second;
    for (auto& instance : instanceList)
    {
        for (auto& intf : instance.interfaces)
        {
            ipmi::PropertyMap allProp =
                readAllProperties(intf.first, instance.path);
            for (auto& properties : intf.second)
            {
                auto iter = allProp.find(properties.first);
                if (iter != allProp.end())
                {
                    data[properties.second.section].emplace(
                        properties.second.property,
                        std::move(
                            std::get<std::string>(allProp[properties.first])));
                }
            }
        }
    }
    return data;
}

const FruAreaData& getFruAreaData(const FRUId& fruNum)
{
    auto iter = cache::fruMap.find(fruNum);
    if (iter != cache::fruMap.end())
    {
        return iter->second;
    }
    auto invData = readDataFromInventory(fruNum);

    // Build area info based on inventory data
    // 根据 inventory 数据构建区信息
    FruAreaData newdata = buildFruAreaData(std::move(invData));
    cache::fruMap.emplace(fruNum, std::move(newdata));
    return cache::fruMap.at(fruNum);
}
} // namespace fru
} // namespace ipmi
