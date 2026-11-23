
/**
 * @file channel_mgmt.hpp
 * @brief 通道管理核心的声明。
 */
/*  @file channel_mgmt.hpp
 *  @brief 通道管理核心的声明。 */
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
/*  // Copyright (c) 2018 Intel Corporation
 *  //
 *  // Licensed under the Apache License, Version 2.0 (the "License");
 *  // you may not use this file except in compliance with the License.
 *  // You may obtain a copy of the License at
 *  //
 *  //      http://www.apache.org/licenses/LICENSE-2.0
 *  //
 *  // Unless required by applicable law or agreed to in writing, software
 *  // distributed under the License is distributed on an "AS IS" BASIS,
 *  // WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  // See the License for the specific language governing permissions and
 *  // limitations under the License. */

#pragma once
#include "channel_layer.hpp"
#include "ipmid/api-types.hpp"

#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/interprocess/sync/named_recursive_mutex.hpp>
#include <nlohmann/json.hpp>
#include <sdbusplus/bus.hpp>

#include <cstdint>
#include <ctime>
#include <variant>

namespace ipmi
{

using Json = nlohmann::json;

using DbusVariant = std::variant<std::vector<std::string>, std::string, bool>;

using DbusChObjProperties = std::vector<std::pair<std::string, DbusVariant>>;

static constexpr const char* ipmiChannelMutex = "ipmi_channel_mutex";
static constexpr const char* ipmiChMutexCleanupLockFile =
    "/run/ipmi/ipmi_channel_mutex_cleanup";

/** @struct ChannelAccessData
 *
 *  Structure to store both non-volatile and volatile channel access information
 *  as used by IPMI specification (refer spec sec 22.22 to 22.24)
 */
/* @struct ChannelAccessData
 *   用于存储 IPMI 规范所用的非易失性和易失性通道访问信息
 *   的结构体（参见规范 22.22 至 22.24 节） */
struct ChannelAccessData
{
    ChannelAccess chNonVolatileData;
    ChannelAccess chVolatileData;
};

/** @struct ChannelProperties
 *
 *  Structure for channel information - base structure to get all information
 * about the channel.(refer spec sec 22.22 to 22.24)
 */
/* @struct ChannelProperties
 *   通道信息的结构体——获取通道全部信息的基础结构体。
 * （参见规范 22.22 至 22.24 节） */
struct ChannelProperties
{
    std::string chName;
    uint8_t chID;
    bool isChValid;
    uint8_t activeSessCount;
    ChannelInfo chInfo;
    ChannelAccessData chAccess;
    size_t maxTransferSize;
    bool isManagementNIC;
};

class ChannelConfig;

ChannelConfig& getChannelConfigObject();

class ChannelConfig
{
  public:
    ChannelConfig(const ChannelConfig&) = delete;
    ChannelConfig& operator=(const ChannelConfig&) = delete;
    ChannelConfig(ChannelConfig&&) = delete;
    ChannelConfig& operator=(ChannelConfig&&) = delete;

    ~ChannelConfig();
    ChannelConfig();

    /** @brief determines valid channel
     *
     *  @param[in] chNum - channel number
     *
     *  @return true if valid, false otherwise
     */
    /* @brief 判断通道是否有效
     *  @param[in] chNum - 通道号
     *  @return 有效返回 true，否则返回 false */
    bool isValidChannel(const uint8_t chNum);

    /** @brief determines valid authentication type
     *
     *  @param[in] chNum - channel number
     *  @param[in] authType - authentication type
     *
     *  @return true if valid, false otherwise
     */
    /* @brief 判断认证类型是否有效
     *  @param[in] chNum - 通道号
     *  @param[in] authType - 认证类型
     *  @return 有效返回 true，否则返回 false */
    bool isValidAuthType(const uint8_t chNum, const EAuthType& authType);

