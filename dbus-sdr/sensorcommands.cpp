
/**
 * @file sensorcommands.cpp
 * @brief 传感器 IPMI 命令（Get Sensor Reading 等）的 D-Bus 后端实现。
 */
/*  @file sensorcommands.cpp
 *  @brief 传感器 IPMI 命令（Get Sensor Reading 等）的 D-Bus 后端实现。 */
/*
// Copyright (c) 2017 2018 Intel Corporation
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
/* // 版权所有 (c) 2017 2018 Intel Corporation
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

#include "config.h"

#include "dbus-sdr/sensorcommands.hpp"

#include "dbus-sdr/sdrutils.hpp"
#include "dbus-sdr/sensorutils.hpp"
#include "dbus-sdr/storagecommands.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/container/flat_map.hpp>
#include <ipmid/api.hpp>
#include <ipmid/entity_map_json.hpp>
#include <ipmid/types.hpp>
#include <ipmid/utils.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/bus.hpp>
#include <user_channel/channel_layer.hpp>
#include <xyz/openbmc_project/Sensor/Threshold/Critical/common.hpp>
#include <xyz/openbmc_project/Sensor/Threshold/Warning/common.hpp>
#include <xyz/openbmc_project/Sensor/Value/common.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

using SensorValue = sdbusplus::common::xyz::openbmc_project::sensor::Value;
using SensorThresholdWarning =
    sdbusplus::common::xyz::openbmc_project::sensor::threshold::Warning;
using SensorThresholdCritical =
    sdbusplus::common::xyz::openbmc_project::sensor::threshold::Critical;

#ifdef FEATURE_HYBRID_SENSORS

#include "sensordatahandler.hpp"
namespace ipmi
{
namespace sensor
{
extern const IdInfoMap sensors;
} // namespace sensor
} // namespace ipmi
#endif
namespace ipmi
{
namespace dcmi
{
// Refer Table 6-14, DCMI Entity ID Extension, DCMI v1.5 spec
// 参见 DCMI v1.5 规范中表 6-14：DCMI Entity ID Extension
static const std::map<uint8_t, uint8_t> validEntityId{
    {0x40, 0x37}, {0x37, 0x40}, {0x41, 0x03},
    {0x03, 0x41}, {0x42, 0x07}, {0x07, 0x42}};
constexpr uint8_t temperatureSensorType = 0x01;
constexpr uint8_t maxRecords = 8;
} // namespace dcmi
} // namespace ipmi
namespace ipmi
{

using phosphor::logging::entry;
using phosphor::logging::level;
using phosphor::logging::log;

static constexpr int sensorMapUpdatePeriod = 10;
static constexpr int sensorMapSdrUpdatePeriod = 60;

// BMC I2C address is generally at 0x20
// BMC 的 I2C 地址通常是 0x20
static constexpr uint8_t bmcI2CAddr = 0x20;

constexpr size_t maxSDRTotalSize =
    76; // Largest SDR Record Size (type 01) + SDR Overheader Size
constexpr static const uint32_t noTimestamp = 0xFFFFFFFF;

static uint16_t sdrReservationID;
static uint32_t sdrLastAdd = noTimestamp;
static uint32_t sdrLastRemove = noTimestamp;
static constexpr size_t lastRecordIndex = 0xFFFF;

// The IPMI spec defines four Logical Units (LUN), each capable of supporting
// 255 sensors. The 256 values assigned to LUN 2 are special and are not used
// for general purpose sensors. Each LUN reserves location 0xFF. The maximum
// number of IPMI sensors are LUN 0 + LUN 1 + LUN 3, less the reserved
// location.
// IPMI 规范定义了四个逻辑单元（LUN），每个可支持
// 255 个传感器。分配给 LUN 2 的 256 个值是特殊的，
// 不用于通用传感器。每个 LUN 都保留位置 0xFF。
// IPMI 传感器的最大数量是 LUN 0 + LUN 1 + LUN 3，
// 再减去保留位置。
static constexpr size_t maxIPMISensors = ((3 * 256) - (3 * 1));

static constexpr uint8_t lun0 = 0x0;
static constexpr uint8_t lun1 = 0x1;
static constexpr uint8_t lun3 = 0x3;

static constexpr size_t lun0MaxSensorNum = 0xfe;
static constexpr size_t lun1MaxSensorNum = 0x1fe;
static constexpr size_t lun3MaxSensorNum = 0x3fe;
static constexpr int GENERAL_ERROR = -1;

static boost::container::flat_map<std::string, ObjectValueTree> SensorCache;

// Specify the comparison required to sort and find char* map objects
// 指定用于对 char* 映射对象进行排序和查找的比较函数
struct CmpStr
{
    bool operator()(const char* a, const char* b) const
    {
        return std::strcmp(a, b) < 0;
    }
};
const static boost::container::flat_map<const char*, SensorUnits, CmpStr>
    sensorUnits{{{"temperature", SensorUnits::degreesC},
                 {"voltage", SensorUnits::volts},
                 {"current", SensorUnits::amps},
                 {"fan_tach", SensorUnits::rpm},
                 {"power", SensorUnits::watts},
                 {"energy", SensorUnits::joules}}};

void registerSensorFunctions() __attribute__((constructor));

static sdbusplus::match sensorAdded(
    *getSdBus(),
    "type='signal',member='InterfacesAdded',arg0path='/xyz/openbmc_project/"
    "sensors/'",
    [](sdbusplus::message_t&) {
        getSensorTree().clear();
        getIpmiDecoratorPaths(/*ctx=*/std::nullopt).reset();
        sdrLastAdd = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    });

static sdbusplus::match sensorRemoved(
    *getSdBus(),
    "type='signal',member='InterfacesRemoved',arg0path='/xyz/openbmc_project/"
    "sensors/'",
    [](sdbusplus::message_t&) {
        getSensorTree().clear();
        getIpmiDecoratorPaths(/*ctx=*/std::nullopt).reset();
        sdrLastRemove = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    });

ipmi::Cc getSensorConnection(ipmi::Context::ptr ctx, uint8_t sensnum,
                             std::string& connection, std::string& path,
                             std::vector<std::string>* interfaces)
{
    auto& sensorTree = getSensorTree();
    if (!getSensorSubtree(sensorTree) && sensorTree.empty())
    {
        return ipmi::ccResponseError;
    }

    if (ctx == nullptr)
    {
        return ipmi::ccResponseError;
    }

    path = getPathFromSensorNumber((ctx->lun << 8) | sensnum);
    if (path.empty())
    {
        return ipmi::ccInvalidFieldRequest;
    }

    for (const auto& sensor : sensorTree)
    {
        if (path == sensor.first)
        {
            connection = sensor.second.begin()->first;
            if (interfaces)
                *interfaces = sensor.second.begin()->second;
            break;
        }
    }

    return 0;
}

SensorSubTree& getSensorTree()
{
    static SensorSubTree sensorTree;
    return sensorTree;
}

// this keeps track of deassertions for sensor event status command. A
// deassertion can only happen if an assertion was seen first.
// 这里记录传感器事件状态命令的去断言（deassertion）。
// 只有先看到断言（assertion）之后才可能发生去断言。
static boost::container::flat_map<
    std::string, boost::container::flat_map<std::string, std::optional<bool>>>
    thresholdDeassertMap;

static sdbusplus::match thresholdChanged(
    *getSdBus(),
    "type='signal',member='PropertiesChanged',interface='org.freedesktop.DBus."
    "Properties',arg0namespace='xyz.openbmc_project.Sensor.Threshold'",
    [](sdbusplus::message_t& m) {
        boost::container::flat_map<std::string, std::variant<bool, double>>
            values;
        m.read(std::string(), values);

        auto findAssert =
            std::find_if(values.begin(), values.end(), [](const auto& pair) {
                return pair.first.find("Alarm") != std::string::npos;
            });
        if (findAssert != values.end())
        {
            auto ptr = std::get_if<bool>(&(findAssert->second));
            if (ptr == nullptr)
            {
                lg2::error("thresholdChanged: Assert non bool");
                return;
            }
            if (*ptr)
            {
                lg2::info(
                    "thresholdChanged: Assert, sensor path: {SENSOR_PATH}",
                    "SENSOR_PATH", m.get_path());
                thresholdDeassertMap[m.get_path()][findAssert->first] = *ptr;
            }
            else
            {
                auto& value =
                    thresholdDeassertMap[m.get_path()][findAssert->first];
                if (value)
                {
                    lg2::info(
                        "thresholdChanged: deassert, sensor path: {SENSOR_PATH}",
                        "SENSOR_PATH", m.get_path());
                    value = *ptr;
                }
            }
        }
    });

namespace sensor
{
static constexpr const char* vrInterface =
    "xyz.openbmc_project.Control.VoltageRegulatorMode";
} // namespace sensor

static void getSensorMaxMin(const DbusInterfaceMap& sensorMap, double& max,
                            double& min)
{
    max = 127;
    min = -128;

    auto sensorObject = sensorMap.find(SensorValue::interface);
    auto critical = sensorMap.find(SensorThresholdCritical::interface);
    auto warning = sensorMap.find(SensorThresholdWarning::interface);

    if (sensorObject != sensorMap.end())
    {
        auto maxMap =
            sensorObject->second.find(SensorValue::property_names::max_value);
        auto minMap =
            sensorObject->second.find(SensorValue::property_names::min_value);

        if (maxMap != sensorObject->second.end())
        {
            max = std::visit(VariantToDoubleVisitor(), maxMap->second);
        }
        if (minMap != sensorObject->second.end())
        {
            min = std::visit(VariantToDoubleVisitor(), minMap->second);
        }
    }
    if (critical != sensorMap.end())
    {
        auto lower = critical->second.find(
            SensorThresholdCritical::property_names::critical_low);
        auto upper = critical->second.find(
            SensorThresholdCritical::property_names::critical_high);
        if (lower != critical->second.end())
        {
            double value = std::visit(VariantToDoubleVisitor(), lower->second);
            if (std::isfinite(value))
            {
                min = std::fmin(value, min);
            }
        }
        if (upper != critical->second.end())
        {
            double value = std::visit(VariantToDoubleVisitor(), upper->second);
            if (std::isfinite(value))
            {
                max = std::fmax(value, max);
            }
        }
    }
    if (warning != sensorMap.end())
    {
        auto lower = warning->second.find(
            SensorThresholdWarning::property_names::warning_low);
        auto upper = warning->second.find(
            SensorThresholdWarning::property_names::warning_high);
        if (lower != warning->second.end())
        {
            double value = std::visit(VariantToDoubleVisitor(), lower->second);
            if (std::isfinite(value))
            {
                min = std::fmin(value, min);
            }
        }
        if (upper != warning->second.end())
        {
            double value = std::visit(VariantToDoubleVisitor(), upper->second);
            if (std::isfinite(value))
            {
                max = std::fmax(value, max);
            }
        }
    }
}

static bool getSensorMap(ipmi::Context::ptr ctx, std::string sensorConnection,
                         std::string sensorPath, DbusInterfaceMap& sensorMap,
                         int updatePeriod = sensorMapUpdatePeriod)
{
#ifdef FEATURE_HYBRID_SENSORS
    if (auto sensor = findStaticSensor(sensorPath);
        sensor != ipmi::sensor::sensors.end() &&
        getSensorEventTypeFromPath(sensorPath) !=
            static_cast<uint8_t>(SensorEventTypeCodes::threshold))
    {
        // If the incoming sensor is a discrete sensor, it might fail in
        // getManagedObjects(), return true, and use its own getFunc to get
        // value.
        // 如果传入的传感器是离散传感器，它可能在 getManagedObjects() 中
        // 失败，此时返回 true，并使用其自身的 getFunc 来
        // 获取值。
        return true;
    }
#endif

    static boost::container::flat_map<
        std::string, std::chrono::time_point<std::chrono::steady_clock>>
        updateTimeMap;

    auto updateFind = updateTimeMap.find(sensorConnection);
    auto lastUpdate = std::chrono::time_point<std::chrono::steady_clock>();
    if (updateFind != updateTimeMap.end())
    {
        lastUpdate = updateFind->second;
    }

    auto now = std::chrono::steady_clock::now();

    if (std::chrono::duration_cast<std::chrono::seconds>(now - lastUpdate)
            .count() > updatePeriod)
    {
        bool found = false;

        // Object managers for different kinds of OpenBMC DBus interfaces.
        // Documented in the phosphor-dbus-interfaces repository.
        // OpenBMC 不同种类 DBus 接口的对象管理器。
        // 文档见 phosphor-dbus-interfaces 仓库。
        const char* paths[] = {
            "/xyz/openbmc_project/sensors",
            "/xyz/openbmc_project/vr",
        };
        constexpr size_t numPaths = sizeof(paths) / sizeof(paths[0]);
        ObjectValueTree allManagedObjects;

        for (size_t i = 0; i < numPaths; i++)
        {
            ObjectValueTree managedObjects;
            boost::system::error_code ec = getManagedObjects(
                ctx, sensorConnection.c_str(), paths[i], managedObjects);
            if (ec)
            {
                continue;
            }
            allManagedObjects.merge(managedObjects);
            found = true;
        }

        if (!found)
        {
            lg2::error("GetManagedObjects for getSensorMap failed, "
                       "service: {SERVICE}",
                       "SERVICE", sensorConnection);

            return false;
        }

        SensorCache[sensorConnection] = allManagedObjects;
        // Update time after finish building the map which allow the
        // data to be cached for updatePeriod plus the build time.
        // 在构建完 map 之后更新时间，这样数据可以被缓存
        // updatePeriod 加上构建所需的时间。
        updateTimeMap[sensorConnection] = std::chrono::steady_clock::now();
    }
    auto connection = SensorCache.find(sensorConnection);
    if (connection == SensorCache.end())
    {
        return false;
    }
    auto path = connection->second.find(sensorPath);
    if (path == connection->second.end())
    {
        return false;
    }
    sensorMap = path->second;

    return true;
}

