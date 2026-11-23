
/**
 * @file sdrutils.cpp
 * @brief SDR（Sensor Data Record）工具函数的实现。
 */
/*  @file sdrutils.cpp
 *  @brief SDR（Sensor Data Record）工具函数的实现。 */
/*
// Copyright (c) 2018 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
/* // 版权所有 (c) 2018 Intel Corporation
 * //
 * // 根据 Apache 许可证 2.0 版（"License"）授权；
 * // 除非符合许可证的规定，否则不得使用本文件。
 * // 你可以从以下地址获取许可证副本：
 * //
 * //      http://www.apache.org/licenses/LICENSE-2.0
 * //
 * // 除非适用法律要求或书面同意，否则本软件
 * // 按"原样"分发，
 * // 不提供任何明示或暗示的保证或条件。
 * // 有关许可证下的具体权限和限制，请参阅许可证
 * // 本身。 */

#include "dbus-sdr/sdrutils.hpp"

#include <ipmid/utils.hpp>
#include <nlohmann/json.hpp>
#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/ObjectMapper/common.hpp>
#include <xyz/openbmc_project/Sensor/Threshold/Critical/common.hpp>
#include <xyz/openbmc_project/Sensor/Threshold/Warning/common.hpp>
#include <xyz/openbmc_project/Sensor/Value/common.hpp>

#include <fstream>
#include <optional>
#include <unordered_set>

using ObjectMapper = sdbusplus::common::xyz::openbmc_project::ObjectMapper;
using SensorValue = sdbusplus::common::xyz::openbmc_project::sensor::Value;
using SensorThresholdWarning =
    sdbusplus::common::xyz::openbmc_project::sensor::threshold::Warning;
using SensorThresholdCritical =
    sdbusplus::common::xyz::openbmc_project::sensor::threshold::Critical;

#ifdef FEATURE_HYBRID_SENSORS

#include <ipmid/utils.hpp>
namespace ipmi
{
namespace sensor
{
extern const IdInfoMap sensors;
} // namespace sensor
} // namespace ipmi

#endif

boost::container::flat_map<
    const char*, std::pair<SensorTypeCodes, SensorEventTypeCodes>, CmpStr>
    sensorTypes{
        {{"temperature", std::make_pair(SensorTypeCodes::temperature,
                                        SensorEventTypeCodes::threshold)},
         {"voltage", std::make_pair(SensorTypeCodes::voltage,
                                    SensorEventTypeCodes::threshold)},
         {"current", std::make_pair(SensorTypeCodes::current,
                                    SensorEventTypeCodes::threshold)},
         {"fan_tach", std::make_pair(SensorTypeCodes::fan,
                                     SensorEventTypeCodes::threshold)},
         {"fan_pwm", std::make_pair(SensorTypeCodes::fan,
                                    SensorEventTypeCodes::threshold)},
         {"intrusion", std::make_pair(SensorTypeCodes::physicalSecurity,
                                      SensorEventTypeCodes::sensorSpecified)},
         {"processor", std::make_pair(SensorTypeCodes::processor,
                                      SensorEventTypeCodes::sensorSpecified)},
         {"power", std::make_pair(SensorTypeCodes::other,
                                  SensorEventTypeCodes::threshold)},
         {"memory", std::make_pair(SensorTypeCodes::memory,
                                   SensorEventTypeCodes::sensorSpecified)},
         {"state", std::make_pair(SensorTypeCodes::powerUnit,
                                  SensorEventTypeCodes::sensorSpecified)},
         {"buttons", std::make_pair(SensorTypeCodes::buttons,
                                    SensorEventTypeCodes::sensorSpecified)},
         {"watchdog", std::make_pair(SensorTypeCodes::watchdog2,
                                     SensorEventTypeCodes::sensorSpecified)},
         {"entity", std::make_pair(SensorTypeCodes::entity,
                                   SensorEventTypeCodes::sensorSpecified)},
         {"energy", std::make_pair(SensorTypeCodes::other,
                                   SensorEventTypeCodes::threshold)},
         {"utilization", std::make_pair(SensorTypeCodes::other,
                                        SensorEventTypeCodes::threshold)}}};