    /** @brief function to get channel name from channel number
     *
     *  @param[in] chNum - channel number index
     *
     *  @return network channel interface name
     */
    /* @brief 从通道号获取通道名的函数
     *  @param[in] chNum - 通道号索引
     *  @return 网络通道接口名 */
    std::string getChannelName(const uint8_t chNum);

    /** @brief function to get channel number from channel name
     *
     *  @param[in] chName - channel name
     *
     *  @return network channel interface number
     */
    /* @brief 从通道名获取通道号的函数
     *  @param[in] chName - 通道名
     *  @return 网络通道接口号 */

    uint8_t getChannelByName(const std::string& chName)
    {
        return convertToChannelNumberFromChannelName(chName);
    }

    /** @brief determines supported session type of a channel
     *
     *  @param[in] chNum - channel number
     *
     *  @return EChannelSessSupported - supported session type
     */
    /* @brief 确定某通道支持的会话类型
     *  @param[in] chNum - 通道号
     *  @return EChannelSessSupported - 支持的会话类型 */
    EChannelSessSupported getChannelSessionSupport(const uint8_t chNum);

    /** @brief determines number of active sessions on a channel
     *
     *  @param[in] chNum - channel number
     *
     *  @return number of active sessions
     */
    /* @brief 确定某通道上的活动会话数
     *  @param[in] chNum - 通道号
     *  @return 活动会话数 */
    int getChannelActiveSessions(const uint8_t chNum);

    /** @brief determines maximum transfer size for a channel
     *
     *  @param[in] chNum - channel number
     *
     *  @return maximum bytes that can be transferred on this channel
     */
    /* @brief 确定某通道的最大传输大小
     *  @param[in] chNum - 通道号
     *  @return 该通道上可传输的最大字节数 */
    size_t getChannelMaxTransferSize(uint8_t chNum);

    /** @brief provides channel info details
     *
     *  @param[in] chNum - channel number
     *  @param[out] chInfo - channel info details
     *
     *  @return ccSuccess for success, others for failure.
     */
    /* @brief 提供通道信息详情
     *  @param[in] chNum - 通道号
     *  @param[out] chInfo - 通道信息详情
     *  @return 成功返回 ccSuccess，否则返回其他值。 */
    Cc getChannelInfo(const uint8_t chNum, ChannelInfo& chInfo);

    /** @brief provides channel access data
     *
     *  @param[in] chNum - channel number
     *  @param[out] chAccessData - channel access data
     *
     *  @return ccSuccess for success, others for failure.
     */
    /* @brief 提供通道访问数据
     *  @param[in] chNum - 通道号
     *  @param[out] chAccessData - 通道访问数据
     *  @return 成功返回 ccSuccess，否则返回其他值。 */
    Cc getChannelAccessData(const uint8_t chNum, ChannelAccess& chAccessData);

    /** @brief to set channel access data
     *
     *  @param[in] chNum - channel number
     *  @param[in] chAccessData - channel access data
     *  @param[in] setFlag - flag to indicate updatable fields
     *
     *  @return ccSuccess for success, others for failure.
     */
    /* @brief 设置通道访问数据
     *  @param[in] chNum - 通道号
     *  @param[in] chAccessData - 通道访问数据
     *  @param[in] setFlag - 指示可更新字段的标志
     *  @return 成功返回 ccSuccess，否则返回其他值。 */
    Cc setChannelAccessData(const uint8_t chNum,
                            const ChannelAccess& chAccessData,
                            const uint8_t setFlag);

    /** @brief to get channel access data persistent data
     *
     *  @param[in] chNum - channel number
     *  @param[out] chAccessData - channel access data
     *
     *  @return ccSuccess for success, others for failure.
     */
    /* @brief 获取通道访问持久数据
     *  @param[in] chNum - 通道号
     *  @param[out] chAccessData - 通道访问数据
     *  @return 成功返回 ccSuccess，否则返回其他值。 */
    Cc getChannelAccessPersistData(const uint8_t chNum,
                                   ChannelAccess& chAccessData);