namespace sensor
{
// Read VR profiles from sensor(daemon) interface
// 从 sensor(daemon) 接口读取 VR profile
static std::optional<std::vector<std::string>> getSupportedVrProfiles(
    const ipmi::DbusInterfaceMap::mapped_type& object)
{
    // get VR mode profiles from Supported Interface
    // 从 Supported Interface 获取 VR mode profile
    auto supportedProperty = object.find("Supported");
    if (supportedProperty == object.end() ||
        object.find("Selected") == object.end())
    {
        lg2::error("Missing the required Supported and Selected properties");
        return std::nullopt;
    }

    const auto profilesPtr =
        std::get_if<std::vector<std::string>>(&supportedProperty->second);

    if (profilesPtr == nullptr)
    {
        lg2::error("property is not array of string");
        return std::nullopt;
    }
    return *profilesPtr;
}

// Calculate VR Mode from input IPMI discrete event bytes
// 根据输入的 IPMI 离散事件字节计算 VR Mode
static std::optional<std::string> calculateVRMode(
    uint15_t assertOffset, const ipmi::DbusInterfaceMap::mapped_type& VRObject)
{
    // get VR mode profiles from Supported Interface
    // 从 Supported Interface 获取 VR mode profile
    auto profiles = getSupportedVrProfiles(VRObject);
    if (!profiles)
    {
        return std::nullopt;
    }

    // interpret IPMI cmd bits into profiles' index
    // 将 IPMI 命令位解释为 profile 的索引
    long unsigned int index = 0;
    // only one bit should be set and the highest bit should not be used.
    // 只能置一个位，且最高位不应使用。
    if (assertOffset == 0 || assertOffset == (1u << 15) ||
        (assertOffset & (assertOffset - 1)))
    {
        lg2::error("IPMI cmd format incorrect, bytes: {BYTES}", "BYTES",
                   lg2::hex, static_cast<uint16_t>(assertOffset));
        return std::nullopt;
    }

    while (assertOffset != 1)
    {
        assertOffset >>= 1;
        index++;
    }

    if (index >= profiles->size())
    {
        lg2::error("profile index out of boundary");
        return std::nullopt;
    }

    return profiles->at(index);
}

// Calculate sensor value from IPMI reading byte
// 根据 IPMI 读数字节计算传感器值
static std::optional<double> calculateValue(
    uint8_t reading, const ipmi::DbusInterfaceMap& sensorMap,
    const ipmi::DbusInterfaceMap::mapped_type& valueObject)
{
    if (valueObject.find(SensorValue::property_names::value) ==
        valueObject.end())
    {
        lg2::error("Missing the required Value property");
        return std::nullopt;
    }

    double max = 0;
    double min = 0;
    getSensorMaxMin(sensorMap, max, min);

    int16_t mValue = 0;
    int16_t bValue = 0;
    int8_t rExp = 0;
    int8_t bExp = 0;
    bool bSigned = false;

    if (!getSensorAttributes(max, min, mValue, rExp, bValue, bExp, bSigned))
    {
        return std::nullopt;
    }

    double value = bSigned ? ((int8_t)reading) : reading;

    value *= ((double)mValue);
    value += ((double)bValue) * std::pow(10.0, bExp);
    value *= std::pow(10.0, rExp);

    return value;
}

// Extract file name from sensor path as the sensors SDR ID. Simplify the name
// if it is too long.
// 从传感器路径中提取文件名作为传感器的 SDR ID。
// 如果名称太长则简化它。
std::string parseSdrIdFromPath(const std::string& path)
{
    std::string name;
    size_t nameStart = path.rfind("/");
    if (nameStart != std::string::npos)
    {
        name = path.substr(nameStart + 1, std::string::npos - nameStart);
    }

    if (name.size() > FULL_RECORD_ID_STR_MAX_LENGTH)
    {
#ifdef SHORTNAME_REMOVE_SUFFIX
        constexpr std::array<const char*, 7> suffixes = {
            "_Output_Voltage", "_Input_Voltage", "_Output_Current",
            "_Input_Current",  "_Output_Power",  "_Input_Power",
            "_Temperature"};
        for (const auto& suffix : suffixes)
        {
            if (name.ends_with(suffix))
            {
                boost::replace_all(name, suffix, "");
                break;
            }
        }
#endif
#ifdef SHORTNAME_REPLACE_WORDS
        constexpr std::array<std::pair<const char*, const char*>, 2>
            replaceWords = {std::make_pair("Output", "Out"),
                            std::make_pair("Input", "In")};
        for (const auto& [find, replace] : replaceWords)
        {
            boost::replace_all(name, find, replace);
        }
#endif

        // as a backup and if nothing else is configured
        // 作为后备方案，且未配置其他内容时使用
        if (name.size() > FULL_RECORD_ID_STR_MAX_LENGTH)
        {
            name.resize(FULL_RECORD_ID_STR_MAX_LENGTH);
        }
    }
    return name;
}

bool getVrEventStatus(ipmi::Context::ptr ctx, const std::string& connection,
                      const std::string& path,
                      const ipmi::DbusInterfaceMap::mapped_type& object,
                      std::bitset<16>& assertions)
{
    auto profiles = sensor::getSupportedVrProfiles(object);
    if (!profiles)
    {
        return false;
    }
    std::string mode;

    auto ec = getDbusProperty(ctx, connection, path, sensor::vrInterface,
                              "Selected", mode);
    if (ec)
    {
        lg2::error("Failed to get Selected, path: {PATH}, "
                   "interface: {INTERFACE}, error: {ERROR}",
                   "PATH", path, "INTERFACE", SensorValue::interface, "ERROR",
                   ec.message());
        return false;
    }

    auto itr = std::find(profiles->begin(), profiles->end(), mode);
    if (itr == profiles->end())
    {
        lg2::error("VR mode doesn't match any of its profiles, path: {PATH}",
                   "PATH", path);
        return false;
    }
    std::size_t index =
        static_cast<std::size_t>(std::distance(profiles->begin(), itr));

    // map index to response event assertion bit.
    // 将索引映射到响应事件的断言位。
    if (index < 16)
    {
        assertions.set(index);
    }
    else
    {
        lg2::error("VR profile index reaches max assertion bit, "
                   "path: {PATH}, index: {INDEX}",
                   "PATH", path, "INDEX", index);
        return false;
    }
    if constexpr (debug)
    {
        lg2::error("VR sensor {PATH} mode is: [{INDEX}] {MODE}", "PATH",
                   sensor::parseSdrIdFromPath(path), "INDEX", index, "MODE",
                   mode);
    }
    return true;
}

/*
 * Handle every Sensor Data Record besides Type 01
 *
 * The D-Bus sensors work well for generating Type 01 SDRs.
 * After the Type 01 sensors are processed the remaining sensor types require
 * special handling. Each BMC vendor is going to have their own requirements for
 * insertion of non-Type 01 records.
 * Manage non-Type 01 records:
 *
 * Create a new file: dbus-sdr/sensorcommands_oem.cpp
 * Populate it with the two weakly linked functions below, without adding the
 * 'weak' attribute definition prior to the function definition.
 *    getOtherSensorsCount(...)
 *    getOtherSensorsDataRecord(...)
 *    Example contents are provided in the weak definitions below
 *    Enable 'sensors-oem' in your phosphor-ipmi-host bbappend file
 *      'EXTRA_OEMESON:append = " -Dsensors-oem=enabled"'
 * The contents of the sensorcommands_oem.cpp file will then override the code
 * provided below.
 */
/*  处理除 Type 01 之外的所有 Sensor Data Record
 *  D-Bus 传感器非常适合用于生成 Type 01 SDR。
 *  在处理完 Type 01 传感器后，其余传感器类型需要
 *  特殊处理。每个 BMC 厂商对于插入非 Type 01 记录
 *  都有自己的要求。
 *  管理非 Type 01 记录的方法：
 *  新建一个文件：dbus-sdr/sensorcommands_oem.cpp
 *  用下面两个弱链接函数填充它，不要在函数定义前添加
 *  'weak' 属性定义。
 *     getOtherSensorsCount(...)
 *     getOtherSensorsDataRecord(...)
 *     下面给出了弱定义中的示例内容
 *     在你的 phosphor-ipmi-host bbappend 文件中启用 'sensors-oem'
 *       'EXTRA_OEMESON:append = " -Dsensors-oem=enabled"'
 *  之后 sensorcommands_oem.cpp 中的内容将覆盖
 *  下面提供的代码。 */

size_t getOtherSensorsCount(ipmi::Context::ptr ctx) __attribute__((weak));
size_t getOtherSensorsCount(ipmi::Context::ptr ctx)
{
    size_t fruCount = 0;

    ipmi::Cc ret = ipmi::storage::getFruSdrCount(ctx, fruCount);
    if (ret != ipmi::ccSuccess)
    {
        lg2::error("getOtherSensorsCount: getFruSdrCount error");
        return std::numeric_limits<size_t>::max();
    }

    const auto& entityRecords =
        ipmi::sensor::EntityInfoMapContainer::getContainer()
            ->getIpmiEntityRecords();
    size_t entityCount = entityRecords.size();

    return fruCount + ipmi::storage::type12Count + entityCount;
}

int getOtherSensorsDataRecord(ipmi::Context::ptr ctx, uint16_t recordID,
                              std::vector<uint8_t>& recordData)
    __attribute__((weak));
int getOtherSensorsDataRecord(ipmi::Context::ptr ctx, uint16_t recordID,
                              std::vector<uint8_t>& recordData)
{
    size_t otherCount{ipmi::sensor::getOtherSensorsCount(ctx)};
    if (otherCount == std::numeric_limits<size_t>::max())
    {
        return GENERAL_ERROR;
    }
    const auto& entityRecords =
        ipmi::sensor::EntityInfoMapContainer::getContainer()
            ->getIpmiEntityRecords();

    size_t sdrIndex(recordID - ipmi::getNumberOfSensors());
    size_t entityCount{entityRecords.size()};
    size_t fruCount{otherCount - ipmi::storage::type12Count - entityCount};

    if (sdrIndex > otherCount)
    {
        return std::numeric_limits<int>::min();
    }
    else if (sdrIndex >= fruCount + ipmi::storage::type12Count)
    {
        // handle type 8 entity map records
        // 处理 type 8 实体映射记录
        ipmi::sensor::EntityInfoMap::const_iterator entity =
            entityRecords.find(static_cast<uint8_t>(
                sdrIndex - fruCount - ipmi::storage::type12Count));

        if (entity == entityRecords.end())
        {
            return GENERAL_ERROR;
        }
        recordData = ipmi::storage::getType8SDRs(entity, recordID);
    }
    else if (sdrIndex >= fruCount)
    {
        // handle type 12 hardcoded records
        // 处理 type 12 硬编码记录
        size_t type12Index = sdrIndex - fruCount;
        if (type12Index >= ipmi::storage::type12Count)
        {
            lg2::error("getSensorDataRecord: type12Index error");
            return GENERAL_ERROR;
        }
        recordData = ipmi::storage::getType12SDRs(type12Index, recordID);
    }
    else
    {
        // handle fru records
        // 处理 FRU 记录
        get_sdr::SensorDataFruRecord data;
        if (ipmi::Cc ret = ipmi::storage::getFruSdrs(ctx, sdrIndex, data);
            ret != ipmi::ccSuccess)
        {
            return GENERAL_ERROR;
        }
        data.header.recordId = recordID;
        recordData.insert(recordData.end(), reinterpret_cast<uint8_t*>(&data),
                          reinterpret_cast<uint8_t*>(&data) + sizeof(data));
    }

    return 0;
}

} // namespace sensor

ipmi::RspType<> ipmiSenPlatformEvent(ipmi::Context::ptr ctx,
                                     ipmi::message::Payload& p)
{
    constexpr const uint8_t validEnvmRev = 0x04;
    constexpr const uint8_t lastSensorType = 0x2C;
    constexpr const uint8_t oemReserved = 0xC0;

    uint8_t sysgeneratorID = 0;
    uint8_t evmRev = 0;
    uint8_t sensorType = 0;
    uint8_t sensorNum = 0;
    uint8_t eventType = 0;
    uint8_t eventData1 = 0;
    std::optional<uint8_t> eventData2 = 0;
    std::optional<uint8_t> eventData3 = 0;
    [[maybe_unused]] uint16_t generatorID = 0;
    ipmi::ChannelInfo chInfo;

    if (ipmi::getChannelInfo(ctx->channel, chInfo) != ipmi::ccSuccess)
    {
        lg2::error("Failed to get Channel Info, channel: {CHANNEL}", "CHANNEL",
                   ctx->channel);
        return ipmi::responseUnspecifiedError();
    }

    if (static_cast<ipmi::EChannelMediumType>(chInfo.mediumType) ==
        ipmi::EChannelMediumType::systemInterface)
    {
        p.unpack(sysgeneratorID, evmRev, sensorType, sensorNum, eventType,
                 eventData1, eventData2, eventData3);
        constexpr const uint8_t isSoftwareID = 0x01;
        if (!(sysgeneratorID & isSoftwareID))
        {
            return ipmi::responseInvalidFieldRequest();
        }
        // Refer to IPMI Spec Table 32: SEL Event Records
        // 参见 IPMI 规范表 32：SEL Event Records
        generatorID = (ctx->channel << 12) // Channel
                      | (0x0 << 10)        // Reserved
                      | (0x0 << 8)         // 0x0 for sys-soft ID
                      | sysgeneratorID;
    }
    else
    {
        p.unpack(evmRev, sensorType, sensorNum, eventType, eventData1,
                 eventData2, eventData3);
        // Refer to IPMI Spec Table 32: SEL Event Records
        // 参见 IPMI 规范表 32：SEL Event Records
        generatorID = (ctx->channel << 12)      // Channel
                      | (0x0 << 10)             // Reserved
                      | ((ctx->lun & 0x3) << 8) // Lun
                      | (ctx->rqSA << 1);
    }

    if (!p.fullyUnpacked())
    {
        return ipmi::responseReqDataLenInvalid();
    }

    // Check for valid evmRev and Sensor Type(per Table 42 of spec)
    // 检查有效的 evmRev 和 Sensor Type（参见规范表 42）
    if (evmRev != validEnvmRev)
    {
        return ipmi::responseInvalidFieldRequest();
    }
    if ((sensorType > lastSensorType) && (sensorType < oemReserved))
    {
        return ipmi::responseInvalidFieldRequest();
    }

    return ipmi::responseSuccess();
}

