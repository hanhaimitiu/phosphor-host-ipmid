
/**
 * @file user_mgmt.hpp
 * @brief 用户管理核心的声明。
 */
/*  @file user_mgmt.hpp
 *  @brief 用户管理核心的声明。 */
/*.
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
/* .
 *  // Copyright (c) 2018 Intel Corporation
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
#include "user_layer.hpp"

#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/interprocess/sync/named_recursive_mutex.hpp>
#include <ipmid/api.hpp>
#include <sdbusplus/bus.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <variant>

namespace ipmi
{

static constexpr const char* dBusPropertiesInterface =
    "org.freedesktop.DBus.Properties";
static constexpr const char* getAllPropertiesMethod = "GetAll";
static constexpr const char* propertiesChangedSignal = "PropertiesChanged";
static constexpr const char* setPropertiesMethod = "Set";

// Object Manager related
// Object Manager 相关
static constexpr const char* dBusObjManager =
    "org.freedesktop.DBus.ObjectManager";
static constexpr const char* getManagedObjectsMethod = "GetManagedObjects";
// Object Manager signals
// Object Manager 信号
static constexpr const char* intfAddedSignal = "InterfacesAdded";
static constexpr const char* intfRemovedSignal = "InterfacesRemoved";

static constexpr const char* ipmiUserMutex = "ipmi_usr_mutex";
static constexpr const char* ipmiMutexCleanupLockFile =
    "/run/ipmi/ipmi_usr_mutex_cleanup";
static constexpr const char* ipmiUserSignalLockFile =
    "/run/ipmi/ipmi_usr_signal_mutex";
static constexpr const char* ipmiUserDataFile = "/var/lib/ipmi/ipmi_user.json";
static constexpr const char* ipmiGrpName = "ipmi";
static constexpr size_t privNoAccess = 0xF;
static constexpr size_t privMask = 0xF;

// User manager related
// User manager 相关
static constexpr const char* userMgrService =
    "xyz.openbmc_project.User.Manager";
static constexpr const char* userMgrObjBasePath = "/xyz/openbmc_project/user";
static constexpr const char* userObjBasePath = "/xyz/openbmc_project/user";
static constexpr const char* userMgrInterface =
    "xyz.openbmc_project.User.Manager";
static constexpr const char* usersInterface =
    "xyz.openbmc_project.User.Attributes";
static constexpr const char* deleteUserInterface =
    "xyz.openbmc_project.Object.Delete";

static constexpr const char* createUserMethod = "CreateUser";
static constexpr const char* deleteUserMethod = "Delete";
static constexpr const char* renameUserMethod = "RenameUser";
// User manager signal members
// User manager 信号成员
static constexpr const char* userRenamedSignal = "UserRenamed";
// Mgr interface properties
// Mgr 接口属性
static constexpr const char* allPrivProperty = "AllPrivileges";
static constexpr const char* allGrpProperty = "AllGroups";
// User interface properties
// User 接口属性
static constexpr const char* userPrivProperty = "UserPrivilege";
static constexpr const char* userGrpProperty = "UserGroups";
static constexpr const char* userEnabledProperty = "UserEnabled";

static std::array<std::string, (PRIVILEGE_OEM + 1)> ipmiPrivIndex = {
    "priv-reserved", // PRIVILEGE_RESERVED - 0
    "priv-callback", // PRIVILEGE_CALLBACK - 1
    "priv-user",     // PRIVILEGE_USER - 2
    "priv-operator", // PRIVILEGE_OPERATOR - 3
    "priv-admin",    // PRIVILEGE_ADMIN - 4
    "priv-custom"    // PRIVILEGE_OEM - 5
};

// Inline functions for safe username buffer operations
// 用于安全操作用户名缓冲区的内联函数
inline size_t safeUsernameLength(const uint8_t* buf)
{
    return strnlen(reinterpret_cast<const char*>(buf), ipmiMaxUserName);
}

inline std::string safeUsernameString(const uint8_t* buf)
{
    return std::string(reinterpret_cast<const char*>(buf),
                       safeUsernameLength(buf));
}

inline void safeUsernameAssign(std::string& str, const uint8_t* buf)
{
    size_t len = safeUsernameLength(buf);
    str.assign(reinterpret_cast<const char*>(buf), len);
}

inline void safeUsernameCopyToBuffer(uint8_t* buf, size_t bufSize,
                                     const std::string& str)
{
    std::memset(buf, 0, bufSize);
    const size_t n = std::min(str.size(), bufSize);
    std::memcpy(buf, str.data(), n);
}

using DbusUserPropVariant =
    std::variant<std::vector<std::string>, std::string, bool>;

using DbusUserObjPath = sdbusplus::object_path;

using DbusUserObjProperties =
    std::vector<std::pair<std::string, DbusUserPropVariant>>;

using DbusUserObjValue = std::map<std::string, DbusUserObjProperties>;

/**
 * @enum User update events.
 */