namespace details
{

// IPMI supports a smaller number of sensors than are available via Redfish.
// Trim the list of sensors, via a configuration file.
// Read the IPMI Sensor Filtering section in docs/configuration.md for
// a more detailed description.
// IPMI 支持的传感器数量少于通过 Redfish 可用的数量。
// 通过一个配置文件裁剪传感器列表。
// 更详细的说明请参阅 docs/configuration.md 中的
// IPMI Sensor Filtering 一节。
static void filterSensors(SensorSubTree& subtree)
{
    constexpr const char* filterFilename =
        "/usr/share/ipmi-providers/sensor_filter.json";
    std::ifstream filterFile(filterFilename);
    if (!filterFile.good())
    {
        return;
    }
    nlohmann::json sensorFilterJSON =
        nlohmann::json::parse(filterFile, nullptr, false);
    nlohmann::json::iterator svcFilterit =
        sensorFilterJSON.find("ServiceFilter");
    if (svcFilterit == sensorFilterJSON.end())
    {
        return;
    }

    subtree.erase(std::remove_if(subtree.begin(), subtree.end(),
                                 [svcFilterit](SensorSubTree::value_type& kv) {
                                     auto& [_, serviceToIfaces] = kv;

                                     for (auto service = svcFilterit->begin();
                                          service != svcFilterit->end();
                                          ++service)
                                     {
                                         serviceToIfaces.erase(*service);
                                     }
                                     return serviceToIfaces.empty();
                                 }),
                  subtree.end());
}

uint16_t getSensorSubtree(std::shared_ptr<SensorSubTree>& subtree)
{
    static std::shared_ptr<SensorSubTree> sensorTreePtr;
    static uint16_t sensorUpdatedIndex = 0;
    std::shared_ptr<sdbusplus::asio::connection> dbus = getSdBus();
    static sdbusplus::match sensorAdded(
        *dbus,
        "type='signal',member='InterfacesAdded',arg0path='/xyz/openbmc_project/"
        "sensors/'",
        [](sdbusplus::message_t&) { sensorTreePtr.reset(); });

    static sdbusplus::match sensorRemoved(
        *dbus,
        "type='signal',member='InterfacesRemoved',arg0path='/xyz/"
        "openbmc_project/sensors/'",
        [](sdbusplus::message_t&) { sensorTreePtr.reset(); });

    if (sensorTreePtr)
    {
        subtree = sensorTreePtr;
        return sensorUpdatedIndex;
    }

    sensorTreePtr = std::make_shared<SensorSubTree>();

    static constexpr const int32_t depth = 2;

    auto lbdUpdateSensorTree = [&dbus](const char* path,
                                       const auto& interfaces) {
        auto mapperCall = dbus->new_method_call(
            ObjectMapper::default_service, ObjectMapper::instance_path,
            ObjectMapper::interface, ObjectMapper::method_names::get_sub_tree);
        SensorSubTree sensorTreePartial;

        mapperCall.append(path, depth, interfaces);

        try
        {
            auto mapperReply = dbus->call(mapperCall);
            mapperReply.read(sensorTreePartial);
        }
        catch (const sdbusplus::exception_t& e)
        {
            lg2::error("Failed to update subtree, path: {PATH}, error: {ERROR}",
                       "PATH", path, "ERROR", e);
            return false;
        }
        if constexpr (debug)
        {
            std::fprintf(stderr, "IPMI updated: %zu sensors under %s\n",
                         sensorTreePartial.size(), path);
        }
        sensorTreePtr->merge(std::move(sensorTreePartial));
        return true;
    };

    // Add sensors to SensorTree
    // 将传感器添加到 SensorTree
    static constexpr const std::array sensorInterfaces = {
        SensorValue::interface, "xyz.openbmc_project.Sensor.ValueMutability",
        SensorThresholdWarning::interface, SensorThresholdCritical::interface};
    static constexpr const std::array vrInterfaces = {
        "xyz.openbmc_project.Control.VoltageRegulatorMode"};

    bool sensorRez =
        lbdUpdateSensorTree("/xyz/openbmc_project/sensors", sensorInterfaces);

#ifdef FEATURE_HYBRID_SENSORS

    if (!ipmi::sensor::sensors.empty())
    {
        for (const auto& sensor : ipmi::sensor::sensors)
        {
            // Threshold sensors should not be emplaced in here.
            // 阈值传感器不应在此处放置（emplace）。
            if (sensor.second.sensorPath.starts_with(
                    "/xyz/openbmc_project/sensors/"))
            {
                continue;
            }

            // The bus service name is not listed in ipmi::sensor::Info. Give it
            // an empty string. For those function using non-threshold sensors,
            // the bus service name will be retrieved in an alternative way.
            // 总线服务名称未在 ipmi::sensor::Info 中列出，
            // 给它一个空字符串。对于使用非阈值传感器的函数，
            // 总线服务名称将以另一种方式获取。
            boost::container::flat_map<std::string, std::vector<std::string>>
                connectionMap{
                    {"", {sensor.second.propertyInterfaces.begin()->first}}};
            sensorTreePtr->emplace(sensor.second.sensorPath, connectionMap);
        }
    }

#endif

    // Error if searching for sensors failed.
    // 如果搜索传感器失败则报错。
    if (!sensorRez)
    {
        return sensorUpdatedIndex;
    }

    filterSensors(*sensorTreePtr);
    // Add VR control as optional search path.
    // 将 VR 控制添加为可选的搜索路径。
    (void)lbdUpdateSensorTree("/xyz/openbmc_project/vr", vrInterfaces);

    subtree = sensorTreePtr;
    sensorUpdatedIndex++;
    // The SDR is being regenerated, wipe the old stats
    // 正在重新生成 SDR，清除旧的统计信息
    sdrStatsTable.wipeTable();
    sdrWriteTable.wipeTable();
    return sensorUpdatedIndex;
}

bool getSensorNumMap(std::shared_ptr<SensorNumMap>& sensorNumMap)
{
    static std::shared_ptr<SensorNumMap> sensorNumMapPtr;
    bool sensorNumMapUpdated = false;
    static uint16_t prevSensorUpdatedIndex = 0;
    std::shared_ptr<SensorSubTree> sensorTree;
    uint16_t curSensorUpdatedIndex = details::getSensorSubtree(sensorTree);
    if (!sensorTree)
    {
        return sensorNumMapUpdated;
    }

    if ((curSensorUpdatedIndex == prevSensorUpdatedIndex) && sensorNumMapPtr)
    {
        sensorNumMap = sensorNumMapPtr;
        return sensorNumMapUpdated;
    }
    prevSensorUpdatedIndex = curSensorUpdatedIndex;

    sensorNumMapPtr = std::make_shared<SensorNumMap>();

    uint16_t sensorNum = 0;
    uint16_t sensorIndex = 0;
    for (const auto& sensor : *sensorTree)
    {
        sensorNumMapPtr->insert(
            SensorNumMap::value_type(sensorNum, sensor.first));
        sensorIndex++;
        if (sensorIndex == maxSensorsPerLUN)
        {
            sensorIndex = lun1Sensor0;
        }
        else if (sensorIndex == (lun1Sensor0 | maxSensorsPerLUN))
        {
            // Skip assigning LUN 0x2 any sensors
            // 跳过为 LUN 0x2 分配任何传感器
            sensorIndex = lun3Sensor0;
        }
        else if (sensorIndex == (lun3Sensor0 | maxSensorsPerLUN))
        {
            // this is an error, too many IPMI sensors
            // 这是一个错误，IPMI 传感器过多
            throw std::out_of_range("Maximum number of IPMI sensors exceeded.");
        }
        sensorNum = sensorIndex;
    }
    sensorNumMap = sensorNumMapPtr;
    sensorNumMapUpdated = true;
    return sensorNumMapUpdated;
}
} // namespace details