ipmi::RspType<> ipmiSetSensorReading(
    ipmi::Context::ptr ctx, uint8_t sensorNumber, uint8_t, uint8_t reading,
    uint15_t assertOffset, bool, uint15_t, bool, uint8_t, uint8_t, uint8_t)
{
    std::string connection;
    std::string path;
    std::vector<std::string> interfaces;

    ipmi::Cc status =
        getSensorConnection(ctx, sensorNumber, connection, path, &interfaces);
    if (status)
    {
        return ipmi::response(status);
    }

    // we can tell the sensor type by its interface type
    // 可以通过接口类型判断传感器类型
    if (std::find(interfaces.begin(), interfaces.end(),
                  SensorValue::interface) != interfaces.end())
    {
        DbusInterfaceMap sensorMap;
        if (!getSensorMap(ctx, connection, path, sensorMap))
        {
            return ipmi::responseResponseError();
        }
        auto sensorObject = sensorMap.find(SensorValue::interface);
        if (sensorObject == sensorMap.end())
        {
            return ipmi::responseResponseError();
        }

        // Only allow external SetSensor if write permission granted
        // 只有在授予写权限时才允许外部 SetSensor
        if (!details::sdrWriteTable.getWritePermission(
                (ctx->lun << 8) | sensorNumber))
        {
            return ipmi::responseResponseError();
        }

        auto value =
            sensor::calculateValue(reading, sensorMap, sensorObject->second);
        if (!value)
        {
            return ipmi::responseResponseError();
        }

        if constexpr (debug)
        {
            lg2::info("IPMI SET_SENSOR, sensor number: {SENSOR_NUM}, "
                      "byte: {BYTE}, value: {VALUE}",
                      "SENSOR_NUM", sensorNumber, "BYTE", (unsigned int)reading,
                      "VALUE", *value);
        }

        boost::system::error_code ec = setDbusProperty(
            ctx, connection, path, SensorValue::interface,
            SensorValue::property_names::value, ipmi::Value(*value));

        // setDbusProperty intended to resolve dbus exception/rc within the
        // function but failed to achieve that. Catch exception in the ipmi
        // callback functions for now (e.g. ipmiSetSensorReading).
        // setDbusProperty 本意是在函数内部处理 dbus 异常/返回码，
        // 但未能实现。目前请在 ipmi 回调函数中
        // 捕获异常（例如 ipmiSetSensorReading）。
        if (ec)
        {
            lg2::error("Failed to set Value, path: {PATH}, "
                       "interface: {INTERFACE}, ERROR: {ERROR}",
                       "PATH", path, "INTERFACE", SensorValue::interface,
                       "ERROR", ec.message());
            return ipmi::responseResponseError();
        }
        return ipmi::responseSuccess();
    }

    if (std::find(interfaces.begin(), interfaces.end(), sensor::vrInterface) !=
        interfaces.end())
    {
        DbusInterfaceMap sensorMap;
        if (!getSensorMap(ctx, connection, path, sensorMap))
        {
            return ipmi::responseResponseError();
        }
        auto sensorObject = sensorMap.find(sensor::vrInterface);
        if (sensorObject == sensorMap.end())
        {
            return ipmi::responseResponseError();
        }

        // VR sensors are treated as a special case and we will not check the
        // write permission for VR sensors, since they always deemed writable
        // and permission table are not applied to VR sensors.
        // VR 传感器作为特例处理，我们不检查 VR 传感器的写权限，
        // 因为它们始终被视为可写，
        // 并且权限表不适用于 VR 传感器。
        auto vrMode =
            sensor::calculateVRMode(assertOffset, sensorObject->second);
        if (!vrMode)
        {
            return ipmi::responseResponseError();
        }
        boost::system::error_code ec = setDbusProperty(
            ctx, connection, path, sensor::vrInterface, "Selected", *vrMode);
        // setDbusProperty intended to resolve dbus exception/rc within the
        // function but failed to achieve that. Catch exception in the ipmi
        // callback functions for now (e.g. ipmiSetSensorReading).
        // setDbusProperty 本意是在函数内部处理 dbus 异常/返回码，
        // 但未能实现。目前请在 ipmi 回调函数中
        // 捕获异常（例如 ipmiSetSensorReading）。
        if (ec)
        {
            lg2::error("Failed to set Selected, path: {PATH}, "
                       "interface: {INTERFACE}, ERROR: {ERROR}",
                       "PATH", path, "INTERFACE", SensorValue::interface,
                       "ERROR", ec.message());
        }
        return ipmi::responseSuccess();
    }

    lg2::error("unknown sensor type, path: {PATH}", "PATH", path);
    return ipmi::responseResponseError();
}

ipmi::RspType<uint8_t, uint8_t, uint8_t, std::optional<uint8_t>>
    ipmiSenGetSensorReading(ipmi::Context::ptr ctx, uint8_t sensnum)
{
    std::string connection;
    std::string path;

    if (sensnum == reservedSensorNumber)
    {
        return ipmi::responseInvalidFieldRequest();
    }

    auto status = getSensorConnection(ctx, sensnum, connection, path);
    if (status)
    {
        return ipmi::response(status);
    }

#ifdef FEATURE_HYBRID_SENSORS
    if (auto sensor = findStaticSensor(path);
        sensor != ipmi::sensor::sensors.end() &&
        getSensorEventTypeFromPath(path) !=
            static_cast<uint8_t>(SensorEventTypeCodes::threshold))
    {
        if (ipmi::sensor::Mutability::Read !=
            (sensor->second.mutability & ipmi::sensor::Mutability::Read))
        {
            return ipmi::responseIllegalCommand();
        }

        uint8_t operation = 0;
        try
        {
            ipmi::sensor::GetSensorResponse getResponse =
                sensor->second.getFunc(sensor->second);

            if (getResponse.readingOrStateUnavailable)
            {
                operation |= static_cast<uint8_t>(
                    IPMISensorReadingByte2::readingStateUnavailable);
            }
            if (getResponse.scanningEnabled)
            {
                operation |= static_cast<uint8_t>(
                    IPMISensorReadingByte2::sensorScanningEnable);
            }
            if (getResponse.allEventMessagesEnabled)
            {
                operation |= static_cast<uint8_t>(
                    IPMISensorReadingByte2::eventMessagesEnable);
            }
            return ipmi::responseSuccess(
                getResponse.reading, operation,
                getResponse.thresholdLevelsStates,
                getResponse.discreteReadingSensorStates);
        }
        catch (const std::exception& e)
        {
            operation |= static_cast<uint8_t>(
                IPMISensorReadingByte2::readingStateUnavailable);
            return ipmi::responseSuccess(0, operation, 0, std::nullopt);
        }
    }
#endif

    DbusInterfaceMap sensorMap;
    if (!getSensorMap(ctx, connection, path, sensorMap))
    {
        return ipmi::responseResponseError();
    }
    auto sensorObject = sensorMap.find(SensorValue::interface);

    if (sensorObject == sensorMap.end() ||
        sensorObject->second.find(SensorValue::property_names::value) ==
            sensorObject->second.end())
    {
        return ipmi::responseResponseError();
    }
    auto& valueVariant =
        sensorObject->second[SensorValue::property_names::value];
    double reading = std::visit(VariantToDoubleVisitor(), valueVariant);

    double max = 0;
    double min = 0;
    getSensorMaxMin(sensorMap, max, min);

    int16_t mValue = 0;
    int16_t bValue = 0;
    int8_t rExp = 0;
    int8_t bExp = 0;
    bool bSigned = false;

    if (!getSensorAttributes(max, min, mValue, rExp, bValue, bExp, bSigned))
    {
        return ipmi::responseResponseError();
    }

    uint8_t value =
        scaleIPMIValueFromDouble(reading, mValue, rExp, bValue, bExp, bSigned);
    uint8_t operation =
        static_cast<uint8_t>(IPMISensorReadingByte2::sensorScanningEnable);
    operation |=
        static_cast<uint8_t>(IPMISensorReadingByte2::eventMessagesEnable);
    bool notReading = std::isnan(reading);

    if (!notReading)
    {
        auto availableObject =
            sensorMap.find("xyz.openbmc_project.State.Decorator.Availability");
        if (availableObject != sensorMap.end())
        {
            auto findAvailable = availableObject->second.find("Available");
            if (findAvailable != availableObject->second.end())
            {
                bool* available = std::get_if<bool>(&(findAvailable->second));
                if (available && !(*available))
                {
                    notReading = true;
                }
            }
        }
    }

    if (notReading)
    {
        operation |= static_cast<uint8_t>(
            IPMISensorReadingByte2::readingStateUnavailable);
    }

    if constexpr (details::enableInstrumentation)
    {
        int byteValue;
        if (bSigned)
        {
            byteValue = static_cast<int>(static_cast<int8_t>(value));
        }
        else
        {
            byteValue = static_cast<int>(static_cast<uint8_t>(value));
        }

        // Keep stats on the reading just obtained, even if it is "NaN"
        // 保留刚获得读数的统计信息，即使它是 "NaN"
        if (details::sdrStatsTable.updateReading((ctx->lun << 8) | sensnum,
                                                 reading, byteValue))
        {
            // This is the first reading, show the coefficients
            // 这是第一次读数，显示系数
            double step = (max - min) / 255.0;
            lg2::error(
                "IPMI sensor {NAME}: Range min={MIN} max={MAX}, step={STEP}, "
                "Coefficients mValue={MVALUE} rExp={REXP} bValue={BVALUE} "
                "bExp={BEXP} bSigned={BSIGNED}",
                "NAME",
                details::sdrStatsTable.getName((ctx->lun << 8) | sensnum),
                "MIN", min, "MAX", max, "STEP", step, "MVALUE", mValue, "REXP",
                rExp, "BVALUE", bValue, "BEXP", bExp, "BSIGNED", bSigned);
        }
    }

    uint8_t thresholds = 0;

    auto warningObject = sensorMap.find(SensorThresholdWarning::interface);
    if (warningObject != sensorMap.end())
    {
        auto alarmHigh = warningObject->second.find(
            SensorThresholdWarning::property_names::warning_alarm_high);
        auto alarmLow = warningObject->second.find(
            SensorThresholdWarning::property_names::warning_alarm_low);
        if (alarmHigh != warningObject->second.end())
        {
            if (std::get<bool>(alarmHigh->second))
            {
                thresholds |= static_cast<uint8_t>(
                    IPMISensorReadingByte3::upperNonCritical);
            }
        }
        if (alarmLow != warningObject->second.end())
        {
            if (std::get<bool>(alarmLow->second))
            {
                thresholds |= static_cast<uint8_t>(
                    IPMISensorReadingByte3::lowerNonCritical);
            }
        }
    }

    auto criticalObject = sensorMap.find(SensorThresholdCritical::interface);
    if (criticalObject != sensorMap.end())
    {
        auto alarmHigh = criticalObject->second.find(
            SensorThresholdCritical::property_names::critical_alarm_high);
        auto alarmLow = criticalObject->second.find(
            SensorThresholdCritical::property_names::critical_alarm_low);
        if (alarmHigh != criticalObject->second.end())
        {
            if (std::get<bool>(alarmHigh->second))
            {
                thresholds |=
                    static_cast<uint8_t>(IPMISensorReadingByte3::upperCritical);
            }
        }
        if (alarmLow != criticalObject->second.end())
        {
            if (std::get<bool>(alarmLow->second))
            {
                thresholds |=
                    static_cast<uint8_t>(IPMISensorReadingByte3::lowerCritical);
            }
        }
    }

    // no discrete as of today so optional byte is never returned
    // 目前没有离散量，因此从不返回可选字节
    return ipmi::responseSuccess(value, operation, thresholds, std::nullopt);
}

/** @brief implements the Set Sensor threshold command
 *  @param sensorNumber        - sensor number
 *  @param lowerNonCriticalThreshMask
 *  @param lowerCriticalThreshMask
 *  @param lowerNonRecovThreshMask
 *  @param upperNonCriticalThreshMask
 *  @param upperCriticalThreshMask
 *  @param upperNonRecovThreshMask
 *  @param reserved
 *  @param lowerNonCritical    - lower non-critical threshold
 *  @param lowerCritical       - Lower critical threshold
 *  @param lowerNonRecoverable - Lower non-recoverable threshold
 *  @param upperNonCritical    - Upper non-critical threshold
 *  @param upperCritical       - Upper critical
 *  @param upperNonRecoverable - Upper Non-recoverable
 *
 *  @returns IPMI completion code
 */
/* @brief 实现 Set Sensor threshold 命令
 *  @param sensorNumber - 传感器编号
 *  @param lowerNonCriticalThreshMask
 *  @param lowerCriticalThreshMask
 *  @param lowerNonRecovThreshMask
 *  @param upperNonCriticalThreshMask
 *  @param upperCriticalThreshMask
 *  @param upperNonRecovThreshMask
 *  @param reserved
 *  @param lowerNonCritical - 下限非关键阈值
 *  @param lowerCritical - 下限关键阈值
 *  @param lowerNonRecoverable - 下限不可恢复阈值
 *  @param upperNonCritical - 上限非关键阈值
 *  @param upperCritical - 上限关键阈值
 *  @param upperNonRecoverable - 上限不可恢复阈值
 *  @returns IPMI 完成码 */