/*  @enum 用户更新事件。 */
enum class UserUpdateEvent
{
    reservedEvent,
    userCreated,
    userDeleted,
    userRenamed,
    userGrpUpdated,
    userPrivUpdated,
    userStateUpdated
};

/** @struct UserPrivAccess
 *
 *  Structure for user privilege access (refer spec sec 22.22)
 */
/* @struct UserPrivAccess
 *   用户特权访问的结构体（参见规范 22.22 节） */
struct UserPrivAccess
{
    uint8_t privilege;
    bool ipmiEnabled;
    bool linkAuthEnabled;
    bool accessCallback;
};

/** @struct UserInfo
 *
 *  Structure for user related information
 */
/* @struct UserInfo
 *   用户相关信息结构体 */
struct UserInfo
{
    uint8_t userName[ipmiMaxUserName];
    UserPrivAccess userPrivAccess[ipmiMaxChannels];
    bool userEnabled;
    bool userInSystem;
    bool fixedUserName;
    PayloadAccess payloadAccess[ipmiMaxChannels];
};

/** @struct UsersTbl
 *
 *  Structure for array of user related information
 */
/* @struct UsersTbl
 *   用户相关信息数组的结构体 */
struct UsersTbl
{
    //+1 to map with UserId directly. UserId 0 is reserved.
    // +1 以便直接与 UserId 对应。UserId 0 为保留值。
    UserInfo user[ipmiMaxUsers + 1];
};

class UserAccess;

UserAccess& getUserAccessObject();

class UserAccess
{
  public:
    UserAccess(const UserAccess&) = delete;
    UserAccess& operator=(const UserAccess&) = delete;
    UserAccess(UserAccess&&) = delete;
    UserAccess& operator=(UserAccess&&) = delete;

    ~UserAccess();
    UserAccess();

    /** @brief determines valid channel
     *
     *  @param[in] chNum - channel number
     *
     *  @return true if valid, false otherwise
     */
    /* @brief 判断通道是否有效
     *   @param[in] chNum - 通道号
     *   @return 有效返回 true，否则返回 false */
    static bool isValidChannel(const uint8_t chNum);

    /** @brief determines valid userId
     *
     *  @param[in] userId - user id
     *
     *  @return true if valid, false otherwise
     */
    /* @brief 判断用户 ID 是否有效
     *   @param[in] userId - 用户 ID
     *   @return 有效返回 true，否则返回 false */
    static bool isValidUserId(const uint8_t userId);

    /** @brief determines valid user privilege
     *
     *  @param[in] priv - Privilege
     *
     *  @return true if valid, false otherwise
     */
    /* @brief 判断用户权限是否有效
     *   @param[in] priv - 权限
     *   @return 有效返回 true，否则返回 false */
    static bool isValidPrivilege(const uint8_t priv);

    /** @brief determines sync index to be mapped with common-user-management
     *
     *  @return Index which will be used as sync index
     */
    /* @brief 确定要与 common-user-management 映射的同步索引
     *   @return 用作同步索引的索引 */
    static uint8_t getUsrMgmtSyncIndex();

    /** @brief Converts system privilege to IPMI privilege
     *
     *  @param[in] value - Privilege in string
     *
     *  @return CommandPrivilege - IPMI privilege type
     */
    /* @brief 将系统权限转换为 IPMI 权限
     *   @param[in] value - 字符串形式的权限
     *   @return CommandPrivilege - IPMI 权限类型 */
    static CommandPrivilege convertToIPMIPrivilege(const std::string& value);

    /** @brief Converts IPMI privilege to system privilege
     *
     *  @param[in] value - IPMI privilege
     *
     *  @return System privilege in string
     */
    /* @brief 将 IPMI 权限转换为系统权限
     *   @param[in] value - IPMI 权限
     *   @return 字符串形式的系统权限 */
    static std::string convertToSystemPrivilege(const CommandPrivilege& value);

    /** @brief determines whether user name is valid
     *
     *  @param[in] userNameInChar - user name
     *
     *  @return true if valid, false otherwise
     */
    /* @brief 判断用户名是否有效
     *   @param[in] userNameInChar - 用户名
     *   @return 有效返回 true，否则返回 false */
    bool isValidUserName(const std::string& userName);