bool getSensorSubtree(SensorSubTree& subtree)
{
    std::shared_ptr<SensorSubTree> sensorTree;
    details::getSensorSubtree(sensorTree);
    if (!sensorTree)
    {
        return false;
    }

    subtree = *sensorTree;
    return true;
}

#ifdef FEATURE_HYBRID_SENSORS
// Static sensors are listed in sensor-gen.cpp.
// 静态传感器列在 sensor-gen.cpp 中。
ipmi::sensor::IdInfoMap::const_iterator findStaticSensor(
    const std::string& path)
{
    return std::find_if(
        ipmi::sensor::sensors.begin(), ipmi::sensor::sensors.end(),
        [&path](const ipmi::sensor::IdInfoMap::value_type& findSensor) {
            return findSensor.second.sensorPath == path;
        });
}
#endif

std::string getSensorTypeStringFromPath(const std::string& path)
{
    // get sensor type string from path, path is defined as
    // /xyz/openbmc_project/sensors/<type>/label
    // 从路径中获取传感器类型字符串，路径定义为
    // /xyz/openbmc_project/sensors/<type>/label
    size_t typeEnd = path.rfind("/");
    if (typeEnd == std::string::npos)
    {
        return path;
    }
    size_t typeStart = path.rfind("/", typeEnd - 1);
    if (typeStart == std::string::npos)
    {
        return path;
    }
    // Start at the character after the '/'
    // 从 '/' 之后的字符开始
    typeStart++;
    return path.substr(typeStart, typeEnd - typeStart);
}