ipmi::RspType<> ipmiSenSetSensorThresholds(
    ipmi::Context::ptr ctx, uint8_t sensorNum, bool lowerNonCriticalThreshMask,
    bool lowerCriticalThreshMask, bool lowerNonRecovThreshMask,
    bool upperNonCriticalThreshMask, bool upperCriticalThreshMask,
    bool upperNonRecovThreshMask, uint2_t reserved, uint8_t lowerNonCritical,
    uint8_t lowerCritical, [[maybe_unused]] uint8_t lowerNonRecoverable,
    uint8_t upperNonCritical, uint8_t upperCritical,
    [[maybe_unused]] uint8_t upperNonRecoverable)
{
    if (sensorNum == reservedSensorNumber || reserved)
    {
        return ipmi::responseInvalidFieldRequest();
    }

    // lower nc and upper nc not supported on any sensor
    // 任何传感器都不支持 lower nc 和 upper nc
    if (lowerNonRecovThreshMask || upperNonRecovThreshMask)
    {
        return ipmi::responseInvalidFieldRequest();
    }

    // if none of the threshold mask are set, nothing to do
    // 如果没有设置任何阈值掩码，则无需操作
    if (!(lowerNonCriticalThreshMask | lowerCriticalThreshMask |
          lowerNonRecovThreshMask | upperNonCriticalThreshMask |
          upperCriticalThreshMask | upperNonRecovThreshMask))
    {
        return ipmi::responseSuccess();
    }

    std::string connection;
    std::string path;

    ipmi::Cc status = getSensorConnection(ctx, sensorNum, connection, path);
    if (status)
    {
        return ipmi::response(status);
    }
    DbusInterfaceMap sensorMap;
    if (!getSensorMap(ctx, connection, path, sensorMap))
    {
        return ipmi::responseResponseError();
    }

    double max = 0;
    double min = 0;
    getSensorMaxMin(sensorMap, max, min);

    int16_t mValue = 0;
    int16_t bValue = 0;
    int8_t rExp = 0;
    int8_t bExp = 0;
    bool bSigned = false;

    if (!getSensorAttributes(max, min, mValue, rExp, bValue, bExp, bSigned))
    {
        return ipmi::responseResponseError();
    }

    // store a vector of property name, value to set, and interface
    // 存储属性名、要设置的值和接口的 vector
    std::vector<std::tuple<std::string, uint8_t, std::string>> thresholdsToSet;

    // define the indexes of the tuple
    // 定义 tuple 的索引
    constexpr uint8_t propertyName = 0;
    constexpr uint8_t thresholdValue = 1;
    constexpr uint8_t interface = 2;
    // verify all needed fields are present
    // 验证所有必需字段都存在
    if (lowerCriticalThreshMask || upperCriticalThreshMask)
    {
        auto findThreshold = sensorMap.find(SensorThresholdCritical::interface);
        if (findThreshold == sensorMap.end())
        {
            return ipmi::responseInvalidFieldRequest();
        }
        if (lowerCriticalThreshMask)
        {
            auto findLower = findThreshold->second.find(
                SensorThresholdCritical::property_names::critical_low);
            if (findLower == findThreshold->second.end())
            {
                return ipmi::responseInvalidFieldRequest();
            }
            thresholdsToSet.emplace_back(
                SensorThresholdCritical::property_names::critical_low,
                lowerCritical, findThreshold->first);
        }
        if (upperCriticalThreshMask)
        {
            auto findUpper = findThreshold->second.find(
                SensorThresholdCritical::property_names::critical_high);
            if (findUpper == findThreshold->second.end())
            {
                return ipmi::responseInvalidFieldRequest();
            }
            thresholdsToSet.emplace_back(
                SensorThresholdCritical::property_names::critical_high,
                upperCritical, findThreshold->first);
        }
    }
    if (lowerNonCriticalThreshMask || upperNonCriticalThreshMask)
    {
        auto findThreshold = sensorMap.find(SensorThresholdWarning::interface);
        if (findThreshold == sensorMap.end())
        {
            return ipmi::responseInvalidFieldRequest();
        }
        if (lowerNonCriticalThreshMask)
        {
            auto findLower = findThreshold->second.find(
                SensorThresholdWarning::property_names::warning_low);
            if (findLower == findThreshold->second.end())
            {
                return ipmi::responseInvalidFieldRequest();
            }
            thresholdsToSet.emplace_back(
                SensorThresholdWarning::property_names::warning_low,
                lowerNonCritical, findThreshold->first);
        }
        if (upperNonCriticalThreshMask)
        {
            auto findUpper = findThreshold->second.find(
                SensorThresholdWarning::property_names::warning_high);
            if (findUpper == findThreshold->second.end())
            {
                return ipmi::responseInvalidFieldRequest();
            }
            thresholdsToSet.emplace_back(
                SensorThresholdWarning::property_names::warning_high,
                upperNonCritical, findThreshold->first);
        }
    }
    for (const auto& property : thresholdsToSet)
    {
        // from section 36.3 in the IPMI Spec, assume all linear
        // 根据 IPMI 规范 36.3 节，假设全部为线性
        double valueToSet = ((mValue * std::get<thresholdValue>(property)) +
                             (bValue * std::pow(10.0, bExp))) *
                            std::pow(10.0, rExp);
        setDbusProperty(
            *getSdBus(), connection, path, std::get<interface>(property),
            std::get<propertyName>(property), ipmi::Value(valueToSet));
    }
    return ipmi::responseSuccess();
}

IPMIThresholds getIPMIThresholds(const DbusInterfaceMap& sensorMap)
{
    IPMIThresholds resp;
    auto warningInterface = sensorMap.find(SensorThresholdWarning::interface);
    auto criticalInterface = sensorMap.find(SensorThresholdCritical::interface);

    if ((warningInterface != sensorMap.end()) ||
        (criticalInterface != sensorMap.end()))
    {
        auto sensorPair = sensorMap.find(SensorValue::interface);

        if (sensorPair == sensorMap.end())
        {
            // should not have been able to find a sensor not implementing
            // the sensor object
            // 本不应该找到未实现
            // 传感器对象的传感器
            throw std::runtime_error("Invalid sensor map");
        }

        double max = 0;
        double min = 0;
        getSensorMaxMin(sensorMap, max, min);

        int16_t mValue = 0;
        int16_t bValue = 0;
        int8_t rExp = 0;
        int8_t bExp = 0;
        bool bSigned = false;

        if (!getSensorAttributes(max, min, mValue, rExp, bValue, bExp, bSigned))
        {
            throw std::runtime_error("Invalid sensor attributes");
        }
        if (warningInterface != sensorMap.end())
        {
            auto& warningMap = warningInterface->second;

            auto warningHigh = warningMap.find(
                SensorThresholdWarning::property_names::warning_high);
            auto warningLow = warningMap.find(
                SensorThresholdWarning::property_names::warning_low);

            if (warningHigh != warningMap.end())
            {
                double value =
                    std::visit(VariantToDoubleVisitor(), warningHigh->second);
                if (std::isfinite(value))
                {
                    resp.warningHigh = scaleIPMIValueFromDouble(
                        value, mValue, rExp, bValue, bExp, bSigned);
                }
            }
            if (warningLow != warningMap.end())
            {
                double value =
                    std::visit(VariantToDoubleVisitor(), warningLow->second);
                if (std::isfinite(value))
                {
                    resp.warningLow = scaleIPMIValueFromDouble(
                        value, mValue, rExp, bValue, bExp, bSigned);
                }
            }
        }
        if (criticalInterface != sensorMap.end())
        {
            auto& criticalMap = criticalInterface->second;

            auto criticalHigh = criticalMap.find(
                SensorThresholdCritical::property_names::critical_high);
            auto criticalLow = criticalMap.find(
                SensorThresholdCritical::property_names::critical_low);

            if (criticalHigh != criticalMap.end())
            {
                double value =
                    std::visit(VariantToDoubleVisitor(), criticalHigh->second);
                if (std::isfinite(value))
                {
                    resp.criticalHigh = scaleIPMIValueFromDouble(
                        value, mValue, rExp, bValue, bExp, bSigned);
                }
            }
            if (criticalLow != criticalMap.end())
            {
                double value =
                    std::visit(VariantToDoubleVisitor(), criticalLow->second);
                if (std::isfinite(value))
                {
                    resp.criticalLow = scaleIPMIValueFromDouble(
                        value, mValue, rExp, bValue, bExp, bSigned);
                }
            }
        }
    }
    return resp;
}

ipmi::RspType<uint8_t, // readable
              uint8_t, // lowerNCrit
              uint8_t, // lowerCrit
              uint8_t, // lowerNrecoverable
              uint8_t, // upperNC
              uint8_t, // upperCrit
              uint8_t> // upperNRecoverable
    ipmiSenGetSensorThresholds(ipmi::Context::ptr ctx, uint8_t sensorNumber)
{
    std::string connection;
    std::string path;

    if (sensorNumber == reservedSensorNumber)
    {
        return ipmi::responseInvalidFieldRequest();
    }

    auto status = getSensorConnection(ctx, sensorNumber, connection, path);
    if (status)
    {
        return ipmi::response(status);
    }

    DbusInterfaceMap sensorMap;
    if (!getSensorMap(ctx, connection, path, sensorMap))
    {
        return ipmi::responseResponseError();
    }

    IPMIThresholds thresholdData;
    try
    {
        thresholdData = getIPMIThresholds(sensorMap);
    }
    catch (const std::exception&)
    {
        return ipmi::responseResponseError();
    }

    uint8_t readable = 0;
    uint8_t lowerNC = 0;
    uint8_t lowerCritical = 0;
    uint8_t lowerNonRecoverable = 0;
    uint8_t upperNC = 0;
    uint8_t upperCritical = 0;
    uint8_t upperNonRecoverable = 0;

    if (thresholdData.warningHigh)
    {
        readable |=
            1 << static_cast<uint8_t>(IPMIThresholdRespBits::upperNonCritical);
        upperNC = *thresholdData.warningHigh;
    }
    if (thresholdData.warningLow)
    {
        readable |=
            1 << static_cast<uint8_t>(IPMIThresholdRespBits::lowerNonCritical);
        lowerNC = *thresholdData.warningLow;
    }

    if (thresholdData.criticalHigh)
    {
        readable |=
            1 << static_cast<uint8_t>(IPMIThresholdRespBits::upperCritical);
        upperCritical = *thresholdData.criticalHigh;
    }
    if (thresholdData.criticalLow)
    {
        readable |=
            1 << static_cast<uint8_t>(IPMIThresholdRespBits::lowerCritical);
        lowerCritical = *thresholdData.criticalLow;
    }

    return ipmi::responseSuccess(readable, lowerNC, lowerCritical,
                                 lowerNonRecoverable, upperNC, upperCritical,
                                 upperNonRecoverable);
}

/** @brief implements the get Sensor event enable command
 *  @param sensorNumber - sensor number
 *
 *  @returns IPMI completion code plus response data
 *   - enabled               - Sensor Event messages
 *   - assertionEnabledLsb   - Assertion event messages
 *   - assertionEnabledMsb   - Assertion event messages
 *   - deassertionEnabledLsb - Deassertion event messages
 *   - deassertionEnabledMsb - Deassertion event messages
 */
/* @brief 实现 get Sensor event enable 命令
 *  @param sensorNumber - 传感器编号
 *  @returns IPMI 完成码加上响应数据
 *   - enabled - 传感器事件消息
 *   - assertionEnabledLsb - 断言事件消息
 *   - assertionEnabledMsb - 断言事件消息
 *   - deassertionEnabledLsb - 去断言事件消息
 *   - deassertionEnabledMsb - 去断言事件消息 */

ipmi::RspType<uint8_t, // enabled
              uint8_t, // assertionEnabledLsb
              uint8_t, // assertionEnabledMsb
              uint8_t, // deassertionEnabledLsb
              uint8_t> // deassertionEnabledMsb
    ipmiSenGetSensorEventEnable(ipmi::Context::ptr ctx, uint8_t sensorNum)
{
    std::string connection;
    std::string path;

    uint8_t enabled = 0;
    uint8_t assertionEnabledLsb = 0;
    uint8_t assertionEnabledMsb = 0;
    uint8_t deassertionEnabledLsb = 0;
    uint8_t deassertionEnabledMsb = 0;

    if (sensorNum == reservedSensorNumber)
    {
        return ipmi::responseInvalidFieldRequest();
    }

    auto status = getSensorConnection(ctx, sensorNum, connection, path);
    if (status)
    {
        return ipmi::response(status);
    }

#ifdef FEATURE_HYBRID_SENSORS
    if (auto sensor = findStaticSensor(path);
        sensor != ipmi::sensor::sensors.end() &&
        getSensorEventTypeFromPath(path) !=
            static_cast<uint8_t>(SensorEventTypeCodes::threshold))
    {
        enabled = static_cast<uint8_t>(
            IPMISensorEventEnableByte2::sensorScanningEnable);
        uint16_t assertionEnabled = 0;
        for (auto& offsetValMap : sensor->second.propertyInterfaces.begin()
                                      ->second.begin()
                                      ->second.second)
        {
            assertionEnabled |= (1 << offsetValMap.first);
        }
        assertionEnabledLsb = static_cast<uint8_t>((assertionEnabled & 0xFF));
        assertionEnabledMsb =
            static_cast<uint8_t>(((assertionEnabled >> 8) & 0xFF));

        return ipmi::responseSuccess(enabled, assertionEnabledLsb,
                                     assertionEnabledMsb, deassertionEnabledLsb,
                                     deassertionEnabledMsb);
    }
#endif

    DbusInterfaceMap sensorMap;
    if (!getSensorMap(ctx, connection, path, sensorMap))
    {
        return ipmi::responseResponseError();
    }

    auto warningInterface = sensorMap.find(SensorThresholdWarning::interface);
    auto criticalInterface = sensorMap.find(SensorThresholdCritical::interface);
    if ((warningInterface != sensorMap.end()) ||
        (criticalInterface != sensorMap.end()))
    {
        enabled = static_cast<uint8_t>(
            IPMISensorEventEnableByte2::sensorScanningEnable);
        if (warningInterface != sensorMap.end())
        {
            auto& warningMap = warningInterface->second;

            auto warningHigh = warningMap.find(
                SensorThresholdWarning::property_names::warning_high);
            auto warningLow = warningMap.find(
                SensorThresholdWarning::property_names::warning_low);
            if (warningHigh != warningMap.end())
            {
                double value =
                    std::visit(VariantToDoubleVisitor(), warningHigh->second);
                if (std::isfinite(value))
                {
                    assertionEnabledLsb |= static_cast<uint8_t>(
                        IPMISensorEventEnableThresholds::
                            upperNonCriticalGoingHigh);
                    deassertionEnabledLsb |= static_cast<uint8_t>(
                        IPMISensorEventEnableThresholds::
                            upperNonCriticalGoingLow);
                }
            }
            if (warningLow != warningMap.end())
            {
                double value =
                    std::visit(VariantToDoubleVisitor(), warningLow->second);
                if (std::isfinite(value))
                {
                    assertionEnabledLsb |= static_cast<uint8_t>(
                        IPMISensorEventEnableThresholds::
                            lowerNonCriticalGoingLow);
                    deassertionEnabledLsb |= static_cast<uint8_t>(
                        IPMISensorEventEnableThresholds::
                            lowerNonCriticalGoingHigh);
                }
            }
        }
        if (criticalInterface != sensorMap.end())
        {
            auto& criticalMap = criticalInterface->second;

            auto criticalHigh = criticalMap.find(
                SensorThresholdCritical::property_names::critical_high);
            auto criticalLow = criticalMap.find(
                SensorThresholdCritical::property_names::critical_low);

            if (criticalHigh != criticalMap.end())
            {
                double value =
                    std::visit(VariantToDoubleVisitor(), criticalHigh->second);
                if (std::isfinite(value))
                {
                    assertionEnabledMsb |= static_cast<uint8_t>(
                        IPMISensorEventEnableThresholds::
                            upperCriticalGoingHigh);
                    deassertionEnabledMsb |= static_cast<uint8_t>(
                        IPMISensorEventEnableThresholds::upperCriticalGoingLow);
                }
            }
            if (criticalLow != criticalMap.end())
            {
                double value =
                    std::visit(VariantToDoubleVisitor(), criticalLow->second);
                if (std::isfinite(value))
                {
                    assertionEnabledLsb |= static_cast<uint8_t>(
                        IPMISensorEventEnableThresholds::lowerCriticalGoingLow);
                    deassertionEnabledLsb |= static_cast<uint8_t>(
                        IPMISensorEventEnableThresholds::
                            lowerCriticalGoingHigh);
                }
            }
        }
    }

    return ipmi::responseSuccess(enabled, assertionEnabledLsb,
                                 assertionEnabledMsb, deassertionEnabledLsb,
                                 deassertionEnabledMsb);
}

