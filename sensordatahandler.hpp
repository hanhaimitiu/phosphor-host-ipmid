#pragma once

/**
 * @file sensordatahandler.hpp
 * @brief 传感器数据 handler 的声明。
 */
/* @file sensordatahandler.hpp
 * @brief 传感器数据 handler 的声明。 */

#include "config.h"

#include "sensorhandler.hpp"

#include <ipmid/api.hpp>
#include <ipmid/types.hpp>
#include <ipmid/utils.hpp>
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/message/types.hpp>
#include <xyz/openbmc_project/Sensor/Threshold/Critical/common.hpp>
#include <xyz/openbmc_project/Sensor/Threshold/Warning/common.hpp>
#include <xyz/openbmc_project/Sensor/Value/common.hpp>

#include <cmath>

#ifdef FEATURE_SENSORS_CACHE

extern ipmi::sensor::SensorCacheMap sensorCacheMap;

// The signal's message type is 0x04 from DBus spec:
// https://dbus.freedesktop.org/doc/dbus-specification.html#message-protocol-messages
// 根据 DBus 规范，该信号的消息类型为 0x04：
// https://dbus.freedesktop.org/doc/dbus-specification.html#message-protocol-messages
static constexpr auto msgTypeSignal = 0x04;

#endif

namespace ipmi
{
namespace sensor
{

using Assertion = uint16_t;
using Deassertion = uint16_t;
using AssertionSet = std::pair<Assertion, Deassertion>;
using Service = std::string;
using Path = std::string;
using Interface = std::string;
using ServicePath = std::pair<Path, Service>;
using Interfaces = std::vector<Interface>;
using MapperResponseType = std::map<Path, std::map<Service, Interfaces>>;
using PropertyMap = ipmi::PropertyMap;

using SensorValue = sdbusplus::common::xyz::openbmc_project::sensor::Value;
using SensorThresholdWarning =
    sdbusplus::common::xyz::openbmc_project::sensor::threshold::Warning;
using SensorThresholdCritical =
    sdbusplus::common::xyz::openbmc_project::sensor::threshold::Critical;

using namespace phosphor::logging;

/** @brief Make assertion set from input data
 *  @param[in] cmdData - Input sensor data
 *  @return pair of assertion and deassertion set
 */
/* @brief 根据输入数据生成断言集合
 *  @param[in] cmdData - 输入传感器数据
 *  @return 断言与取消断言集合的 pair */
AssertionSet getAssertionSet(const SetSensorReadingReq& cmdData);

/** @brief send the message to DBus
 *  @param[in] msg - message to send
 *  @return failure status in IPMI error code
 */
/* @brief 向 DBus 发送消息
 *  @param[in] msg - 要发送的消息
 *  @return 以 IPMI 错误码表示的失败状态 */
ipmi::Cc updateToDbus(IpmiUpdateData& msg);

namespace get
{

/** @brief Populate sensor name from the D-Bus property associated with the
 *         sensor. In the example entry from the yaml, the name of the D-bus
 *         property "AttemptsLeft" is the sensor name.
 *
 *         0x07:
 *            sensorType: 195
 *            path: /xyz/openbmc_project/state/host0
 *            sensorReadingType: 0x6F
 *            serviceInterface: org.freedesktop.DBus.Properties
 *            readingType: readingAssertion
 *            sensorNamePattern: nameProperty
 *            interfaces:
 *              xyz.openbmc_project.Control.Boot.RebootAttempts:
 *                AttemptsLeft:
 *                    Offsets:
 *                        0xFF:
 *                          type: uint32_t
 *
 *
 *  @param[in] sensorInfo - Dbus info related to sensor.
 *
 *  @return On success return the sensor name for the sensor.
 */
/* @brief 根据与传感器关联的 D-Bus 属性填充传感器名称。
 *         在 yaml 的示例条目中，D-bus 属性
 *         "AttemptsLeft" 的名称即为传感器名称。
 *         0x07:
 *            sensorType: 195
 *            path: /xyz/openbmc_project/state/host0
 *            sensorReadingType: 0x6F
 *            serviceInterface: org.freedesktop.DBus.Properties
 *            readingType: readingAssertion
 *            sensorNamePattern: nameProperty
 *            interfaces:
 *              xyz.openbmc_project.Control.Boot.RebootAttempts:
 *                AttemptsLeft:
 *                    Offsets:
 *                        0xFF:
 *                          type: uint32_t
 *  @param[in] sensorInfo - 与传感器相关的 Dbus 信息。
 *  @return 成功时返回该传感器的名称。 */
inline SensorName nameProperty(const Info& sensorInfo)
{
    return sensorInfo.propertyInterfaces.begin()->second.begin()->first;
}

/** @brief Populate sensor name from the D-Bus object associated with the
 *         sensor. If the object path is /system/chassis/motherboard/dimm0 then
 *         the leaf dimm0 is considered as the sensor name.
 *
 *  @param[in] sensorInfo - Dbus info related to sensor.
 *
 *  @return On success return the sensor name for the sensor.
 */
/* @brief 根据与传感器关联的 D-Bus 对象填充传感器名称。
 *         若对象路径为 /system/chassis/motherboard/dimm0，
 *         则叶子节点 dimm0 被视为传感器名称。
 *  @param[in] sensorInfo - 与传感器相关的 Dbus 信息。
 *  @return 成功时返回该传感器的名称。 */
inline SensorName nameLeaf(const Info& sensorInfo)
{
    return sensorInfo.sensorPath.substr(
        sensorInfo.sensorPath.find_last_of('/') + 1,
        sensorInfo.sensorPath.length());
}

/** @brief Populate sensor name from the D-Bus object associated with the
 *         sensor and the property.
 *         If the object path is /xyz/openbmc_project/inventory/Fan0 and
 *         the property is Present, the leaf Fan0 and the Property is
 *         joined to Fan0_Present as the sensor name.
 *
 *  @param[in] sensorInfo - Dbus info related to sensor.
 *
 *  @return On success return the sensor name for the sensor.
 */
/* @brief 根据与传感器及其属性关联的 D-Bus 对象
 *         填充传感器名称。
 *         若对象路径为 /xyz/openbmc_project/inventory/Fan0 且
 *         属性为 Present，则将叶子节点 Fan0 与该属性
 *         组合成 Fan0_Present 作为传感器名称。
 *  @param[in] sensorInfo - 与传感器相关的 Dbus 信息。
 *  @return 成功时返回该传感器的名称。 */
inline SensorName nameLeafProperty(const Info& sensorInfo)
{
    return nameLeaf(sensorInfo) + "_" + nameProperty(sensorInfo);
}

/** @brief Populate sensor name from the D-Bus object associated with the
 *         sensor. If the object path is /system/chassis/motherboard/cpu0/core0
 *         then the sensor name is cpu0_core0. The leaf and the parent is put
 *         together to get the sensor name.
 *
 *  @param[in] sensorInfo - Dbus info related to sensor.
 *
 *  @return On success return the sensor name for the sensor.
 */
/* @brief 根据与传感器关联的 D-Bus 对象填充传感器名称。
 *         若对象路径为 /system/chassis/motherboard/cpu0/core0，
 *         则传感器名称为 cpu0_core0。将叶子节点与父节点
 *         组合起来得到传感器名称。
 *  @param[in] sensorInfo - 与传感器相关的 Dbus 信息。
 *  @return 成功时返回该传感器的名称。 */
SensorName nameParentLeaf(const Info& sensorInfo);

/**
 *  @brief Helper function to map the dbus info to sensor's assertion status
 *         for the get sensor reading command.
 *
 *  @param[in] sensorInfo - Dbus info related to sensor.
 *  @param[in] path - Dbus object path.
 *  @param[in] interface - Dbus interface.
 *
 *  @return Response for get sensor reading command.
 */
/*  @brief 辅助函数，将 dbus 信息映射为传感器在
 *         get sensor reading 命令中的断言状态。
 *  @param[in] sensorInfo - 与传感器相关的 Dbus 信息。
 *  @param[in] path - Dbus 对象路径。
 *  @param[in] interface - Dbus 接口。
 *  @return get sensor reading 命令的响应。 */
GetSensorResponse mapDbusToAssertion(const Info& sensorInfo,
                                     const InstancePath& path,
                                     const DbusInterface& interface);

#ifndef FEATURE_SENSORS_CACHE
/**
 *  @brief Map the Dbus info to sensor's assertion status in the Get sensor
 *         reading command response.
 *
 *  @param[in] sensorInfo - Dbus info related to sensor.
 *
 *  @return Response for get sensor reading command.
 */
/*  @brief 将 Dbus 信息映射为 Get sensor reading
 *         命令响应中传感器的断言状态。
 *  @param[in] sensorInfo - 与传感器相关的 Dbus 信息。
 *  @return get sensor reading 命令的响应。 */
GetSensorResponse assertion(const Info& sensorInfo);

/**
 *  @brief Maps the Dbus info to the reading field in the Get sensor reading
 *         command response.
 *
 *  @param[in] sensorInfo - Dbus info related to sensor.
 *
 *  @return Response for get sensor reading command.
 */
/*  @brief 将 Dbus 信息映射到 Get sensor reading
 *         命令响应中的 reading 字段。
 *  @param[in] sensorInfo - 与传感器相关的 Dbus 信息。
 *  @return get sensor reading 命令的响应。 */
GetSensorResponse eventdata2(const Info& sensorInfo);

/**
 *  @brief readingAssertion is a case where the entire assertion state field
 *         serves as the sensor value.
 *
 *  @tparam T - type of the dbus property related to sensor.
 *  @param[in] sensorInfo - Dbus info related to sensor.
 *
 *  @return Response for get sensor reading command.
 */
/*  @brief readingAssertion 是指整个断言状态字段
 *         直接作为传感器值的情况。
 *  @tparam T - 与传感器相关的 dbus 属性的类型。
 *  @param[in] sensorInfo - 与传感器相关的 Dbus 信息。
 *  @return get sensor reading 命令的响应。 */
template <typename T>
GetSensorResponse readingAssertion(const Info& sensorInfo)
{
    sdbusplus::bus_t bus{ipmid_get_sd_bus_connection()};
    GetSensorResponse response{};

    enableScanning(response);
    try
    {
        auto service = ipmi::getService(bus, sensorInfo.sensorInterface,
                                        sensorInfo.sensorPath);
        auto propValue = ipmi::getDbusProperty(
            bus, service, sensorInfo.sensorPath,
            sensorInfo.propertyInterfaces.begin()->first,
            sensorInfo.propertyInterfaces.begin()->second.begin()->first);

        setAssertionBytes(static_cast<uint16_t>(std::get<T>(propValue)),
                          response);
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Failed to call readingAssertion, path: {PATH}, interface: {INTERFACE}: {ERROR}",
            "PATH", sensorInfo.sensorPath, "INTERFACE",
            sensorInfo.sensorInterface, "ERROR", e);
    }