    /** @brief determines whether ipmi is in available groups list
     *
     * @return true if ipmi group is present, false otherwise
     */
    /* @brief 判断 ipmi 是否在可用组列表中
     *  @return 存在 ipmi 组返回 true，否则返回 false */
    bool isIpmiInAvailableGroupList();

    /** @brief provides user id of the user
     *
     *  @param[in] userName - user name
     *
     *  @return user id of the user, else invalid user id (0xFF), if user not
     * found
     */
    /* @brief 提供用户的用户 ID
     *  @param[in] userName - 用户名
     *  @return 用户的用户 ID，找不到用户时返回
     * 无效的用户 ID（0xFF） */
    uint8_t getUserId(const std::string& userName);

    /** @brief provides user information
     *
     *  @param[in] userId - user id
     *
     *  @return UserInfo for the specified user id
     */
    /* @brief 提供用户信息
     *   @param[in] userId - 用户 ID
     *   @return 指定用户 ID 的 UserInfo */
    UserInfo* getUserInfo(const uint8_t userId);

    /** @brief sets user information
     *
     *  @param[in] userId - user id
     *  @param[in] userInfo - user information
     *
     */
    /* @brief 设置用户信息
     *   @param[in] userId - 用户 ID
     *   @param[in] userInfo - 用户信息 */
    void setUserInfo(const uint8_t userId, UserInfo* userInfo);

    /** @brief provides user name
     *
     *  @param[in] userId - user id
     *  @param[out] userName - user name
     *
     *  @return ccSuccess for success, others for failure.
     */
    /* @brief 提供用户名
     *   @param[in] userId - 用户 ID
     *   @param[out] userName - 用户名
     *   @return 成功返回 ccSuccess，否则返回其他值。 */
    Cc getUserName(const uint8_t userId, std::string& userName);

    /** @brief to set user name
     *
     *  @param[in] userId - user id
     *  @param[in] userName - user name
     *
     *  @return ccSuccess for success, others for failure.
     */
    /* @brief 设置用户名
     *   @param[in] userId - 用户 ID
     *   @param[in] userName - 用户名
     *   @return 成功返回 ccSuccess，否则返回其他值。 */
    Cc setUserName(const uint8_t userId, const std::string& userName);

    /** @brief to set user enabled state
     *
     *  @param[in] userId - user id
     *  @param[in] enabledState - enabled state of the user
     *
     *  @return ccSuccess for success, others for failure.
     */
    /* @brief 设置用户启用状态
     *   @param[in] userId - 用户 ID
     *   @param[in] enabledState - 用户的启用状态
     *   @return 成功返回 ccSuccess，否则返回其他值。 */
    Cc setUserEnabledState(const uint8_t userId, const bool& enabledState);

    /** @brief to set user password
     *
     *  @param[in] userId - user id
     *  @param[in] userPassword  - new password of the user
     *
     *  @return ccSuccess for success, others for failure.
     */
    /* @brief 设置用户密码
     *   @param[in] userId - 用户 ID
     *   @param[in] userPassword  - 用户的新密码
     *   @return 成功返回 ccSuccess，否则返回其他值。 */
    Cc setUserPassword(const uint8_t userId, const char* userPassword);

    /** @brief to set special user password
     *
     *  @param[in] userName - user name
     *  @param[in] userPassword  - new password of the user
     *
     *  @return ccSuccess for success, others for failure.
     */
    /* @brief 设置特殊用户密码
     *   @param[in] userName - 用户名
     *   @param[in] userPassword  - 用户的新密码
     *   @return 成功返回 ccSuccess，否则返回其他值。 */
    Cc setSpecialUserPassword(const std::string& userName,
                              const SecureString& userPassword);

    /** @brief to set user privilege and access details
     *
     *  @param[in] userId - user id
     *  @param[in] chNum - channel number
     *  @param[in] privAccess - privilege access
     *  @param[in] otherPrivUpdates - other privilege update flag to update ipmi
     * enable, link authentication and access callback
     *
     *  @return ccSuccess for success, others for failure.
     */
    /* @brief 设置用户特权与访问详情
     *   @param[in] userId - 用户 ID
     *   @param[in] chNum - 通道号
     *   @param[in] privAccess - 特权访问
     *   @param[in] otherPrivUpdates - 其他特权更新标志，用于更新 ipmi
     *  enable、链路认证和访问回调
     *   @return 成功返回 ccSuccess，否则返回其他值。 */
    Cc setUserPrivilegeAccess(const uint8_t userId, const uint8_t chNum,
                              const UserPrivAccess& privAccess,
                              const bool& otherPrivUpdates);