/** @brief implements the get Sensor event status command
 *  @param sensorNumber - sensor number, FFh = reserved
 *
 *  @returns IPMI completion code plus response data
 *   - sensorEventStatus - Sensor Event messages state
 *   - assertions        - Assertion event messages
 *   - deassertions      - Deassertion event messages
 */
/* @brief 实现 get Sensor event status 命令
 *  @param sensorNumber - 传感器编号，FFh = 保留
 *  @returns IPMI 完成码加上响应数据
 *   - sensorEventStatus - 传感器事件消息状态
 *   - assertions - 断言事件消息
 *   - deassertions - 去断言事件消息 */
ipmi::RspType<uint8_t,         // sensorEventStatus
              std::bitset<16>, // assertions
              std::bitset<16>  // deassertion
              >
    ipmiSenGetSensorEventStatus(ipmi::Context::ptr ctx, uint8_t sensorNum)
{
    if (sensorNum == reservedSensorNumber)
    {
        return ipmi::responseInvalidFieldRequest();
    }

    std::string connection;
    std::string path;
    auto status = getSensorConnection(ctx, sensorNum, connection, path);
    if (status)
    {
        lg2::error("ipmiSenGetSensorEventStatus: Sensor connection Error, "
                   "sensor number: {SENSOR_NUM}",
                   "SENSOR_NUM", sensorNum);
        return ipmi::response(status);
    }

#ifdef FEATURE_HYBRID_SENSORS
    if (auto sensor = findStaticSensor(path);
        sensor != ipmi::sensor::sensors.end() &&
        getSensorEventTypeFromPath(path) !=
            static_cast<uint8_t>(SensorEventTypeCodes::threshold))
    {
        auto response = ipmi::sensor::get::mapDbusToAssertion(
            sensor->second, path, sensor->second.sensorInterface);
        std::bitset<16> assertions;
        // deassertions are not used.
        // 不使用去断言（deassertions）。
        std::bitset<16> deassertions = 0;
        uint8_t sensorEventStatus = 0;
        if (response.readingOrStateUnavailable)
        {
            sensorEventStatus |= static_cast<uint8_t>(
                IPMISensorReadingByte2::readingStateUnavailable);
        }
        if (response.scanningEnabled)
        {
            sensorEventStatus |= static_cast<uint8_t>(
                IPMISensorReadingByte2::sensorScanningEnable);
        }
        if (response.allEventMessagesEnabled)
        {
            sensorEventStatus |= static_cast<uint8_t>(
                IPMISensorReadingByte2::eventMessagesEnable);
        }
        assertions |= response.discreteReadingSensorStates << 8;
        assertions |= response.thresholdLevelsStates;
        return ipmi::responseSuccess(sensorEventStatus, assertions,
                                     deassertions);
    }
#endif

    DbusInterfaceMap sensorMap;
    if (!getSensorMap(ctx, connection, path, sensorMap))
    {
        lg2::error("ipmiSenGetSensorEventStatus: Sensor Mapping Error, "
                   "sensor path: {SENSOR_PATH}",
                   "SENSOR_PATH", path);
        return ipmi::responseResponseError();
    }

    uint8_t sensorEventStatus =
        static_cast<uint8_t>(IPMISensorEventEnableByte2::sensorScanningEnable);
    std::bitset<16> assertions = 0;
    std::bitset<16> deassertions = 0;

    // handle VR typed sensor
    // 处理 VR 类型传感器
    auto vrInterface = sensorMap.find(sensor::vrInterface);
    if (vrInterface != sensorMap.end())
    {
        if (!sensor::getVrEventStatus(ctx, connection, path,
                                      vrInterface->second, assertions))
        {
            return ipmi::responseResponseError();
        }

        // both Event Message and Sensor Scanning are disable for VR.
        // VR 的 Event Message 和 Sensor Scanning 均已禁用。
        sensorEventStatus = 0;
        return ipmi::responseSuccess(sensorEventStatus, assertions,
                                     deassertions);
    }

    auto warningInterface = sensorMap.find(SensorThresholdWarning::interface);
    auto criticalInterface = sensorMap.find(SensorThresholdCritical::interface);

    std::optional<bool> criticalDeassertHigh = thresholdDeassertMap
        [path][SensorThresholdCritical::property_names::critical_alarm_high];
    std::optional<bool> criticalDeassertLow = thresholdDeassertMap
        [path][SensorThresholdCritical::property_names::critical_alarm_low];
    std::optional<bool> warningDeassertHigh = thresholdDeassertMap
        [path][SensorThresholdWarning::property_names::warning_alarm_high];
    std::optional<bool> warningDeassertLow = thresholdDeassertMap
        [path][SensorThresholdWarning::property_names::warning_alarm_low];

    if (criticalDeassertHigh && !*criticalDeassertHigh)
    {
        deassertions.set(static_cast<size_t>(
            IPMIGetSensorEventEnableThresholds::upperCriticalGoingHigh));
    }
    if (criticalDeassertLow && !*criticalDeassertLow)
    {
        deassertions.set(static_cast<size_t>(
            IPMIGetSensorEventEnableThresholds::upperCriticalGoingLow));
    }
    if (warningDeassertHigh && !*warningDeassertHigh)
    {
        deassertions.set(static_cast<size_t>(
            IPMIGetSensorEventEnableThresholds::upperNonCriticalGoingHigh));
    }
    if (warningDeassertLow && !*warningDeassertLow)
    {
        deassertions.set(static_cast<size_t>(
            IPMIGetSensorEventEnableThresholds::lowerNonCriticalGoingHigh));
    }
    if ((warningInterface != sensorMap.end()) ||
        (criticalInterface != sensorMap.end()))
    {
        sensorEventStatus = static_cast<size_t>(
            IPMISensorEventEnableByte2::eventMessagesEnable);
        if (warningInterface != sensorMap.end())
        {
            auto& warningMap = warningInterface->second;

            auto warningHigh = warningMap.find(
                SensorThresholdWarning::property_names::warning_alarm_high);
            auto warningLow = warningMap.find(
                SensorThresholdWarning::property_names::warning_alarm_low);
            auto warningHighAlarm = false;
            auto warningLowAlarm = false;

            if (warningHigh != warningMap.end())
            {
                warningHighAlarm = std::get<bool>(warningHigh->second);
            }
            if (warningLow != warningMap.end())
            {
                warningLowAlarm = std::get<bool>(warningLow->second);
            }
            if (warningHighAlarm)
            {
                assertions.set(static_cast<size_t>(
                    IPMIGetSensorEventEnableThresholds::
                        upperNonCriticalGoingHigh));
            }
            if (warningLowAlarm)
            {
                assertions.set(static_cast<size_t>(
                    IPMIGetSensorEventEnableThresholds::
                        lowerNonCriticalGoingLow));
            }
        }
        if (criticalInterface != sensorMap.end())
        {
            auto& criticalMap = criticalInterface->second;

            auto criticalHigh = criticalMap.find(
                SensorThresholdCritical::property_names::critical_alarm_high);
            auto criticalLow = criticalMap.find(
                SensorThresholdCritical::property_names::critical_alarm_low);
            auto criticalHighAlarm = false;
            auto criticalLowAlarm = false;

            if (criticalHigh != criticalMap.end())
            {
                criticalHighAlarm = std::get<bool>(criticalHigh->second);
            }
            if (criticalLow != criticalMap.end())
            {
                criticalLowAlarm = std::get<bool>(criticalLow->second);
            }
            if (criticalHighAlarm)
            {
                assertions.set(static_cast<size_t>(
                    IPMIGetSensorEventEnableThresholds::
                        upperCriticalGoingHigh));
            }
            if (criticalLowAlarm)
            {
                assertions.set(static_cast<size_t>(
                    IPMIGetSensorEventEnableThresholds::lowerCriticalGoingLow));
            }
        }
    }

    return ipmi::responseSuccess(sensorEventStatus, assertions, deassertions);
}