    /** @brief to set channel access data persistent data
     *
     *  @param[in] chNum - channel number
     *  @param[in] chAccessData - channel access data
     *  @param[in] setFlag - flag to indicate updatable fields
     *
     *  @return ccSuccess for success, others for failure.
     */
    /* @brief 设置通道访问持久数据
     *  @param[in] chNum - 通道号
     *  @param[in] chAccessData - 通道访问数据
     *  @param[in] setFlag - 指示可更新字段的标志
     *  @return 成功返回 ccSuccess，否则返回其他值。 */
    Cc setChannelAccessPersistData(const uint8_t chNum,
                                   const ChannelAccess& chAccessData,
                                   const uint8_t setFlag);

    /** @brief provides supported authentication type for the channel
     *
     *  @param[in] chNum - channel number
     *  @param[out] authTypeSupported - supported authentication type
     *
     *  @return ccSuccess for success, others for failure.
     */
    /* @brief 提供该通道支持的认证类型
     *  @param[in] chNum - 通道号
     *  @param[out] authTypeSupported - 支持的认证类型
     *  @return 成功返回 ccSuccess，否则返回其他值。 */
    Cc getChannelAuthTypeSupported(const uint8_t chNum,
                                   uint8_t& authTypeSupported);

    /** @brief provides enabled authentication type for the channel
     *
     *  @param[in] chNum - channel number
     *  @param[in] priv - privilege
     *  @param[out] authType - enabled authentication type
     *
     *  @return ccSuccess for success, others for failure.
     */
    /* @brief 提供该通道已启用的认证类型
     *  @param[in] chNum - 通道号
     *  @param[in] priv - 权限
     *  @param[out] authType - 已启用的认证类型
     *  @return 成功返回 ccSuccess，否则返回其他值。 */
    Cc getChannelEnabledAuthType(const uint8_t chNum, const uint8_t priv,
                                 EAuthType& authType);

    /** @brief convert to channel privilege from system privilege
     *
     *  @param[in] value - privilege value
     *
     *  @return Channel privilege
     */
    /* @brief 从系统权限转换为通道权限
     *  @param[in] value - 权限值
     *  @return 通道权限 */
    CommandPrivilege convertToPrivLimitIndex(const std::string& value);

    /** @brief function to write persistent channel configuration to config file
     *
     *  @return 0 for success, -errno for failure.
     */
    /* @brief 将持久通道配置写入配置文件的函数
     *  @return 成功返回 0，失败返回 -errno。 */
    int writeChannelPersistData();

    /** @brief function to write volatile channel configuration to config file
     *
     *  @return 0 for success, -errno for failure.
     */
    /* @brief 将易失性通道配置写入配置文件的函数
     *  @return 成功返回 0，失败返回 -errno。 */
    int writeChannelVolatileData();

    /** @brief Returns the IPMI channel ID authorized to push IPMI privilege
     * changes to phosphor-user-manager. Any channel access changes made on
     * any other channel are ignored.
     *
     *  @return IPMI channel ID as defined in channel_config.json
     */
    /* @brief 返回被授权向 phosphor-user-manager 推送 IPMI 权限
     *  变更的 IPMI 通道号。在其他任何通道上所做的通道访问
     *  变更都会被忽略。
     *  @return channel_config.json 中定义的 IPMI 通道号 */
    uint8_t getManagementNICID();

  private:
    uint32_t signalFlag = 0;
    std::unique_ptr<boost::interprocess::named_recursive_mutex> channelMutex{
        nullptr};
    std::array<ChannelProperties, maxIpmiChannels> channelData;
    std::time_t nvFileLastUpdatedTime;
    std::time_t voltFileLastUpdatedTime;
    boost::interprocess::file_lock mutexCleanupLock;
    sdbusplus::bus_t bus;
    bool signalHndlrObjectState = false;
    boost::interprocess::file_lock sigHndlrLock;