    /** @brief to get user payload access details from userInfo entry.
     *
     *  @param[in] userInfo    - userInfo entry in usersTbl.
     *  @param[out] stdPayload - stdPayloadEnables1 in a 2D-array.
     *  @param[out] oemPayload - oemPayloadEnables1 in a 2D-array.
     *
     *  @details Update the given 2D-arrays using the payload access details
     *  available in the given userInfo entry (from usersTbl).
     *  This 2D-array will be mapped to a JSON object (which will be written to
     *  a JSON file subsequently).
     */
    /* @brief 从 userInfo 条目获取用户 payload 访问详情。
     *  @param[in] userInfo    - usersTbl 中的 userInfo 条目。
     *  @param[out] stdPayload - 二维数组形式的 stdPayloadEnables1。
     *  @param[out] oemPayload - 二维数组形式的 oemPayloadEnables1。
     *  @details 使用给定 userInfo 条目中的 payload 访问详情
     *  更新给定的二维数组（userInfo 来自 usersTbl）。
     *  该二维数组将映射为一个 JSON 对象（随后
     *  写入 JSON 文件）。 */
    void readPayloadAccessFromUserInfo(
        const UserInfo& userInfo,
        std::array<std::array<bool, ipmiMaxChannels>, payloadsPerByte>&
            stdPayload,
        std::array<std::array<bool, ipmiMaxChannels>, payloadsPerByte>&
            oemPayload);

    /** @brief to update user payload access details in userInfo entry.
     *
     *  @param[in] stdPayload - stdPayloadEnables1 in a 2D-array.
     *  @param[in] oemPayload - oemPayloadEnables1 in a 2D-array.
     *  @param[out] userInfo  - userInfo entry in usersTbl.
     *
     *  @details Update user payload access details of a given userInfo
     *  entry (in usersTbl) with the information provided in given 2D-arrays.
     *  This 2D-array was created out of a JSON object (which was created by
     *  parsing a JSON file).
     */
    /* @brief 更新 userInfo 条目中的用户 payload 访问详情。
     *  @param[in] stdPayload - 二维数组形式的 stdPayloadEnables1。
     *  @param[in] oemPayload - 二维数组形式的 oemPayloadEnables1。
     *  @param[out] userInfo  - usersTbl 中的 userInfo 条目。
     *  @details 用给定二维数组中的信息更新 usersTbl 中
     *  给定 userInfo 条目的用户 payload 访问详情。
     *  该二维数组由 JSON 对象（通过解析 JSON
     *  文件创建）生成。 */
    void updatePayloadAccessInUserInfo(
        const std::array<std::array<bool, ipmiMaxChannels>, payloadsPerByte>&
            stdPayload,
        const std::array<std::array<bool, ipmiMaxChannels>, payloadsPerByte>&
            oemPayload,
        UserInfo& userInfo);

    /** @brief to set user payload access details
     *
     *  @param[in] chNum - channel number
     *  @param[in] operation - Enable / Disable
     *  @param[in] userId - user id
     *  @param[in] payloadAccess - payload access
     *
     *  @return ccSuccess for success, others for failure.
     */
    /* @brief 设置用户 payload 访问详情
     *   @param[in] chNum - 通道号
     *   @param[in] operation - 启用 / 禁用
     *   @param[in] userId - 用户 ID
     *   @param[in] payloadAccess - payload 访问
     *   @return 成功返回 ccSuccess，否则返回其他值。 */
    Cc setUserPayloadAccess(const uint8_t chNum, const uint8_t operation,
                            const uint8_t userId,
                            const PayloadAccess& payloadAccess);

    /** @brief reads user management related data from configuration file
     *
     */
    /* @brief 从配置文件读取用户管理相关数据 */
    void readUserData();

    /** @brief writes user management related data to configuration file
     *
     */
    /* @brief 将用户管理相关数据写入配置文件 */
    void writeUserData();

    /** @brief initializes user data file with default values
     *
     */
    /* @brief 用默认值初始化用户数据文件 */
    void initializeUserDataFile();

    /** @brief Function which checks and reload configuration file data if
     * needed.
     *
     */
    /* @brief 检查并在需要时重新加载配置文件
     * 数据的函数 */
    void checkAndReloadUserData();

    /** @brief provides user details from D-Bus user property data
     *
     *  @param[in] properties - D-Bus user property
     *  @param[out] usrGrps - user group details
     *  @param[out] usrPriv - user privilege
     *  @param[out] usrEnabled - enabled state of the user.
     *
     *  @return 0 for success, -errno for failure.
     */
    /* @brief 从 D-Bus 用户属性数据提供用户详情
     *   @param[in] properties - D-Bus 用户属性
     *   @param[out] usrGrps - 用户组详情
     *   @param[out] usrPriv - 用户权限
     *   @param[out] usrEnabled - 用户的启用状态。
     *   @return 成功返回 0，失败返回 -errno。 */
    void getUserProperties(const DbusUserObjProperties& properties,
                           std::vector<std::string>& usrGrps,
                           std::string& usrPriv, bool& usrEnabled);