// Construct a type 1 SDR for threshold sensor.
// 为阈值传感器构造 type 1 SDR。
void constructSensorSdrHeaderKey(uint16_t sensorNum, uint16_t recordID,
                                 get_sdr::SensorDataFullRecord& record)
{
    uint8_t sensornumber = static_cast<uint8_t>(sensorNum);
    uint8_t lun = static_cast<uint8_t>(sensorNum >> 8);

    record.header.recordId = recordID;
    record.header.sdrVersion = ipmiSdrVersion;
    record.header.recordType = get_sdr::SENSOR_DATA_FULL_RECORD;
    record.header.recordLength = sizeof(get_sdr::SensorDataFullRecord) -
                                 sizeof(get_sdr::SensorDataRecordHeader);
    record.key.ownerId = bmcI2CAddr;
    record.key.ownerLun = lun;
    record.key.sensorNumber = sensornumber;
}
bool constructSensorSdr(
    ipmi::Context::ptr ctx,
    const std::unordered_set<std::string>& ipmiDecoratorPaths,
    uint16_t sensorNum, uint16_t recordID, const std::string& service,
    const std::string& path, get_sdr::SensorDataFullRecord& record)
{
    constructSensorSdrHeaderKey(sensorNum, recordID, record);

    DbusInterfaceMap sensorMap;
    if (!getSensorMap(ctx, service, path, sensorMap, sensorMapSdrUpdatePeriod))
    {
        lg2::error("Failed to update sensor map for threshold sensor, "
                   "service: {SERVICE}, path: {PATH}",
                   "SERVICE", service, "PATH", path);
        return false;
    }

    record.body.sensorCapabilities = 0x68; // auto rearm - todo hysteresis
    record.body.sensorType = getSensorTypeFromPath(path);
    std::string type = getSensorTypeStringFromPath(path);
    auto typeCstr = type.c_str();
    auto findUnits = sensorUnits.find(typeCstr);
    if (findUnits != sensorUnits.end())
    {
        record.body.sensorUnits2Base = static_cast<uint8_t>(findUnits->second);
    } // else default 0x0 unspecified

    record.body.eventReadingType = getSensorEventTypeFromPath(path);

    auto sensorObject = sensorMap.find(SensorValue::interface);
    if (sensorObject == sensorMap.end())
    {
        lg2::error("constructSensorSdr: sensorObject error");
        return false;
    }

    uint8_t entityId = 0;
    uint8_t entityInstance = 0x01;

    // follow the association chain to get the parent board's entityid and
    // entityInstance
    // 沿着关联链获取父板卡的 entityid 和
    // entityInstance
    updateIpmiFromAssociation(path, ipmiDecoratorPaths, sensorMap, entityId,
                              entityInstance);

    record.body.entityId = entityId;
    record.body.entityInstance = entityInstance;

    double max = 0;
    double min = 0;
    getSensorMaxMin(sensorMap, max, min);

    int16_t mValue = 0;
    int8_t rExp = 0;
    int16_t bValue = 0;
    int8_t bExp = 0;
    bool bSigned = false;

    if (!getSensorAttributes(max, min, mValue, rExp, bValue, bExp, bSigned))
    {
        lg2::error("constructSensorSdr: getSensorAttributes error");
        return false;
    }

    // The record.body is a struct SensorDataFullRecordBody
    // from sensorhandler.hpp in phosphor-ipmi-host.
    // The meaning of these bits appears to come from
    // table 43.1 of the IPMI spec.
    // The above 5 sensor attributes are stuffed in as follows:
    // Byte 21 = AA000000 = analog interpretation, 10 signed, 00 unsigned
    // Byte 22-24 are for other purposes
    // Byte 25 = MMMMMMMM = LSB of M
    // Byte 26 = MMTTTTTT = MSB of M (signed), and Tolerance
    // Byte 27 = BBBBBBBB = LSB of B
    // Byte 28 = BBAAAAAA = MSB of B (signed), and LSB of Accuracy
    // Byte 29 = AAAAEE00 = MSB of Accuracy, exponent of Accuracy
    // Byte 30 = RRRRBBBB = rExp (signed), bExp (signed)
    // record.body 是来自 phosphor-ipmi-host 中 sensorhandler.hpp 的
    // 结构体 SensorDataFullRecordBody。
    // 这些位的含义似乎来自 IPMI 规范的
    // 表 43.1。
    // 上述 5 个传感器属性按如下方式填入：
    // Byte 21 = AA000000 = 模拟量解释，10 有符号，00 无符号
    // Byte 22-24 用于其他用途
    // Byte 25 = MMMMMMMM = M 的低字节
    // Byte 26 = MMTTTTTT = M 的高字节（有符号）和 Tolerance
    // Byte 27 = BBBBBBBB = B 的低字节
    // Byte 28 = BBAAAAAA = B 的高字节（有符号）和 Accuracy 低字节
    // Byte 29 = AAAAEE00 = Accuracy 高字节和 Accuracy 指数
    // Byte 30 = RRRRBBBB = rExp（有符号）、bExp（有符号）

    // apply M, B, and exponents, M and B are 10 bit values, exponents are 4
    // 应用 M、B 和指数，M 和 B 是 10 位值，指数是 4 位
    record.body.mLsb = mValue & 0xFF;

    uint8_t mBitSign = (mValue < 0) ? 1 : 0;
    uint8_t mBitNine = (mValue & 0x0100) >> 8;

    // move the smallest bit of the MSB into place (bit 9)
    // the MSbs are bits 7:8 in mMsbAndToLerance
    // 将 MSB 的最小位移动到正确位置（bit 9）
    // MSb 位于 mMsbAndToLerance 的 bit 7:8
    record.body.mMsbAndTolerance = (mBitSign << 7) | (mBitNine << 6);

    record.body.bLsb = bValue & 0xFF;

    uint8_t bBitSign = (bValue < 0) ? 1 : 0;
    uint8_t bBitNine = (bValue & 0x0100) >> 8;

    // move the smallest bit of the MSB into place (bit 9)
    // the MSbs are bits 7:8 in bMsbAndAccuracyLsb
    // 将 MSB 的最小位移动到正确位置（bit 9）
    // MSb 位于 bMsbAndAccuracyLsb 的 bit 7:8
    record.body.bMsbAndAccuracyLsb = (bBitSign << 7) | (bBitNine << 6);

    uint8_t rExpSign = (rExp < 0) ? 1 : 0;
    uint8_t rExpBits = rExp & 0x07;

    uint8_t bExpSign = (bExp < 0) ? 1 : 0;
    uint8_t bExpBits = bExp & 0x07;

    // move rExp and bExp into place
    // 将 rExp 和 bExp 移动到正确位置
    record.body.rbExponents =
        (rExpSign << 7) | (rExpBits << 4) | (bExpSign << 3) | bExpBits;

    // Set the analog reading byte interpretation accordingly
    // 相应地设置模拟读数字节的解释方式
    record.body.sensorUnits1 = (bSigned ? 1 : 0) << 7;

    // TODO(): Perhaps care about Tolerance, Accuracy, and so on
    // These seem redundant, but derivable from the above 5 attributes
    // Original comment said "todo fill out rest of units"
    // TODO()：也许应该关注 Tolerance、Accuracy 等。
    // 这些看起来冗余，但可由上述 5 个属性推导得出。
    // 原注释写着 "todo fill out rest of units"

    // populate sensor name from path
    // 从路径填充传感器名称
    auto name = sensor::parseSdrIdFromPath(path);
    get_sdr::body::setIdStrLen(name.size(), record.body);
    get_sdr::body::setIdType(3, record.body); // "8-bit ASCII + Latin 1"
    std::memcpy(record.body.idString, name.c_str(),
                std::min(name.length() + 1, sizeof(record.body.idString)));

    // Remember the sensor name, as determined for this sensor number
    // 记住为该传感器编号确定的传感器名称
    details::sdrStatsTable.updateName(sensorNum, name);

    bool sensorSettable = false;
    auto mutability =
        sensorMap.find("xyz.openbmc_project.Sensor.ValueMutability");
    if (mutability != sensorMap.end())
    {
        sensorSettable =
            mappedVariant<bool>(mutability->second, "Mutable", false);
    }
    get_sdr::body::initSettableState(sensorSettable, record.body);

    // Grant write permission to sensors deemed externally settable
    // 授予被视为可由外部设置的传感器的写权限
    details::sdrWriteTable.setWritePermission(sensorNum, sensorSettable);

    IPMIThresholds thresholdData;
    try
    {
        thresholdData = getIPMIThresholds(sensorMap);
    }
    catch (const std::exception&)
    {
        lg2::error("constructSensorSdr: getIPMIThresholds error");
        return false;
    }

    if (thresholdData.criticalHigh)
    {
        record.body.upperCriticalThreshold = *thresholdData.criticalHigh;
        record.body.supportedDeassertions[1] |= static_cast<uint8_t>(
            IPMISensorEventEnableThresholds::criticalThreshold);
        record.body.supportedDeassertions[1] |= static_cast<uint8_t>(
            IPMISensorEventEnableThresholds::upperCriticalGoingHigh);
        record.body.supportedAssertions[1] |= static_cast<uint8_t>(
            IPMISensorEventEnableThresholds::upperCriticalGoingHigh);
        record.body.discreteReadingSettingMask[0] |=
            static_cast<uint8_t>(IPMISensorReadingByte3::upperCritical);
    }
    if (thresholdData.warningHigh)
    {
        record.body.upperNoncriticalThreshold = *thresholdData.warningHigh;
        record.body.supportedDeassertions[1] |= static_cast<uint8_t>(
            IPMISensorEventEnableThresholds::nonCriticalThreshold);
        record.body.supportedDeassertions[0] |= static_cast<uint8_t>(
            IPMISensorEventEnableThresholds::upperNonCriticalGoingHigh);
        record.body.supportedAssertions[0] |= static_cast<uint8_t>(
            IPMISensorEventEnableThresholds::upperNonCriticalGoingHigh);
        record.body.discreteReadingSettingMask[0] |=
            static_cast<uint8_t>(IPMISensorReadingByte3::upperNonCritical);
    }
    if (thresholdData.criticalLow)
    {
        record.body.lowerCriticalThreshold = *thresholdData.criticalLow;
        record.body.supportedAssertions[1] |= static_cast<uint8_t>(
            IPMISensorEventEnableThresholds::criticalThreshold);
        record.body.supportedDeassertions[0] |= static_cast<uint8_t>(
            IPMISensorEventEnableThresholds::lowerCriticalGoingLow);
        record.body.supportedAssertions[0] |= static_cast<uint8_t>(
            IPMISensorEventEnableThresholds::lowerCriticalGoingLow);
        record.body.discreteReadingSettingMask[0] |=
            static_cast<uint8_t>(IPMISensorReadingByte3::lowerCritical);
    }
    if (thresholdData.warningLow)
    {
        record.body.lowerNoncriticalThreshold = *thresholdData.warningLow;
        record.body.supportedAssertions[1] |= static_cast<uint8_t>(
            IPMISensorEventEnableThresholds::nonCriticalThreshold);
        record.body.supportedDeassertions[0] |= static_cast<uint8_t>(
            IPMISensorEventEnableThresholds::lowerNonCriticalGoingLow);
        record.body.supportedAssertions[0] |= static_cast<uint8_t>(
            IPMISensorEventEnableThresholds::lowerNonCriticalGoingLow);
        record.body.discreteReadingSettingMask[0] |=
            static_cast<uint8_t>(IPMISensorReadingByte3::lowerNonCritical);
    }

    // everything that is readable is settable
    // 所有可读的都可设置
    record.body.discreteReadingSettingMask[1] =
        record.body.discreteReadingSettingMask[0];
    return true;
}

#ifdef FEATURE_HYBRID_SENSORS
// Construct a type 1 SDR for discrete Sensor typed sensor.
// 为离散类型传感器构造 type 1 SDR。
void constructStaticSensorSdr(ipmi::Context::ptr, uint16_t sensorNum,
                              uint16_t recordID,
                              ipmi::sensor::IdInfoMap::const_iterator sensor,
                              get_sdr::SensorDataFullRecord& record)
{
    constructSensorSdrHeaderKey(sensorNum, recordID, record);

    record.body.entityId = sensor->second.entityType;
    record.body.sensorType = sensor->second.sensorType;
    record.body.eventReadingType = sensor->second.sensorReadingType;
    record.body.entityInstance = sensor->second.instance;
    if (ipmi::sensor::Mutability::Write ==
        (sensor->second.mutability & ipmi::sensor::Mutability::Write))
    {
        get_sdr::body::initSettableState(true, record.body);
    }

    auto idString = sensor->second.sensorName;

    if (idString.empty())
    {
        idString = sensor->second.sensorNameFunc(sensor->second);
    }

    if (idString.length() > FULL_RECORD_ID_STR_MAX_LENGTH)
    {
        get_sdr::body::setIdStrLen(FULL_RECORD_ID_STR_MAX_LENGTH, record.body);
    }
    else
    {
        get_sdr::body::setIdStrLen(idString.length(), record.body);
    }
    get_sdr::body::setIdType(3, record.body); // "8-bit ASCII + Latin 1"
    std::strncpy(record.body.idString, idString.c_str(),
                 get_sdr::body::getIdStrLen(record.body));
}
#endif

// Construct type 3 SDR header and key (for VR and other discrete sensors)
// 构造 type 3 SDR 头部和键（用于 VR 和其他离散传感器）
void constructEventSdrHeaderKey(uint16_t sensorNum, uint16_t recordID,
                                get_sdr::SensorDataEventRecord& record)
{
    uint8_t sensornumber = static_cast<uint8_t>(sensorNum);
    uint8_t lun = static_cast<uint8_t>(sensorNum >> 8);

    record.header.recordId = recordID;
    record.header.sdrVersion = ipmiSdrVersion;
    record.header.recordType = get_sdr::SENSOR_DATA_EVENT_RECORD;
    record.header.recordLength = sizeof(get_sdr::SensorDataEventRecord) -
                                 sizeof(get_sdr::SensorDataRecordHeader);
    record.key.ownerId = bmcI2CAddr;
    record.key.ownerLun = lun;
    record.key.sensorNumber = sensornumber;

    record.body.entityId = 0x00;
    record.body.entityInstance = 0x01;
}

// Construct a type 3 SDR for VR typed sensor(daemon).
// 为 VR 类型传感器（daemon）构造 type 3 SDR。
bool constructVrSdr(ipmi::Context::ptr ctx,
                    const std::unordered_set<std::string>& ipmiDecoratorPaths,
                    uint16_t sensorNum, uint16_t recordID,
                    const std::string& service, const std::string& path,
                    get_sdr::SensorDataEventRecord& record)
{
    constructEventSdrHeaderKey(sensorNum, recordID, record);

    DbusInterfaceMap sensorMap;
    if (!getSensorMap(ctx, service, path, sensorMap, sensorMapSdrUpdatePeriod))
    {
        lg2::error("Failed to update sensor map for VR sensor, "
                   "service: {SERVICE}, path: {PATH}",
                   "SERVICE", service, "PATH", path);
        return false;
    }
    // follow the association chain to get the parent board's entityid and
    // entityInstance
    // 沿着关联链获取父板卡的 entityid 和
    // entityInstance
    updateIpmiFromAssociation(path, ipmiDecoratorPaths, sensorMap,
                              record.body.entityId, record.body.entityInstance);

    // Sensor type is hardcoded as a module/board type instead of parsing from
    // sensor path. This is because VR control is allocated in an independent
    // path(/xyz/openbmc_project/vr/profile/...) which is not categorized by
    // types.
    // 传感器类型硬编码为 module/board 类型，而不是从传感器路径解析。
    // 这是因为 VR 控制分配在独立的路径
    // (/xyz/openbmc_project/vr/profile/...) 下，
    // 该路径没有按类型分类。
    static constexpr const uint8_t moduleBoardType = 0x15;
    record.body.sensorType = moduleBoardType;
    record.body.eventReadingType = 0x00;

    record.body.sensorRecordSharing1 = 0x00;
    record.body.sensorRecordSharing2 = 0x00;

    // populate sensor name from path
    // 从路径填充传感器名称
    auto name = sensor::parseSdrIdFromPath(path);
    int nameSize = std::min(name.size(), sizeof(record.body.idString));
    get_sdr::body::setIdStrLen(nameSize, record.body);
    get_sdr::body::setIdType(3, record.body); // "8-bit ASCII + Latin 1"
    std::memset(record.body.idString, 0x00, sizeof(record.body.idString));
    std::memcpy(record.body.idString, name.c_str(), nameSize);

    // Remember the sensor name, as determined for this sensor number
    // 记住为该传感器编号确定的传感器名称
    details::sdrStatsTable.updateName(sensorNum, name);

    return true;
}

uint16_t getNumberOfSensors()
{
    return std::min(getSensorTree().size(), maxIPMISensors);
}