uint8_t getSensorTypeFromPath(const std::string& path)
{
    uint8_t sensorType = 0;
    std::string type = getSensorTypeStringFromPath(path);
    auto findSensor = sensorTypes.find(type.c_str());
    if (findSensor != sensorTypes.end())
    {
        sensorType =
            static_cast<uint8_t>(std::get<sensorTypeCodes>(findSensor->second));
    } // else default 0x0 RESERVED

    return sensorType;
}

uint16_t getSensorNumberFromPath(const std::string& path)
{
    std::shared_ptr<SensorNumMap> sensorNumMapPtr;
    details::getSensorNumMap(sensorNumMapPtr);
    if (!sensorNumMapPtr)
    {
        return invalidSensorNumber;
    }

    try
    {
        return sensorNumMapPtr->right.at(path);
    }
    catch (const std::out_of_range& e)
    {
        return invalidSensorNumber;
    }
}

uint8_t getSensorEventTypeFromPath(const std::string& path)
{
    uint8_t sensorEventType = 0;
    std::string type = getSensorTypeStringFromPath(path);
    auto findSensor = sensorTypes.find(type.c_str());
    if (findSensor != sensorTypes.end())
    {
        sensorEventType = static_cast<uint8_t>(
            std::get<sensorEventTypeCodes>(findSensor->second));
    }

    return sensorEventType;
}

std::string getPathFromSensorNumber(uint16_t sensorNum)
{
    std::shared_ptr<SensorNumMap> sensorNumMapPtr;
    details::getSensorNumMap(sensorNumMapPtr);
    if (!sensorNumMapPtr)
    {
        return std::string();
    }

    try
    {
        return sensorNumMapPtr->left.at(sensorNum);
    }
    catch (const std::out_of_range& e)
    {
        return std::string();
    }
}