    /** @brief provides user details from D-Bus user object data
     *
     *  @param[in] userObjs - D-Bus user object
     *  @param[out] usrGrps - user group details
     *  @param[out] usrPriv - user privilege
     *  @param[out] usrEnabled - enabled state of the user.
     *
     *  @return 0 for success, -errno for failure.
     */
    /* @brief 从 D-Bus 用户对象数据提供用户详情
     *   @param[in] userObjs - D-Bus 用户对象
     *   @param[out] usrGrps - 用户组详情
     *   @param[out] usrPriv - 用户权限
     *   @param[out] usrEnabled - 用户的启用状态。
     *   @return 成功返回 0，失败返回 -errno。 */
    int getUserObjProperties(const DbusUserObjValue& userObjs,
                             std::vector<std::string>& usrGrps,
                             std::string& usrPriv, bool& usrEnabled);

    /** @brief function to add user entry information to the configuration
     *
     *  @param[in] userName - user name
     *  @param[in] priv - privilege of the user
     *  @param[in] enabled - enabled state of the user
     *
     *  @return true for success, false for failure
     */
    /* @brief 向配置添加用户条目信息的函数
     *   @param[in] userName - 用户名
     *   @param[in] priv - 用户的权限
     *   @param[in] enabled - 用户的启用状态
     *   @return 成功返回 true，失败返回 false */
    bool addUserEntry(const std::string& userName, const std::string& priv,
                      const bool& enabled);

    /** @brief function to delete user entry based on user index
     *
     *  @param[in] usrIdx - user index
     *
     */
    /* @brief 根据用户索引删除用户条目的函数
     *   @param[in] usrIdx - 用户索引 */
    void deleteUserIndex(const size_t& usrIdx);

    /** @brief function to get users table
     *
     */
    /* @brief 获取用户表的函数 */
    UsersTbl* getUsersTblPtr();

    /** @brief function to get all User available groups
     *
     */
    /* @brief 获取所有用户可用组的函数 */
    const std::vector<std::string>& getUsersAllAvailableGroup();

    /** @brief determines valid user group access
     *
     *  @param[in] groupAccess - List of groupAccess
     *
     *  @return true if valid, false otherwise
     */
    /* @brief 判断用户组访问是否有效
     *   @param[in] groupAccess - groupAccess 列表
     *   @return 有效返回 true，否则返回 false */
    bool isValidGroups(const std::vector<std::string>& groupAccess);

    /** @brief sets user group access data
     *
     *  @param[in] userId - user id
     *  @param[in] chNum - channel number
     *  @param[in] groups - groups data
     *
     *  @return ccSuccess for success, others for failure.
     */
    /* @brief 设置用户组访问数据
     *   @param[in] userId - 用户 ID
     *   @param[in] chNum - 通道号
     *   @param[in] groups - 组数据
     *   @return 成功返回 ccSuccess，否则返回其他值。 */
    Cc setUserGroups(const uint8_t userId, const uint8_t chNum,
                     const std::vector<std::string>& groupAccess);

    std::unique_ptr<boost::interprocess::named_recursive_mutex> userMutex{
        nullptr};

  private:
    UsersTbl usersTbl;
    std::vector<std::string> availablePrivileges;
    std::vector<std::string> availableGroups;
    sdbusplus::bus_t bus;
    std::timespec fileLastUpdatedTime;
    bool signalHndlrObject = false;
    boost::interprocess::file_lock sigHndlrLock;
    boost::interprocess::file_lock mutexCleanupLock;

    /** @brief function to get user configuration file timestamp
     *
     *  @return time stamp or -EIO for failure
     */
    /* @brief 获取用户配置文件时间戳的函数
     *   @return 时间戳，失败返回 -EIO */
    std::timespec getUpdatedFileTime();

    /** @brief function to available system privileges and groups
     *
     */
    /* @brief 获取可用系统权限和组的函数 */
    void getSystemPrivAndGroups();

    /** @brief function to init user data from configuration & D-Bus objects
     * and to register for signals
     *
     */
    /* @brief 从配置与 D-Bus 对象初始化用户数据
     * 并注册信号的函数 */
    void cacheUserDataFile();
};

} // namespace ipmi