static int getSensorDataRecord(
    ipmi::Context::ptr ctx,
    const std::unordered_set<std::string>& ipmiDecoratorPaths,
    std::vector<uint8_t>& recordData, uint16_t recordID,
    uint8_t readBytes = std::numeric_limits<uint8_t>::max())
{
    recordData.clear();
    size_t lastRecord = ipmi::getNumberOfSensors() +
                        ipmi::sensor::getOtherSensorsCount(ctx) - 1;
    uint16_t nextRecord(recordID + 1);

    if (recordID == lastRecordIndex)
    {
        recordID = lastRecord;
    }
    if (recordID == lastRecord)
    {
        nextRecord = lastRecordIndex;
    }
    if (recordID > lastRecord)
    {
        lg2::error("getSensorDataRecord: recordID > lastRecord error");
        return GENERAL_ERROR;
    }
    if (recordID >= ipmi::getNumberOfSensors())
    {
        if (auto err = ipmi::sensor::getOtherSensorsDataRecord(ctx, recordID,
                                                               recordData);
            err < 0)
        {
            return lastRecordIndex;
        }
        return nextRecord;
    }

    // Perform a incremental scan of the SDR Record ID's and translate the
    // first 765 SDR records (i.e. maxIPMISensors) into IPMI Sensor
    // Numbers. The IPMI sensor numbers are not linear, and have a reserved
    // gap at 0xff. This code creates 254 sensors per LUN, excepting LUN 2
    // which has special meaning.
    // 对 SDR Record ID 执行增量扫描，并将前 765 条 SDR 记录
    // （即 maxIPMISensors）转换为 IPMI 传感器编号。IPMI 传感器
    // 编号不是线性的，在 0xff 处有一个保留间隙。此代码为
    // 每个 LUN 创建 254 个传感器，但 LUN 2 除外，
    // 它具有特殊含义。
    std::string connection;
    std::string path;
    std::vector<std::string> interfaces;
    uint16_t sensNumFromRecID{recordID};
    if ((recordID > lun0MaxSensorNum) && (recordID < lun1MaxSensorNum))
    {
        // LUN 0 has one reserved sensor number. Compensate here by adding one
        // to the record ID
        // LUN 0 有一个保留的传感器编号。此处通过给记录 ID
        // 加 1 来补偿
        sensNumFromRecID = recordID + 1;
        ctx->lun = lun1;
    }
    else if ((recordID >= lun1MaxSensorNum) && (recordID < maxIPMISensors))
    {
        // LUN 0, 1 have a reserved sensor number. Compensate here by adding 2
        // to the record ID. Skip all 256 sensors in LUN 2, as it has special
        // rules governing its use.
        // LUN 0、1 各有一个保留的传感器编号。此处通过给记录 ID 加 2 来补偿。
        // 跳过 LUN 2 中的全部 256 个传感器，
        // 因为其使用受特殊规则约束。
        sensNumFromRecID = recordID + (maxSensorsPerLUN + 1) + 2;
        ctx->lun = lun3;
    }

    auto status =
        getSensorConnection(ctx, static_cast<uint8_t>(sensNumFromRecID),
                            connection, path, &interfaces);
    if (status)
    {
        lg2::error("getSensorDataRecord: getSensorConnection error");
        return GENERAL_ERROR;
    }
    uint16_t sensorNum = getSensorNumberFromPath(path);
    // Return an error on LUN 2 assignments, and any sensor number beyond the
    // range of LUN 3
    // 对 LUN 2 的分配以及超出 LUN 3 范围的
    // 传感器编号返回错误
    if (((sensorNum > lun1MaxSensorNum) && (sensorNum <= maxIPMISensors)) ||
        (sensorNum > lun3MaxSensorNum))
    {
        lg2::error("getSensorDataRecord: invalidSensorNumber");
        return GENERAL_ERROR;
    }
    uint8_t sensornumber = static_cast<uint8_t>(sensorNum);
    uint8_t lun = static_cast<uint8_t>(sensorNum >> 8);

    if ((sensornumber != static_cast<uint8_t>(sensNumFromRecID)) &&
        (lun != ctx->lun))
    {
        lg2::error("getSensorDataRecord: sensor record mismatch");
        return GENERAL_ERROR;
    }

    // Construct full record (SDR type 1) for the threshold sensors
    // 为阈值传感器构造完整记录（SDR type 1）
    if (std::find(interfaces.begin(), interfaces.end(),
                  SensorValue::interface) != interfaces.end())
    {
        get_sdr::SensorDataFullRecord record = {};

        // If the request doesn't read SDR body, construct only header and key
        // part to avoid additional DBus transaction.
        // 如果请求不读取 SDR body，则只构造头部和键部分，
        // 以避免额外的 DBus 事务。
        if (readBytes <= sizeof(record.header) + sizeof(record.key))
        {
            constructSensorSdrHeaderKey(sensorNum, recordID, record);
        }
        else if (!constructSensorSdr(ctx, ipmiDecoratorPaths, sensorNum,
                                     recordID, connection, path, record))
        {
            return GENERAL_ERROR;
        }

        recordData.insert(recordData.end(), reinterpret_cast<uint8_t*>(&record),
                          reinterpret_cast<uint8_t*>(&record) + sizeof(record));

        return nextRecord;
    }

#ifdef FEATURE_HYBRID_SENSORS
    if (auto sensor = findStaticSensor(path);
        sensor != ipmi::sensor::sensors.end() &&
        getSensorEventTypeFromPath(path) !=
            static_cast<uint8_t>(SensorEventTypeCodes::threshold))
    {
        get_sdr::SensorDataFullRecord record = {};

        // If the request doesn't read SDR body, construct only header and key
        // part to avoid additional DBus transaction.
        // 如果请求不读取 SDR body，则只构造头部和键部分，
        // 以避免额外的 DBus 事务。
        if (readBytes <= sizeof(record.header) + sizeof(record.key))
        {
            constructSensorSdrHeaderKey(sensorNum, recordID, record);
        }
        else
        {
            constructStaticSensorSdr(ctx, sensorNum, recordID, sensor, record);
        }

        recordData.insert(recordData.end(), reinterpret_cast<uint8_t*>(&record),
                          reinterpret_cast<uint8_t*>(&record) + sizeof(record));

        return nextRecord;
    }
#endif

    // Construct SDR type 3 record for VR sensor (daemon)
    // 为 VR 传感器（daemon）构造 SDR type 3 记录
    if (std::find(interfaces.begin(), interfaces.end(), sensor::vrInterface) !=
        interfaces.end())
    {
        get_sdr::SensorDataEventRecord record = {};

        // If the request doesn't read SDR body, construct only header and key
        // part to avoid additional DBus transaction.
        // 如果请求不读取 SDR body，则只构造头部和键部分，
        // 以避免额外的 DBus 事务。
        if (readBytes <= sizeof(record.header) + sizeof(record.key))
        {
            constructEventSdrHeaderKey(sensorNum, recordID, record);
        }
        else if (!constructVrSdr(ctx, ipmiDecoratorPaths, sensorNum, recordID,
                                 connection, path, record))
        {
            return GENERAL_ERROR;
        }
        recordData.insert(recordData.end(), reinterpret_cast<uint8_t*>(&record),
                          reinterpret_cast<uint8_t*>(&record) + sizeof(record));
    }

    return nextRecord;
}

/** @brief implements the get SDR Info command
 *  @param operation : 0 or not supplied returns sensor count
 *                     1 return SDR count
 *
 *  @returns IPMI completion code plus response data
 *   - sdrCount - sensor/SDR count
 *   - lunsAndDynamicPopulation - static/Dynamic sensor population flag
 */
/* @brief 实现 get SDR Info 命令
 *  @param operation：0 或未提供时返回传感器数量；
 *                     1 返回 SDR 数量
 *  @returns IPMI 完成码加上响应数据
 *   - sdrCount - 传感器/SDR 数量
 *   - lunsAndDynamicPopulation - 静态/动态传感器标志 */
static ipmi::RspType<uint8_t, // respcount
                     uint8_t, // dynamic population flags
                     uint32_t // last time a sensor was added
                     >
    ipmiSensorGetDeviceSdrInfo(ipmi::Context::ptr ctx,
                               std::optional<uint8_t> operation)
{
    auto& sensorTree{getSensorTree()};
    uint8_t sdrCount{};
    // Sensors are dynamically allocated
    // 传感器是动态分配的
    uint8_t lunsAndDynamicPopulation{0x80};
    constexpr uint8_t getSdrCount{1};
    constexpr uint8_t getSensorCount{0};

    if (!getSensorSubtree(sensorTree) || sensorTree.empty())
    {
        return ipmi::responseResponseError();
    }
    uint16_t numSensors{ipmi::getNumberOfSensors()};
    if (operation.value_or(0) == getSdrCount)
    {
        sdrCount = numSensors + ipmi::sensor::getOtherSensorsCount(ctx) - 1;
    }
    else if (operation.value_or(0) == getSensorCount)
    {
        // Return the number of sensors attached to the LUN
        // 返回附加到该 LUN 的传感器数量
        if ((ctx->lun == lun0) && (numSensors > 0))
        {
            sdrCount =
                (numSensors > maxSensorsPerLUN) ? maxSensorsPerLUN : numSensors;
        }
        else if ((ctx->lun == lun1) && (numSensors > maxSensorsPerLUN))
        {
            sdrCount = (numSensors > (2 * maxSensorsPerLUN))
                           ? maxSensorsPerLUN
                           : (numSensors - maxSensorsPerLUN) & maxSensorsPerLUN;
        }
        else if (ctx->lun == lun3)
        {
            if (numSensors <= maxIPMISensors)
            {
                sdrCount = (numSensors - (2 * maxSensorsPerLUN)) &
                           maxSensorsPerLUN;
            }
            else
            {
                throw std::out_of_range(
                    "Maximum number of IPMI sensors exceeded.");
            }
        }
    }
    else
    {
        return ipmi::responseInvalidFieldRequest();
    }

    // Flag which LUNs have sensors associated
    // 标记哪些 LUN 有关联的传感器
    if (numSensors > 0)
    {
        lunsAndDynamicPopulation |= 1;
    }
    if (numSensors > maxSensorsPerLUN)
    {
        lunsAndDynamicPopulation |= 2;
    }
    if (numSensors >= (maxSensorsPerLUN * 2))
    {
        lunsAndDynamicPopulation |= 8;
    }
    if (numSensors > maxIPMISensors)
    {
        throw std::out_of_range("Maximum number of IPMI sensors exceeded.");
    }

    return ipmi::responseSuccess(sdrCount, lunsAndDynamicPopulation,
                                 sdrLastAdd);
}

/* end sensor commands */
/* 传感器命令结束 */

/* storage commands */
/* 存储命令 */

ipmi::RspType<uint8_t,  // sdr version
              uint16_t, // record count
              uint16_t, // free space
              uint32_t, // most recent addition
              uint32_t, // most recent erase
              uint8_t   // operationSupport
              >
    ipmiStorageGetSDRRepositoryInfo(ipmi::Context::ptr ctx)
{
    constexpr const uint16_t unspecifiedFreeSpace = 0xFFFF;
    uint16_t recordCount =
        ipmi::getNumberOfSensors() + ipmi::sensor::getOtherSensorsCount(ctx);

    uint8_t operationSupport = static_cast<uint8_t>(
        SdrRepositoryInfoOps::overflow); // write not supported

    operationSupport |=
        static_cast<uint8_t>(SdrRepositoryInfoOps::allocCommandSupported);
    operationSupport |= static_cast<uint8_t>(
        SdrRepositoryInfoOps::reserveSDRRepositoryCommandSupported);
    return ipmi::responseSuccess(ipmiSdrVersion, recordCount,
                                 unspecifiedFreeSpace, sdrLastAdd,
                                 sdrLastRemove, operationSupport);
}

/** @brief implements the get SDR allocation info command
 *
 *  @returns IPMI completion code plus response data
 *   - allocUnits    - Number of possible allocation units
 *   - allocUnitSize - Allocation unit size in bytes.
 *   - allocUnitFree - Number of free allocation units
 *   - allocUnitLargestFree - Largest free block in allocation units
 *   - maxRecordSize    - Maximum record size in allocation units.
 */
/* @brief 实现 get SDR allocation info 命令
 *  @returns IPMI 完成码加上响应数据
 *   - allocUnits - 可能的分配单元数量
 *   - allocUnitSize - 分配单元大小（字节）
 *   - allocUnitFree - 空闲分配单元数量
 *   - allocUnitLargestFree - 以分配单元计的最大空闲块
 *   - maxRecordSize - 以分配单元计的最大记录大小 */
ipmi::RspType<uint16_t, // allocUnits
              uint16_t, // allocUnitSize
              uint16_t, // allocUnitFree
              uint16_t, // allocUnitLargestFree
              uint8_t   // maxRecordSize
              >
    ipmiStorageGetSDRAllocationInfo()
{
    // 0000h unspecified number of alloc units
    // 0000h 表示未指定分配单元数量
    constexpr uint16_t allocUnits = 0;

    constexpr uint16_t allocUnitFree = 0;
    constexpr uint16_t allocUnitLargestFree = 0;
    // only allow one block at a time
    // 一次只允许一个块
    constexpr uint8_t maxRecordSize = 1;

    return ipmi::responseSuccess(allocUnits, maxSDRTotalSize, allocUnitFree,
                                 allocUnitLargestFree, maxRecordSize);
}

/** @brief implements the reserve SDR command
 *  @returns IPMI completion code plus response data
 *   - sdrReservationID
 */
/* @brief 实现 reserve SDR 命令
 *  @returns IPMI 完成码加上响应数据
 *   - sdrReservationID */
ipmi::RspType<uint16_t> ipmiStorageReserveSDR()
{
    sdrReservationID++;
    if (sdrReservationID == 0)
    {
        sdrReservationID++;
    }

    return ipmi::responseSuccess(sdrReservationID);
}