namespace ipmi
{

std::optional<std::map<std::string, std::vector<std::string>>>
    getObjectInterfaces(const char* path)
{
    std::map<std::string, std::vector<std::string>> interfacesResponse;
    std::vector<std::string> interfaces;
    std::shared_ptr<sdbusplus::asio::connection> dbus = getSdBus();

    sdbusplus::message_t getObjectMessage = dbus->new_method_call(
        ObjectMapper::default_service, ObjectMapper::instance_path,
        ObjectMapper::interface, ObjectMapper::method_names::get_object);
    getObjectMessage.append(path, interfaces);

    try
    {
        sdbusplus::message_t response = dbus->call(getObjectMessage);
        response.read(interfacesResponse);
    }
    catch (const std::exception& e)
    {
        return std::nullopt;
    }

    return interfacesResponse;
}

std::map<std::string, Value> getEntityManagerProperties(const char* path,
                                                        const char* interface)
{
    std::map<std::string, Value> properties;
    std::shared_ptr<sdbusplus::asio::connection> dbus = getSdBus();

    sdbusplus::message_t getProperties =
        dbus->new_method_call("xyz.openbmc_project.EntityManager", path,
                              "org.freedesktop.DBus.Properties", "GetAll");
    getProperties.append(interface);

    try
    {
        sdbusplus::message_t response = dbus->call(getProperties);
        response.read(properties);
    }
    catch (const std::exception& e)
    {
        lg2::error("Failed to GetAll, path: {PATH}, interface: {INTERFACE}, "
                   "error: {ERROR}",
                   "PATH", path, "INTERFACE", interface, "ERROR", e);
    }

    return properties;
}

// Fetch the ipmiDecoratorPaths to get the list of dbus objects that
// have ipmi decorator to prevent unnecessary dbus call to fetch the info
// 获取 ipmiDecoratorPaths 以得到带有 ipmi 装饰器的 dbus 对象列表，
// 避免不必要的 dbus 调用来获取信息
std::optional<std::unordered_set<std::string>>& getIpmiDecoratorPaths(
    const std::optional<ipmi::Context::ptr>& ctx)
{
    static std::optional<std::unordered_set<std::string>> ipmiDecoratorPaths;

    if (!ctx.has_value() || ipmiDecoratorPaths != std::nullopt)
    {
        return ipmiDecoratorPaths;
    }

    using Paths = std::vector<std::string>;
    boost::system::error_code ec;
    Paths paths = ipmi::callDbusMethod<Paths>(
        *ctx, ec, ObjectMapper::default_service, ObjectMapper::instance_path,
        ObjectMapper::interface, ObjectMapper::method_names::get_sub_tree_paths,
        "/", int32_t(0),
        std::array<const char*, 1>{
            "xyz.openbmc_project.Inventory.Decorator.Ipmi"});

    if (ec)
    {
        return ipmiDecoratorPaths;
    }

    ipmiDecoratorPaths =
        std::unordered_set<std::string>(paths.begin(), paths.end());
    return ipmiDecoratorPaths;
}

const std::string* getSensorConfigurationInterface(
    const std::map<std::string, std::vector<std::string>>&
        sensorInterfacesResponse)
{
    auto entityManagerService =
        sensorInterfacesResponse.find("xyz.openbmc_project.EntityManager");
    if (entityManagerService == sensorInterfacesResponse.end())
    {
        return nullptr;
    }

    // Find the fan configuration first (fans can have multiple configuration
    // interfaces).
    // 先查找风扇配置（风扇可以有多个配置
    // 接口）。
    for (const auto& entry : entityManagerService->second)
    {
        if (entry == "xyz.openbmc_project.Configuration.AspeedFan" ||
            entry == "xyz.openbmc_project.Configuration.I2CFan" ||
            entry == "xyz.openbmc_project.Configuration.NuvotonFan")
        {
            return &entry;
        }
    }

    for (const auto& entry : entityManagerService->second)
    {
        if (entry.starts_with("xyz.openbmc_project.Configuration."))
        {
            return &entry;
        }
    }

    return nullptr;
}

// Follow Association properties for Sensor back to the Board dbus object to
// check for an EntityId and EntityInstance property.
// 沿着 Sensor 的 Association 属性回溯到 Board dbus 对象，
// 检查 EntityId 和 EntityInstance 属性。
void updateIpmiFromAssociation(
    const std::string& path,
    const std::unordered_set<std::string>& ipmiDecoratorPaths,
    const DbusInterfaceMap& sensorMap, uint8_t& entityId,
    uint8_t& entityInstance)
{
    namespace fs = std::filesystem;

    auto sensorAssociationObject =
        sensorMap.find("xyz.openbmc_project.Association.Definitions");
    if (sensorAssociationObject == sensorMap.end())
    {
        if constexpr (debug)
        {
            std::fprintf(stderr, "path=%s, no association interface found\n",
                         path.c_str());
        }

        return;
    }

    auto associationObject =
        sensorAssociationObject->second.find("Associations");
    if (associationObject == sensorAssociationObject->second.end())
    {
        if constexpr (debug)
        {
            std::fprintf(stderr, "path=%s, no association records found\n",
                         path.c_str());
        }

        return;
    }

    std::vector<Association> associationValues =
        std::get<std::vector<Association>>(associationObject->second);

    // loop through the Associations looking for the right one:
    // 遍历 Associations 寻找正确的那一个：
    for (const auto& entry : associationValues)
    {
        // forward, reverse, endpoint
        // forward、reverse、endpoint
        const std::string& forward = std::get<0>(entry);
        const std::string& reverse = std::get<1>(entry);
        const std::string& endpoint = std::get<2>(entry);

        // We only currently concern ourselves with chassis+all_sensors.
        // 目前我们只关注 chassis+all_sensors。
        if (!(forward == "chassis" && reverse == "all_sensors"))
        {
            continue;
        }

        // the endpoint is the board entry provided by
        // Entity-Manager. so let's grab its properties if it has
        // the right interface.
        // endpoint 是 Entity-Manager 提供的板卡条目。
        // 因此，如果它具有正确的接口，就获取
        // 它的属性。

        // just try grabbing the properties first.
        // 先尝试直接获取属性。
        ipmi::PropertyMap::iterator entityIdProp;
        ipmi::PropertyMap::iterator entityInstanceProp;
        if (ipmiDecoratorPaths.contains(endpoint))
        {
            std::map<std::string, Value> ipmiProperties =
                getEntityManagerProperties(
                    endpoint.c_str(),
                    "xyz.openbmc_project.Inventory.Decorator.Ipmi");

            entityIdProp = ipmiProperties.find("EntityId");
            entityInstanceProp = ipmiProperties.find("EntityInstance");
            if (entityIdProp != ipmiProperties.end())
            {
                entityId = static_cast<uint8_t>(
                    std::get<uint64_t>(entityIdProp->second));
            }
            if (entityInstanceProp != ipmiProperties.end())
            {
                entityInstance = static_cast<uint8_t>(
                    std::get<uint64_t>(entityInstanceProp->second));
            }
        }

        // Now check the entity-manager entry for this sensor to see
        // if it has its own value and use that instead.
        //
        // In theory, checking this first saves us from checking
        // both, except in most use-cases identified, there won't be
        // a per sensor override, so we need to always check both.
        // 现在检查该传感器在 entity-manager 中的条目，看看
        // 它是否有自己的值，若有则改用它。
        // 理论上，先检查这里可以免去同时检查两者，
        // 但在已识别的大多数使用场景中并不会有针对
        // 单个传感器的覆盖，因此仍需始终检查两者。
        std::string sensorNameFromPath = fs::path(path).filename();

        std::string sensorConfigPath = endpoint + "/" + sensorNameFromPath;

        // Download the interfaces for the sensor from
        // Entity-Manager to find the name of the configuration
        // interface.
        // 从 Entity-Manager 下载传感器的接口，
        // 以找到配置
        // 接口的名称。
        std::optional<std::map<std::string, std::vector<std::string>>>
            sensorInterfacesResponseOpt =
                getObjectInterfaces(sensorConfigPath.c_str());

        if (!sensorInterfacesResponseOpt.has_value())
        {
            lg2::debug("Failed to GetObject, path: {PATH}", "PATH",
                       sensorConfigPath);
            continue;
        }

        const std::string* configurationInterface =
            getSensorConfigurationInterface(
                sensorInterfacesResponseOpt.value());

        // If there are multi association path settings and only one path exist,
        // we need to continue if cannot find configuration interface for this
        // sensor.
        // 如果存在多个关联路径设置而实际只有一个路径，
        // 当找不到该传感器的配置接口时，
        // 我们需要继续处理。
        if (!configurationInterface)
        {
            continue;
        }

        // We found a configuration interface.
        // 我们找到了一个配置接口。
        std::map<std::string, Value> configurationProperties =
            getEntityManagerProperties(sensorConfigPath.c_str(),
                                       configurationInterface->c_str());

        entityIdProp = configurationProperties.find("EntityId");
        entityInstanceProp = configurationProperties.find("EntityInstance");
        if (entityIdProp != configurationProperties.end())
        {
            entityId =
                static_cast<uint8_t>(std::get<uint64_t>(entityIdProp->second));
        }
        if (entityInstanceProp != configurationProperties.end())
        {
            entityInstance = static_cast<uint8_t>(
                std::get<uint64_t>(entityInstanceProp->second));
        }

        // stop searching Association records.
        // 停止搜索 Association 记录。
        break;
    } // end for Association vectors.

    if constexpr (debug)
    {
        std::fprintf(stderr, "path=%s, entityId=%d, entityInstance=%d\n",
                     path.c_str(), entityId, entityInstance);
    }
}

} // namespace ipmi