    /** @brief function to initialize persistent channel configuration
     *
     */
    /* @brief 初始化持久通道配置的函数 */
    void initChannelPersistData();

    /** @brief function to set default channel configuration based on channel
     * number
     *
     *  @param[in] chNum - channel number
     *  @param[in] chName - channel name
     */
    /* @brief 根据通道号设置默认通道配置的
     * 函数
     *   @param[in] chNum - 通道号
     *   @param[in] chName - 通道名 */
    void setDefaultChannelConfig(const uint8_t chNum,
                                 const std::string& chName);

    /** @brief function to load all channel configuration
     *
     *  @return 0 for success, -errno for failure.
     */
    /* @brief 加载所有通道配置的函数
     *  @return 成功返回 0，失败返回 -errno。 */
    int loadChannelConfig();

    /** @brief function to read persistent channel data
     *
     *  @return 0 for success, -errno for failure.
     */
    /* @brief 读取通道持久数据的函数
     *  @return 成功返回 0，失败返回 -errno。 */
    int readChannelPersistData();

    /** @brief function to read volatile channel data
     *
     *  @return 0 for success, -errno for failure.
     */
    /* @brief 读取通道易失性数据的函数
     *  @return 成功返回 0，失败返回 -errno。 */
    int readChannelVolatileData();

    /** @brief function to check and reload persistent channel data
     *
     *  @return 0 for success, -errno for failure.
     */
    /* @brief 检查并重新加载通道持久数据的函数
     *  @return 成功返回 0，失败返回 -errno。 */
    int checkAndReloadNVData();

    /** @brief function to check and reload volatile channel data
     *
     *  @return 0 for success, -errno for failure.
     */
    /* @brief 检查并重新加载通道易失性数据的函数
     *  @return 成功返回 0，失败返回 -errno。 */
    int checkAndReloadVolatileData();

    /** @brief function to sync channel privilege with system network channel
     * privilege
     *
     *  @return 0 for success, -errno for failure.
     */
    /* @brief 将通道权限与系统网络通道
     * 权限同步的函数
     *  @return 成功返回 0，失败返回 -errno。 */
    int syncNetworkChannelConfig();

    /** @brief function to set D-Bus property value
     *
     *  @param[in] service - service name
     *  @param[in] objPath - object path
     *  @param[in] interface - interface
     *  @param[in] property - property name
     *  @param[in] value - property value
     *
     *  @return 0 for success, -errno for failure.
     */
    /* @brief 设置 D-Bus 属性值的函数
     *  @param[in] service - 服务名
     *  @param[in] objPath - 对象路径
     *  @param[in] interface - interface
     *  @param[in] property - 属性名
     *  @param[in] value - 属性值
     *  @return 成功返回 0，失败返回 -errno。 */
    int setDbusProperty(const std::string& service, const std::string& objPath,
                        const std::string& interface,
                        const std::string& property, const DbusVariant& value);

    /** @brief function to read json config file
     *
     *  @param[in] configFile - configuration file name
     *
     *  @return Json object
     */
    /* @brief 读取 json 配置文件的函数
     *  @param[in] configFile - 配置文件名
     *  @return Json 对象 */
    Json readJsonFile(const std::string& configFile);

    /** @brief function to write json config file
     *
     *  @param[in] configFile - configuration file name
     *  @param[in] jsonData - json object
     *
     *  @return 0 for success, -errno for failure.
     */
    /* @brief 写入 json 配置文件的函数
     *  @param[in] configFile - 配置文件名
     *  @param[in] jsonData - json 对象
     *  @return 成功返回 0，失败返回 -errno。 */
    int writeJsonFile(const std::string& configFile, const Json& jsonData);

    /** @brief function to convert system access mode to Channel access mode
     * type
     *
     *  @param[in] mode - access mode in string
     *
     *  @return Channel access mode.
     */
    /* @brief 将系统访问模式转换为通道访问模式
     * 类型的函数
     *  @param[in] mode - 字符串形式的访问模式
     *  @return 通道访问模式。 */
    EChannelAccessMode convertToAccessModeIndex(const std::string& mode);