ipmi::RspType<uint16_t,            // next record ID
              std::vector<uint8_t> // payload
              >
    ipmiStorageGetSDR(ipmi::Context::ptr ctx, uint16_t reservationID,
                      uint16_t recordID, uint8_t offset, uint8_t bytesToRead)
{
    // reservation required for partial reads with non zero offset into
    // record
    // 对记录中非零偏移量的部分读取
    // 需要预留（reservation）
    if ((sdrReservationID == 0 || reservationID != sdrReservationID) && offset)
    {
        lg2::error("ipmiStorageGetSDR: responseInvalidReservationId");
        return ipmi::responseInvalidReservationId();
    }

    auto& sensorTree = getSensorTree();
    if (!getSensorSubtree(sensorTree) && sensorTree.empty())
    {
        lg2::error("ipmiStorageGetSDR: getSensorSubtree error");
        return ipmi::responseResponseError();
    }

    auto& ipmiDecoratorPaths = getIpmiDecoratorPaths(ctx);

    std::vector<uint8_t> record;
    int nextRecordId = getSensorDataRecord(
        ctx, ipmiDecoratorPaths.value_or(std::unordered_set<std::string>()),
        record, recordID, offset + bytesToRead);

    if (nextRecordId < 0)
    {
        lg2::error("ipmiStorageGetSDR: fail to get SDR");
        return ipmi::responseInvalidFieldRequest();
    }
    get_sdr::SensorDataRecordHeader* hdr =
        reinterpret_cast<get_sdr::SensorDataRecordHeader*>(record.data());
    if (!hdr)
    {
        lg2::error("ipmiStorageGetSDR: record header is null");
        return ipmi::responseSuccess(nextRecordId, record);
    }

    size_t sdrLength =
        sizeof(get_sdr::SensorDataRecordHeader) + hdr->recordLength;
    if (offset >= sdrLength)
    {
        lg2::error("ipmiStorageGetSDR: offset is outside the record");
        return ipmi::responseParmOutOfRange();
    }
    if (sdrLength < (offset + bytesToRead))
    {
        bytesToRead = sdrLength - offset;
    }

    uint8_t* respStart = reinterpret_cast<uint8_t*>(hdr) + offset;
    if (!respStart)
    {
        lg2::error("ipmiStorageGetSDR: record is null");
        return ipmi::responseSuccess(nextRecordId, record);
    }

    std::vector<uint8_t> recordData(respStart, respStart + bytesToRead);

    return ipmi::responseSuccess(nextRecordId, recordData);
}
namespace dcmi
{

std::tuple<uint8_t,                // Total of instance sensors
           std::vector<sensorInfo> // The list of sensors
           >
    getSensorsByEntityId(ipmi::Context::ptr ctx, uint8_t entityId,
                         uint8_t entityInstance, uint8_t instanceStart)
{
    std::vector<sensorInfo> sensorList;
    uint8_t totalInstSensor = 0;
    auto match = ipmi::dcmi::validEntityId.find(entityId);

    if (match == ipmi::dcmi::validEntityId.end())
    {
        return std::make_tuple(totalInstSensor, sensorList);
    }

    auto& sensorTree = getSensorTree();
    if (!getSensorSubtree(sensorTree) && sensorTree.empty())
    {
        return std::make_tuple(totalInstSensor, sensorList);
    }

    auto& ipmiDecoratorPaths = getIpmiDecoratorPaths(ctx);

    size_t invalidSensorNumberErrCount = 0;
    for (const auto& sensor : sensorTree)
    {
        const std::string& sensorObjPath = sensor.first;
        const auto& sensorTypeValue = getSensorTypeFromPath(sensorObjPath);

        /*
         * In the DCMI specification, it only supports the sensor type is 0x01
         * (temperature type) for both Get Sensor Info and Get Temperature
         * Readings commands.
         */
        /* 在 DCMI 规范中，
         * Get Sensor Info 和 Get Temperature Readings 命令
         * 都只支持传感器类型 0x01（温度类型）。 */
        if (sensorTypeValue != ipmi::dcmi::temperatureSensorType)
        {
            continue;
        }

        const auto& connection = sensor.second.begin()->first;
        DbusInterfaceMap sensorMap;

        if (!getSensorMap(ctx, connection, sensorObjPath, sensorMap,
                          sensorMapSdrUpdatePeriod))
        {
            lg2::error("Failed to update sensor map for threshold sensor, "
                       "service: {SERVICE}, path: {PATH}",
                       "SERVICE", connection, "PATH", sensorObjPath);
            continue;
        }

        uint8_t entityIdValue = 0;
        uint8_t entityInstanceValue = 0;

        /*
         * Get the Entity ID, Entity Instance information which are configured
         * in the Entity-Manager.
         */
        /* 获取 Entity-Manager 中配置的 Entity ID、
         * Entity Instance 信息。 */
        updateIpmiFromAssociation(
            sensorObjPath,
            ipmiDecoratorPaths.value_or(std::unordered_set<std::string>()),
            sensorMap, entityIdValue, entityInstanceValue);

        if (entityIdValue == match->first || entityIdValue == match->second)
        {
            totalInstSensor++;

            /*
             * When Entity Instance parameter is not 0, we only get the first
             * sensor whose Entity Instance number is equal input Entity
             * Instance parameter.
             */
            /* 当 Entity Instance 参数不为 0 时，
             * 我们只获取第一个 Entity Instance 编号等于
             * 输入 Entity Instance 参数的传感器。 */
            if (entityInstance)
            {
                if (!sensorList.empty())
                {
                    continue;
                }

                if (entityInstanceValue == entityInstance)
                {
                    auto recordId = getSensorNumberFromPath(sensorObjPath);
                    if (recordId == invalidSensorNumber)
                    {
                        ++invalidSensorNumberErrCount;
                        continue;
                    }
                    sensorList.emplace_back(sensorObjPath, sensorTypeValue,
                                            recordId, entityIdValue,
                                            entityInstanceValue);
                }
            }
            else if (entityInstanceValue >= instanceStart)
            {
                auto recordId = getSensorNumberFromPath(sensorObjPath);
                if (recordId == invalidSensorNumber)
                {
                    ++invalidSensorNumberErrCount;
                    continue;
                }
                sensorList.emplace_back(sensorObjPath, sensorTypeValue,
                                        recordId, entityIdValue,
                                        entityInstanceValue);
            }
        }
    }
    if (invalidSensorNumberErrCount != 0)
    {
        lg2::error("getSensorNumberFromPath returned invalidSensorNumber "
                   "{ERR_COUNT} times",
                   "ERR_COUNT", invalidSensorNumberErrCount);
    }

    auto cmpFunc = [](sensorInfo first, sensorInfo second) {
        return first.entityInstance <= second.entityInstance;
    };

    sort(sensorList.begin(), sensorList.end(), cmpFunc);

    return std::make_tuple(totalInstSensor, sensorList);
}

std::tuple<bool,    // Reading result
           uint7_t, // Temp value
           bool>    // Sign bit
    readTemp(ipmi::Context::ptr ctx, const std::string& objectPath)
{
    std::string service{};
    boost::system::error_code ec =
        ipmi::getService(ctx, SensorValue::interface, objectPath, service);
    if (ec.value())
    {
        return std::make_tuple(false, 0, false);
    }

    ipmi::PropertyMap properties{};
    ec = ipmi::getAllDbusProperties(ctx, service, objectPath,
                                    SensorValue::interface, properties);
    if (ec.value())
    {
        return std::make_tuple(false, 0, false);
    }

    auto scaleIt = properties.find("Scale");
    double scaleVal = 0.0;
    if (scaleIt != properties.end())
    {
        scaleVal = std::visit(ipmi::VariantToDoubleVisitor(), scaleIt->second);
    }

    auto tempValIt = properties.find(SensorValue::property_names::value);
    double tempVal = 0.0;
    if (tempValIt == properties.end())
    {
        return std::make_tuple(false, 0, false);
    }

    const double maxTemp = 127;
    double absTempVal = 0.0;
    bool signBit = false;

    tempVal = std::visit(ipmi::VariantToDoubleVisitor(), tempValIt->second);
    tempVal = std::pow(10, scaleVal) * tempVal;
    absTempVal = std::abs(tempVal);
    absTempVal = std::min(absTempVal, maxTemp);
    signBit = (tempVal < 0) ? true : false;

    return std::make_tuple(true, static_cast<uint7_t>(absTempVal), signBit);
}

ipmi::RspType<uint8_t,              // No of instances for requested id
              uint8_t,              // No of record ids in the response
              std::vector<uint16_t> // SDR Record ID corresponding to the Entity
                                    // IDs
                                    // IDs
              >
    getSensorInfo(ipmi::Context::ptr ctx, uint8_t sensorType, uint8_t entityId,
                  uint8_t entityInstance, uint8_t instanceStart)
{
    auto match = ipmi::dcmi::validEntityId.find(entityId);
    if (match == ipmi::dcmi::validEntityId.end())
    {
        lg2::error("Unknown Entity ID: {ENTITY_ID}", "ENTITY_ID", entityId);

        return ipmi::responseInvalidFieldRequest();
    }

    if (sensorType != ipmi::dcmi::temperatureSensorType)
    {
        lg2::error("Invalid sensor type: {SENSOR_TYPE}", "SENSOR_TYPE",
                   sensorType);

        return ipmi::responseInvalidFieldRequest();
    }

    std::vector<uint16_t> sensorRec{};
    const auto& [totalSensorInst, sensorList] =
        getSensorsByEntityId(ctx, entityId, entityInstance, instanceStart);

    if (sensorList.empty())
    {
        return ipmi::responseSuccess(totalSensorInst, 0, sensorRec);
    }

    /*
     * As DCMI specification, the maximum number of Record Ids of response data
     * is 1 if Entity Instance parameter is not 0. Else the maximum number of
     * Record Ids of response data is 8. Therefore, not all of sensors are shown
     * in response data.
     */
    /* 按照 DCMI 规范，当 Entity Instance 参数不为 0 时，
     * 响应数据中 Record Id 的最大数量为 1；
     * 否则响应数据中 Record Id 的最大数量为 8。
     * 因此，并非所有传感器都会显示在响应数据中。 */
    uint8_t numOfRec = (entityInstance != 0) ? 1 : ipmi::dcmi::maxRecords;

    for (const auto& sensor : sensorList)
    {
        sensorRec.emplace_back(sensor.recordId);
        if (sensorRec.size() >= numOfRec)
        {
            break;
        }
    }

    return ipmi::responseSuccess(
        totalSensorInst, static_cast<uint8_t>(sensorRec.size()), sensorRec);
}

ipmi::RspType<uint8_t,                // No of instances for requested id
              uint8_t,                // No of record ids in the response
              std::vector<            // Temperature Data
                  std::tuple<uint7_t, // Temperature value
                             bool,    // Sign bit
                             uint8_t  // Entity Instance of sensor
                             >>>
    getTempReadings(ipmi::Context::ptr ctx, uint8_t sensorType,
                    uint8_t entityId, uint8_t entityInstance,
                    uint8_t instanceStart)
{
    auto match = ipmi::dcmi::validEntityId.find(entityId);
    if (match == ipmi::dcmi::validEntityId.end())
    {
        lg2::error("Unknown Entity ID: {ENTITY_ID}", "ENTITY_ID", entityId);

        return ipmi::responseInvalidFieldRequest();
    }

    if (sensorType != ipmi::dcmi::temperatureSensorType)
    {
        lg2::error("Invalid sensor type: {SENSOR_TYPE}", "SENSOR_TYPE",
                   sensorType);

        return ipmi::responseInvalidFieldRequest();
    }

    std::vector<std::tuple<uint7_t, bool, uint8_t>> tempReadingVal{};
    const auto& [totalSensorInst, sensorList] =
        getSensorsByEntityId(ctx, entityId, entityInstance, instanceStart);

    if (sensorList.empty())
    {
        return ipmi::responseSuccess(totalSensorInst, 0, tempReadingVal);
    }

    /*
     * As DCMI specification, the maximum number of Record Ids of response data
     * is 1 if Entity Instance parameter is not 0. Else the maximum number of
     * Record Ids of response data is 8. Therefore, not all of sensors are shown
     * in response data.
     */
    /* 按照 DCMI 规范，当 Entity Instance 参数不为 0 时，
     * 响应数据中 Record Id 的最大数量为 1；
     * 否则响应数据中 Record Id 的最大数量为 8。
     * 因此，并非所有传感器都会显示在响应数据中。 */
    uint8_t numOfRec = (entityInstance != 0) ? 1 : ipmi::dcmi::maxRecords;

    for (const auto& sensor : sensorList)
    {
        const auto& [readResult, tempVal, signBit] =
            readTemp(ctx, sensor.objectPath);

        if (readResult)
        {
            tempReadingVal.emplace_back(
                std::make_tuple(tempVal, signBit, sensor.entityInstance));

            if (tempReadingVal.size() >= numOfRec)
            {
                break;
            }
        }
    }

    return ipmi::responseSuccess(totalSensorInst,
                                 static_cast<uint8_t>(tempReadingVal.size()),
                                 tempReadingVal);
}

} // namespace dcmi

/* end storage commands */
/* 存储命令结束 */

void registerSensorFunctions()
{
    // <Platform Event>
    // <Platform Event>（平台事件）
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnSensor,
                          ipmi::sensor_event::cmdPlatformEvent,
                          ipmi::Privilege::Operator, ipmiSenPlatformEvent);

    // <Set Sensor Reading and Event Status>
    // <Set Sensor Reading and Event Status>（设置传感器读数和事件状态）
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnSensor,
                          ipmi::sensor_event::cmdSetSensorReadingAndEvtSts,
                          ipmi::Privilege::Operator, ipmiSetSensorReading);

    // <Get Sensor Reading>
    // <Get Sensor Reading>（获取传感器读数）
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnSensor,
                          ipmi::sensor_event::cmdGetSensorReading,
                          ipmi::Privilege::User, ipmiSenGetSensorReading);

    // <Get Sensor Threshold>
    // <Get Sensor Threshold>（获取传感器阈值）
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnSensor,
                          ipmi::sensor_event::cmdGetSensorThreshold,
                          ipmi::Privilege::User, ipmiSenGetSensorThresholds);

    // <Set Sensor Threshold>
    // <Set Sensor Threshold>（设置传感器阈值）
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnSensor,
                          ipmi::sensor_event::cmdSetSensorThreshold,
                          ipmi::Privilege::Operator,
                          ipmiSenSetSensorThresholds);

    // <Get Sensor Event Enable>
    // <Get Sensor Event Enable>（获取传感器事件使能）
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnSensor,
                          ipmi::sensor_event::cmdGetSensorEventEnable,
                          ipmi::Privilege::User, ipmiSenGetSensorEventEnable);

    // <Get Sensor Event Status>
    // <Get Sensor Event Status>（获取传感器事件状态）
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnSensor,
                          ipmi::sensor_event::cmdGetSensorEventStatus,
                          ipmi::Privilege::User, ipmiSenGetSensorEventStatus);

    // register all storage commands for both Sensor and Storage command
    // versions
    // 为 Sensor 和 Storage 两个命令版本
    // 注册所有存储命令

    // <Get SDR Repository Info>
    // <Get SDR Repository Info>（获取 SDR 仓库信息）
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnStorage,
                          ipmi::storage::cmdGetSdrRepositoryInfo,
                          ipmi::Privilege::User,
                          ipmiStorageGetSDRRepositoryInfo);

    // <Get Device SDR Info>
    // <Get Device SDR Info>（获取设备 SDR 信息）
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnSensor,
                          ipmi::sensor_event::cmdGetDeviceSdrInfo,
                          ipmi::Privilege::User, ipmiSensorGetDeviceSdrInfo);

    // <Get SDR Allocation Info>
    // <Get SDR Allocation Info>（获取 SDR 分配信息）
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnStorage,
                          ipmi::storage::cmdGetSdrRepositoryAllocInfo,
                          ipmi::Privilege::User,
                          ipmiStorageGetSDRAllocationInfo);

    // <Reserve SDR Repo>
    // <Reserve SDR Repo>（预留 SDR 仓库）
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnSensor,
                          ipmi::sensor_event::cmdReserveDeviceSdrRepository,
                          ipmi::Privilege::User, ipmiStorageReserveSDR);

    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnStorage,
                          ipmi::storage::cmdReserveSdrRepository,
                          ipmi::Privilege::User, ipmiStorageReserveSDR);

    // <Get Sdr>
    // <Get Sdr>（获取 SDR）
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnSensor,
                          ipmi::sensor_event::cmdGetDeviceSdr,
                          ipmi::Privilege::User, ipmiStorageGetSDR);

    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnStorage,
                          ipmi::storage::cmdGetSdr, ipmi::Privilege::User,
                          ipmiStorageGetSDR);
    // <Get DCMI Sensor Info>
    // <Get DCMI Sensor Info>（获取 DCMI 传感器信息）
    ipmi::registerGroupHandler(
        ipmi::prioOpenBmcBase, ipmi::groupDCMI,
        ipmi::dcmi::cmdGetDcmiSensorInfo, ipmi::Privilege::Operator,
        ipmi::dcmi::getSensorInfo);
    // <Get Temperature Readings>
    // <Get Temperature Readings>（获取温度读数）
    ipmi::registerGroupHandler(
        ipmi::prioOpenBmcBase, ipmi::groupDCMI,
        ipmi::dcmi::cmdGetTemperatureReadings, ipmi::Privilege::User,
        ipmi::dcmi::getTempReadings);
}
} // namespace ipmi