    return response;
}

/** @brief Map the Dbus info to the reading field in the Get sensor reading
 *         command response
 *
 *  @tparam T - type of the dbus property related to sensor.
 *  @param[in] sensorInfo - Dbus info related to sensor.
 *
 *  @return Response for get sensor reading command.
 */
/* @brief 将 Dbus 信息映射到 Get sensor reading
 *         命令响应中的 reading 字段
 *  @tparam T - 与传感器相关的 dbus 属性的类型。
 *  @param[in] sensorInfo - 与传感器相关的 Dbus 信息。
 *  @return get sensor reading 命令的响应。 */
template <typename T>
GetSensorResponse readingData(const Info& sensorInfo)
{
    sdbusplus::bus_t bus{ipmid_get_sd_bus_connection()};

    GetSensorResponse response{};

    enableScanning(response);

    auto service = ipmi::getService(bus, sensorInfo.sensorInterface,
                                    sensorInfo.sensorPath);

#ifdef UPDATE_FUNCTIONAL_ON_FAIL
    // Check the OperationalStatus interface for functional property
    // 检查 OperationalStatus 接口中的 functional 属性
    if (sensorInfo.propertyInterfaces.begin()->first == SensorValue::interface)
    {
        bool functional = true;
        try
        {
            auto funcValue = ipmi::getDbusProperty(
                bus, service, sensorInfo.sensorPath,
                "xyz.openbmc_project.State.Decorator.OperationalStatus",
                "Functional");
            functional = std::get<bool>(funcValue);
        }
        catch (...)
        {
            // No-op if Functional property could not be found since this
            // check is only valid for Sensor.Value read for hwmonio
            // 若找不到 Functional 属性则不做任何操作，因为该
            // 检查仅对 hwmonio 的 Sensor.Value 读取有效。
        }
        if (!functional)
        {
            throw SensorFunctionalError();
        }
    }
#endif

    double value{};
    try
    {
        auto propValue = ipmi::getDbusProperty(
            bus, service, sensorInfo.sensorPath,
            sensorInfo.propertyInterfaces.begin()->first,
            sensorInfo.propertyInterfaces.begin()->second.begin()->first);

        value = std::get<T>(propValue) *
                std::pow(10, sensorInfo.scale - sensorInfo.exponentR);
    }
    catch (const std::exception& e)
    {
        lg2::error(
            "Failed to call readingData, path: {PATH}, interface: {INTERFACE}: {ERROR}",
            "PATH", sensorInfo.sensorPath, "INTERFACE",
            sensorInfo.sensorInterface, "ERROR", e);
        return response;
    }

    int32_t rawData =
        (value - sensorInfo.scaledOffset) / sensorInfo.coefficientM;
    constexpr uint8_t reserved_bits_7_6 = 0xC0; // bits[7:6] = 11b (reserved)
    constexpr uint8_t sensorUnitsSignedBits = 2 << 6;
    constexpr uint8_t signedDataFormat = 0x80;
    // if sensorUnits1 [7:6] = 10b, sensor is signed
    // 若 sensorUnits1 [7:6] = 10b，则传感器为有符号数
    int32_t minClamp;
    int32_t maxClamp;
    if ((sensorInfo.sensorUnits1 & sensorUnitsSignedBits) == signedDataFormat)
    {
        minClamp = std::numeric_limits<int8_t>::lowest();
        maxClamp = std::numeric_limits<int8_t>::max();
    }
    else
    {
        minClamp = std::numeric_limits<uint8_t>::lowest();
        maxClamp = std::numeric_limits<uint8_t>::max();
    }
    setReading(static_cast<uint8_t>(std::clamp(rawData, minClamp, maxClamp)),
               response);

    if (!std::isfinite(value))
    {
        response.readingOrStateUnavailable = 1;
    }

    bool critAlarmHigh;
    try
    {
        critAlarmHigh = std::get<bool>(ipmi::getDbusProperty(
            bus, service, sensorInfo.sensorPath,
            SensorThresholdCritical::interface,
            SensorThresholdCritical::property_names::critical_alarm_high));
    }
    catch (const std::exception& e)
    {
        critAlarmHigh = false;
    }
    bool critAlarmLow;
    try
    {
        critAlarmLow = std::get<bool>(ipmi::getDbusProperty(
            bus, service, sensorInfo.sensorPath,
            SensorThresholdCritical::interface,
            SensorThresholdCritical::property_names::critical_alarm_low));
    }
    catch (const std::exception& e)
    {
        critAlarmLow = false;
    }
    bool warningAlarmHigh;
    try
    {
        warningAlarmHigh = std::get<bool>(ipmi::getDbusProperty(
            bus, service, sensorInfo.sensorPath,
            SensorThresholdWarning::interface,
            SensorThresholdWarning::property_names::warning_alarm_high));
    }
    catch (const std::exception& e)
    {
        warningAlarmHigh = false;
    }
    bool warningAlarmLow;
    try
    {
        warningAlarmLow = std::get<bool>(ipmi::getDbusProperty(
            bus, service, sensorInfo.sensorPath,
            SensorThresholdWarning::interface,
            SensorThresholdWarning::property_names::warning_alarm_low));
    }
    catch (const std::exception& e)
    {
        warningAlarmLow = false;
    }
    response.thresholdLevelsStates =
        reserved_bits_7_6 | (static_cast<uint8_t>(critAlarmHigh) << 4) |
        (static_cast<uint8_t>(warningAlarmHigh) << 3) |
        (static_cast<uint8_t>(critAlarmLow) << 1) |
        (static_cast<uint8_t>(warningAlarmLow) << 0);
    return response;
}
#else

/**
 *  @brief Map the Dbus info to sensor's assertion status in the Get sensor
 *         reading command response.
 *
 *  @param[in] id - The sensor id
 *  @param[in] sensorInfo - Dbus info related to sensor.
 *  @param[in] msg - Dbus message from match callback.
 *
 *  @return Response for get sensor reading command.
 */
/*  @brief 将 Dbus 信息映射为 Get sensor reading
 *         命令响应中传感器的断言状态。
 *  @param[in] id - 传感器 id
 *  @param[in] sensorInfo - 与传感器相关的 Dbus 信息。
 *  @param[in] msg - 来自 match 回调的 Dbus 消息。
 *  @return get sensor reading 命令的响应。 */
std::optional<GetSensorResponse> assertion(uint8_t id, const Info& sensorInfo,
                                           const PropertyMap& properties);

/**
 *  @brief Maps the Dbus info to the reading field in the Get sensor reading
 *         command response.
 *
 *  @param[in] id - The sensor id
 *  @param[in] sensorInfo - Dbus info related to sensor.
 *  @param[in] msg - Dbus message from match callback.
 *
 *  @return Response for get sensor reading command.
 */
/*  @brief 将 Dbus 信息映射到 Get sensor reading
 *         命令响应中的 reading 字段。
 *  @param[in] id - 传感器 id
 *  @param[in] sensorInfo - 与传感器相关的 Dbus 信息。
 *  @param[in] msg - 来自 match 回调的 Dbus 消息。
 *  @return get sensor reading 命令的响应。 */
std::optional<GetSensorResponse> eventdata2(uint8_t id, const Info& sensorInfo,
                                            const PropertyMap& properties);

/**
 *  @brief readingAssertion is a case where the entire assertion state field
 *         serves as the sensor value.
 *
 *  @tparam T - type of the dbus property related to sensor.
 *  @param[in] id - The sensor id
 *  @param[in] sensorInfo - Dbus info related to sensor.
 *  @param[in] msg - Dbus message from match callback.
 *
 *  @return Response for get sensor reading command.
 */
/*  @brief readingAssertion 是指整个断言状态字段
 *         直接作为传感器值的情况。
 *  @tparam T - 与传感器相关的 dbus 属性的类型。
 *  @param[in] id - 传感器 id
 *  @param[in] sensorInfo - 与传感器相关的 Dbus 信息。
 *  @param[in] msg - 来自 match 回调的 Dbus 消息。
 *  @return get sensor reading 命令的响应。 */
template <typename T>
std::optional<GetSensorResponse> readingAssertion(
    uint8_t id, const Info& sensorInfo, const PropertyMap& properties)
{
    GetSensorResponse response{};
    enableScanning(response);

    auto iter = properties.find(
        sensorInfo.propertyInterfaces.begin()->second.begin()->first);
    if (iter == properties.end())
    {
        return {};
    }

    setAssertionBytes(static_cast<uint16_t>(std::get<T>(iter->second)),
                      response);

    if (!sensorCacheMap[id].has_value())
    {
        sensorCacheMap[id] = SensorData{};
    }
    sensorCacheMap[id]->response = response;
    return response;
}

/** @brief Get sensor reading from the dbus message from match
 *
 *  @tparam T - type of the dbus property related to sensor.
 *  @param[in] id - The sensor id
 *  @param[in] sensorInfo - Dbus info related to sensor.
 *  @param[in] msg - Dbus message from match callback.
 *
 *  @return Response for get sensor reading command.
 */
/* @brief 从 match 回调的 dbus 消息中获取传感器读数
 *  @tparam T - 与传感器相关的 dbus 属性的类型。
 *  @param[in] id - 传感器 id
 *  @param[in] sensorInfo - 与传感器相关的 Dbus 信息。
 *  @param[in] msg - 来自 match 回调的 Dbus 消息。
 *  @return get sensor reading 命令的响应。 */
template <typename T>
std::optional<GetSensorResponse> readingData(uint8_t id, const Info& sensorInfo,
                                             const PropertyMap& properties)
{
    if (!sensorCacheMap[id].has_value())
    {
        sensorCacheMap[id] = SensorData{};
    }

    auto iter = properties.find("Functional");
    if (iter != properties.end())
    {
        sensorCacheMap[id]->functional = std::get<bool>(iter->second);
    }
    iter = properties.find("Available");
    if (iter != properties.end())
    {
        sensorCacheMap[id]->available = std::get<bool>(iter->second);
    }
#ifdef UPDATE_FUNCTIONAL_ON_FAIL
    if (sensorCacheMap[id])
    {
        if (!sensorCacheMap[id]->functional)
        {
            throw SensorFunctionalError();
        }
    }
#endif

    GetSensorResponse response = sensorCacheMap[id]->response;
    if (!response.scanningEnabled)
    {
        enableScanning(response);
    }

    iter = properties.find(
        sensorInfo.propertyInterfaces.begin()->second.begin()->first);
    if (iter != properties.end())
    {
        double value = std::get<T>(iter->second) *
                       std::pow(10, sensorInfo.scale - sensorInfo.exponentR);
        int32_t rawData =
            (value - sensorInfo.scaledOffset) / sensorInfo.coefficientM;

        constexpr uint8_t sensorUnitsSignedBits = 2 << 6;
        constexpr uint8_t signedDataFormat = 0x80;
        // if sensorUnits1 [7:6] = 10b, sensor is signed
        // 若 sensorUnits1 [7:6] = 10b，则传感器为有符号数
        if ((sensorInfo.sensorUnits1 & sensorUnitsSignedBits) ==
            signedDataFormat)
        {
            if (rawData > std::numeric_limits<int8_t>::max() ||
                rawData < std::numeric_limits<int8_t>::lowest())
            {
                lg2::error("Value out of range");
                throw std::out_of_range("Value out of range");
            }
            setReading(static_cast<int8_t>(rawData), response);
        }
        else
        {
            if (rawData > std::numeric_limits<uint8_t>::max() ||
                rawData < std::numeric_limits<uint8_t>::lowest())
            {
                lg2::error("Value out of range");
                throw std::out_of_range("Value out of range");
            }
            setReading(static_cast<uint8_t>(rawData), response);
        }

        if (!std::isfinite(value))
        {
            response.readingOrStateUnavailable = 1;
        }
    }

    bool critAlarmHigh =
        static_cast<bool>(response.thresholdLevelsStates & (1U << 4));
    auto critAlarmHighIter = properties.find(
        SensorThresholdCritical::property_names::critical_alarm_high);
    if (critAlarmHighIter != properties.end())
    {
        if (const auto* alarm = std::get_if<bool>(&critAlarmHighIter->second))
        {
            critAlarmHigh = *alarm;
        }
    }

    bool critAlarmLow =
        static_cast<bool>(response.thresholdLevelsStates & (1U << 1));
    auto critAlarmLowIter = properties.find(
        SensorThresholdCritical::property_names::critical_alarm_low);
    if (critAlarmLowIter != properties.end())
    {
        if (const auto* alarm = std::get_if<bool>(&critAlarmLowIter->second))
        {
            critAlarmLow = *alarm;
        }
    }

    bool warningAlarmHigh =
        static_cast<bool>(response.thresholdLevelsStates & (1U << 3));
    auto warningAlarmHighIter = properties.find(
        SensorThresholdWarning::property_names::warning_alarm_high);
    if (warningAlarmHighIter != properties.end())
    {
        if (const auto* alarm =
                std::get_if<bool>(&warningAlarmHighIter->second))
        {
            warningAlarmHigh = *alarm;
        }
    }

    bool warningAlarmLow =
        static_cast<bool>(response.thresholdLevelsStates & (1U << 0));
    auto warningAlarmLowIter = properties.find(
        SensorThresholdWarning::property_names::warning_alarm_low);
    if (warningAlarmLowIter != properties.end())
    {
        if (const auto* alarm = std::get_if<bool>(&warningAlarmLowIter->second))
        {
            warningAlarmLow = *alarm;
        }
    }

    constexpr uint8_t reserved_bits_7_6 = 0xC0;
    response.thresholdLevelsStates =
        reserved_bits_7_6 | (static_cast<uint8_t>(critAlarmHigh) << 4) |
        (static_cast<uint8_t>(warningAlarmHigh) << 3) |
        (static_cast<uint8_t>(critAlarmLow) << 1) |
        (static_cast<uint8_t>(warningAlarmLow) << 0);
    sensorCacheMap[id]->response = response;

    return response;
}

#endif // FEATURE_SENSORS_CACHE

} // namespace get

namespace set
{

/** @brief Make a DBus message for a Dbus call
 *  @param[in] updateInterface - Interface name
 *  @param[in] sensorPath - Path of the sensor
 *  @param[in] command - command to be executed
 *  @param[in] sensorInterface - DBus interface of sensor
 *  @return a dbus message
 */
/* @brief 为 Dbus 调用构造 DBus 消息
 *  @param[in] updateInterface - 接口名
 *  @param[in] sensorPath - 传感器路径
 *  @param[in] command - 要执行的命令
 *  @param[in] sensorInterface - 传感器的 DBus 接口
 *  @return 一条 dbus 消息 */
IpmiUpdateData makeDbusMsg(const std::string& updateInterface,
                           const std::string& sensorPath,
                           const std::string& command,
                           const std::string& sensorInterface);

/** @brief Update d-bus based on assertion type sensor data
 *  @param[in] cmdData - input sensor data
 *  @param[in] sensorInfo - sensor d-bus info
 *  @return a IPMI error code
 */
/* @brief 根据断言类型的传感器数据更新 d-bus
 *  @param[in] cmdData - 输入传感器数据
 *  @param[in] sensorInfo - 传感器 d-bus 信息
 *  @return IPMI 错误码 */
ipmi::Cc assertion(const SetSensorReadingReq& cmdData, const Info& sensorInfo);

/** @brief Update d-bus based on a reading assertion
 *  @tparam T - type of d-bus property mapping this sensor
 *  @param[in] cmdData - input sensor data
 *  @param[in] sensorInfo - sensor d-bus info
 *  @return a IPMI error code
 */
/* @brief 根据读数断言更新 d-bus
 *  @tparam T - 映射该传感器的 d-bus 属性类型
 *  @param[in] cmdData - 输入传感器数据
 *  @param[in] sensorInfo - 传感器 d-bus 信息
 *  @return IPMI 错误码 */
template <typename T>
ipmi::Cc readingAssertion(const SetSensorReadingReq& cmdData,
                          const Info& sensorInfo)
{
    auto msg =
        makeDbusMsg("org.freedesktop.DBus.Properties", sensorInfo.sensorPath,
                    "Set", sensorInfo.sensorInterface);

    const auto& interface = sensorInfo.propertyInterfaces.begin();
    msg.append(interface->first);
    for (const auto& property : interface->second)
    {
        msg.append(property.first);
        std::variant<T> value = static_cast<T>(
            (cmdData.assertOffset8_14 << 8) | cmdData.assertOffset0_7);
        msg.append(value);
    }
    return updateToDbus(msg);
}

/** @brief Update d-bus based on a discrete reading
 *  @param[in] cmdData - input sensor data
 *  @param[in] sensorInfo - sensor d-bus info
 *  @return an IPMI error code
 */
/* @brief 根据离散读数更新 d-bus
 *  @param[in] cmdData - 输入传感器数据
 *  @param[in] sensorInfo - 传感器 d-bus 信息
 *  @return IPMI 错误码 */
template <typename T>
ipmi::Cc readingData(const SetSensorReadingReq& cmdData, const Info& sensorInfo)
{
    T raw_value = (sensorInfo.coefficientM * cmdData.reading) +
                  sensorInfo.scaledOffset;

    raw_value *= std::pow(10, sensorInfo.exponentR - sensorInfo.scale);

    auto msg =
        makeDbusMsg("org.freedesktop.DBus.Properties", sensorInfo.sensorPath,
                    "Set", sensorInfo.sensorInterface);

    const auto& interface = sensorInfo.propertyInterfaces.begin();
    msg.append(interface->first);

    for (const auto& property : interface->second)
    {
        msg.append(property.first);
        std::variant<T> value = raw_value;
        msg.append(value);
    }
    return updateToDbus(msg);
}

/** @brief Update d-bus based on eventdata type sensor data
 *  @param[in] cmdData - input sensor data
 *  @param[in] sensorInfo - sensor d-bus info
 *  @return a IPMI error code
 */
/* @brief 根据 eventdata 类型的传感器数据更新 d-bus
 *  @param[in] cmdData - 输入传感器数据
 *  @param[in] sensorInfo - 传感器 d-bus 信息
 *  @return IPMI 错误码 */
ipmi::Cc eventdata(const SetSensorReadingReq& cmdData, const Info& sensorInfo,
                   uint8_t data);

/** @brief Update d-bus based on eventdata1 type sensor data
 *  @param[in] cmdData - input sensor data
 *  @param[in] sensorInfo - sensor d-bus info
 *  @return a IPMI error code
 */
/* @brief 根据 eventdata1 类型的传感器数据更新 d-bus
 *  @param[in] cmdData - 输入传感器数据
 *  @param[in] sensorInfo - 传感器 d-bus 信息
 *  @return IPMI 错误码 */
inline ipmi::Cc eventdata1(const SetSensorReadingReq& cmdData,
                           const Info& sensorInfo)
{
    return eventdata(cmdData, sensorInfo, cmdData.eventData1);
}

/** @brief Update d-bus based on eventdata2 type sensor data
 *  @param[in] cmdData - input sensor data
 *  @param[in] sensorInfo - sensor d-bus info
 *  @return a IPMI error code
 */
/* @brief 根据 eventdata2 类型的传感器数据更新 d-bus
 *  @param[in] cmdData - 输入传感器数据
 *  @param[in] sensorInfo - 传感器 d-bus 信息
 *  @return IPMI 错误码 */
inline ipmi::Cc eventdata2(const SetSensorReadingReq& cmdData,
                           const Info& sensorInfo)
{
    return eventdata(cmdData, sensorInfo, cmdData.eventData2);
}

/** @brief Update d-bus based on eventdata3 type sensor data
 *  @param[in] cmdData - input sensor data
 *  @param[in] sensorInfo - sensor d-bus info
 *  @return a IPMI error code
 */
/* @brief 根据 eventdata3 类型的传感器数据更新 d-bus
 *  @param[in] cmdData - 输入传感器数据
 *  @param[in] sensorInfo - 传感器 d-bus 信息
 *  @return IPMI 错误码 */
inline ipmi::Cc eventdata3(const SetSensorReadingReq& cmdData,
                           const Info& sensorInfo)
{
    return eventdata(cmdData, sensorInfo, cmdData.eventData3);
}

} // namespace set

namespace notify
{

/** @brief Make a DBus message for a Dbus call
 *  @param[in] updateInterface - Interface name
 *  @param[in] sensorPath - Path of the sensor
 *  @param[in] command - command to be executed
 *  @param[in] sensorInterface - DBus interface of sensor
 *  @return a dbus message
 */
/* @brief 为 Dbus 调用构造 DBus 消息
 *  @param[in] updateInterface - 接口名
 *  @param[in] sensorPath - 传感器路径
 *  @param[in] command - 要执行的命令
 *  @param[in] sensorInterface - 传感器的 DBus 接口
 *  @return 一条 dbus 消息 */
IpmiUpdateData makeDbusMsg(const std::string& updateInterface,
                           const std::string& sensorPath,
                           const std::string& command,
                           const std::string& sensorInterface);

/** @brief Update d-bus based on assertion type sensor data
 *  @param[in] interfaceMap - sensor interface
 *  @param[in] cmdData - input sensor data
 *  @param[in] sensorInfo - sensor d-bus info
 *  @return a IPMI error code
 */
/* @brief 根据断言类型的传感器数据更新 d-bus
 *  @param[in] interfaceMap - 传感器接口
 *  @param[in] cmdData - 输入传感器数据
 *  @param[in] sensorInfo - 传感器 d-bus 信息
 *  @return IPMI 错误码 */
ipmi::Cc assertion(const SetSensorReadingReq& cmdData, const Info& sensorInfo);

} // namespace notify

namespace inventory
{

namespace get
{

#ifndef FEATURE_SENSORS_CACHE

/**
 *  @brief Map the Dbus info to sensor's assertion status in the Get sensor
 *         reading command response.
 *
 *  @param[in] sensorInfo - Dbus info related to sensor.
 *
 *  @return Response for get sensor reading command.
 */
/*  @brief 将 Dbus 信息映射为 Get sensor reading
 *         命令响应中传感器的断言状态。
 *  @param[in] sensorInfo - 与传感器相关的 Dbus 信息。
 *  @return get sensor reading 命令的响应。 */
GetSensorResponse assertion(const Info& sensorInfo);

#else

/**
 *  @brief Map the Dbus info to sensor's assertion status in the Get sensor
 *         reading command response.
 *
 *  @param[in] id - The sensor id
 *  @param[in] sensorInfo - Dbus info related to sensor.
 *  @param[in] msg - Dbus message from match callback.
 *
 *  @return Response for get sensor reading command.
 */
/*  @brief 将 Dbus 信息映射为 Get sensor reading
 *         命令响应中传感器的断言状态。
 *  @param[in] id - 传感器 id
 *  @param[in] sensorInfo - 与传感器相关的 Dbus 信息。
 *  @param[in] msg - 来自 match 回调的 Dbus 消息。
 *  @return get sensor reading 命令的响应。 */
std::optional<GetSensorResponse> assertion(uint8_t id, const Info& sensorInfo,
                                           const PropertyMap& properties);

#endif

} // namespace get

} // namespace inventory
} // namespace sensor
} // namespace ipmi