    /** @brief function to convert access mode value to string
     *
     *  @param[in] value - access mode value
     *
     *  @return access mode in string
     */
    /* @brief 将访问模式值转换为字符串的函数
     *  @param[in] value - 访问模式值
     *  @return 字符串形式的访问模式 */
    std::string convertToAccessModeString(const uint8_t value);

    /** @brief function to convert privilege value to string
     *
     *  @param[in] value - privilege value
     *
     *  @return privilege in string
     */
    /* @brief 将权限值转换为字符串的函数
     *  @param[in] value - 权限值
     *  @return 字符串形式的权限 */
    std::string convertToPrivLimitString(const uint8_t value);

    /** @brief function to convert session support string to value type
     *
     *  @param[in] value - session support type in string
     *
     *  @return support session type
     */
    /* @brief 将会话支持字符串转换为值类型的函数
     *  @param[in] value - 字符串形式的会话支持类型
     *  @return 支持的会话类型 */
    EChannelSessSupported convertToSessionSupportIndex(
        const std::string& value);

    /** @brief function to convert medium type string to value type
     *
     *  @param[in] value - medium type in string
     *
     *  @return channel medium type
     */
    /* @brief 将介质类型字符串转换为值类型的函数
     *  @param[in] value - 字符串形式的介质类型
     *  @return 通道介质类型 */
    EChannelMediumType convertToMediumTypeIndex(const std::string& value);

    /** @brief function to convert protocol type string to value type
     *
     *  @param[in] value - protocol type in string
     *
     *  @return channel protocol type
     */
    /* @brief 将协议类型字符串转换为值类型的函数
     *  @param[in] value - 字符串形式的协议类型
     *  @return 通道协议类型 */
    EChannelProtocolType convertToProtocolTypeIndex(const std::string& value);

    /** @brief function to convert channel name to the IPMI channel number.
     *
     *  @param[in] chName - the channel name defined in the JSON input file
     *  (i.e. LAN1)
     *
     *  @return IPMI channel number
     */
    /* @brief 将通道名转换为 IPMI 通道号的函数。
     *  @param[in] chName - JSON 输入文件中定义的通道名
     * （即 LAN1）
     *  @return IPMI 通道号 */
    int convertToChannelNumberFromChannelName(const std::string& chName);

    /** @brief function to handle Channel access property update through the
     * D-Bus handler.
     *
     *  @param[in] path - D-Bus path to the network element (i.e. eth0)
     *  @param[in] chProperties - D-Bus channel properties
     */
    /* @brief 通过 D-Bus 处理函数处理通道访问
     * 属性更新的函数。
     *   @param[in] path - 网络元素的 D-Bus 路径（即 eth0）
     *   @param[in] chProperties - D-Bus 通道属性 */
    void processChAccessPropChange(const std::string& path,
                                   const DbusChObjProperties& chProperties);

    /** @brief function to retrieve last modification time for the named file
     *
     *  @param[in] fileName - the name of the file for which to acquire
     *  timestamp data
     *
     *  @return time the file was last modified
     */
    /* @brief 获取指定文件最后修改时间的函数
     *  @param[in] fileName - 要获取时间戳数据的
     *  文件名
     *  @return 文件的最后修改时间 */
    std::time_t getUpdatedFileTime(const std::string& fileName);

    /** @brief function to convert the DBus path to a network channel name
     *
     *  @param[in] path - The DBus path to the device
     *
     *  @return network channel name (i.e. eth0)
     */
    /* @brief 将 DBus 路径转换为网络通道名的函数
     *  @param[in] path - 设备的 DBus 路径
     *  @return 网络通道名（即 eth0） */
    std::string getChannelNameFromPath(const std::string& path);
};

} // namespace ipmi
