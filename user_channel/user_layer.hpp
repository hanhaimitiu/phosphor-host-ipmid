
/**
 * @file user_layer.hpp
 * @brief 用户层的声明。
 */
/*  @file user_layer.hpp
 *  @brief 用户层的声明。 */
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

#include <ipmid/api.hpp>
#include <ipmid/types.hpp>

#include <bitset>
#include <string>
#include <vector>

namespace ipmi
{

// TODO: Has to be replaced with proper channel number assignment logic
// TODO：需要用合适的通道号分配逻辑替换
/**
 * @enum Channel Id
 */
/*  @enum 通道 ID */
enum class EChannelID : uint8_t
{
    chanLan1 = 0x01
};

static constexpr uint8_t invalidUserId = 0xFF;
static constexpr uint8_t reservedUserId = 0x0;
static constexpr uint8_t ipmiMaxUserName = 16;
static constexpr uint8_t ipmiMaxUsers = 15;
static constexpr uint8_t ipmiMaxChannels = 16;
static constexpr uint8_t maxIpmi20PasswordSize = 20;
static constexpr uint8_t maxIpmi15PasswordSize = 16;
static constexpr uint8_t payloadsPerByte = 8;

/** @struct PrivAccess
 *
 *  User privilege related access data as per IPMI specification.(refer spec
 * sec 22.26)
 */
/* @struct PrivAccess
 *   按 IPMI 规范定义的用户特权相关访问数据。（参见规范
 *   22.26 节） */
struct PrivAccess
{
#if BYTE_ORDER == LITTLE_ENDIAN
    uint8_t privilege:4;
    uint8_t ipmiEnabled:1;
    uint8_t linkAuthEnabled:1;
    uint8_t accessCallback:1;
    uint8_t reserved:1;
#endif
#if BYTE_ORDER == BIG_ENDIAN
    uint8_t reserved:1;
    uint8_t accessCallback:1;
    uint8_t linkAuthEnabled:1;
    uint8_t ipmiEnabled:1;
    uint8_t privilege:4;
#endif
} __attribute__((packed));

/** @struct UserPayloadAccess
 *
 *  Structure to denote payload access restrictions applicable for a
 *  given user and channel. (refer spec sec 24.6)
 */
/* @struct UserPayloadAccess
 *   表示适用于给定用户和通道的 payload 访问限制的结构体。
 * （参见规范 24.6 节） */
struct PayloadAccess
{
    std::bitset<payloadsPerByte> stdPayloadEnables1;
    std::bitset<payloadsPerByte> stdPayloadEnables2Reserved;
    std::bitset<payloadsPerByte> oemPayloadEnables1;
    std::bitset<payloadsPerByte> oemPayloadEnables2Reserved;
};

/** @brief initializes user management
 *
 *  @return ccSuccess for success, others for failure.
 */
/* @brief 初始化用户管理
 *   @return 成功返回 ccSuccess，否则返回其他值。 */
Cc ipmiUserInit();

/** @brief The ipmi get user password layer call
 *
 *  @param[in] userName - user name
 *
 *  @return password or empty string
 */
/* @brief ipmi 获取用户密码的层调用
 *   @param[in] userName - 用户名
 *   @return 密码或空字符串 */
SecureString ipmiUserGetPassword(const std::string& userName);

/** @brief The IPMI call to clear password entry associated with specified
 * username
 *
 *  @param[in] userName - user name to be removed
 *
 *  @return 0 on success, non-zero otherwise.
 */
/* @brief 清除与指定用户关联的密码条目的
 * IPMI 调用
 *  @param[in] userName - 要删除的用户名
 *  @return 成功返回 0，否则返回非零值。 */
Cc ipmiClearUserEntryPassword(const std::string& userName);

/** @brief The IPMI call to reuse password entry for the renamed user
 *  to another one
 *
 *  @param[in] userName - user name which has to be renamed
 *  @param[in] newUserName - new user name
 *
 *  @return 0 on success, non-zero otherwise.
 */
/* @brief 为重命名用户复用密码条目
 * 到另一用户的 IPMI 调用
 *  @param[in] userName - 要重命名的用户名
 *  @param[in] newUserName - 新用户名
 *  @return 成功返回 0，否则返回非零值。 */
Cc ipmiRenameUserEntryPassword(const std::string& userName,
                               const std::string& newUserName);

/** @brief determines valid userId
 *
 *  @param[in] userId - user id
 *
 *  @return true if valid, false otherwise
 */
/* @brief 判断用户 ID 是否有效
 *   @param[in] userId - 用户 ID
 *   @return 有效返回 true，否则返回 false */
bool ipmiUserIsValidUserId(const uint8_t userId);

/** @brief determines valid privilege level
 *
 *  @param[in] priv - privilege level
 *
 *  @return true if valid, false otherwise
 */
/* @brief 判断权限级别是否有效
 *   @param[in] priv - 权限级别
 *   @return 有效返回 true，否则返回 false */
bool ipmiUserIsValidPrivilege(const uint8_t priv);

/** @brief get user id corresponding to the user name
 *
 *  @param[in] userName - user name
 *
 *  @return userid. Will return 0xff if no user id found
 */
/* @brief 获取用户名对应的用户 ID
 *   @param[in] userName - 用户名
 *   @return 用户 ID。未找到用户 ID 时返回 0xff */
uint8_t ipmiUserGetUserId(const std::string& userName);

/** @brief set's user name
 *  This API is deprecated
 */
/* @brief 设置用户名
 *   该 API 已废弃 */
Cc ipmiUserSetUserName(const uint8_t userId, const char* userName)
    __attribute__((deprecated));

/** @brief set's user name
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
Cc ipmiUserSetUserName(const uint8_t userId, const std::string& userName);

/** @brief set user password
 *
 *  @param[in] userId - user id
 *  @param[in] userPassword - New Password
 *
 *  @return ccSuccess for success, others for failure.
 */
/* @brief 设置用户密码
 *   @param[in] userId - 用户 ID
 *   @param[in] userPassword - 新密码
 *   @return 成功返回 ccSuccess，否则返回其他值。 */
Cc ipmiUserSetUserPassword(const uint8_t userId, const char* userPassword);

/** @brief set special user password (non-ipmi accounts)
 *
 *  @param[in] userName - user name
 *  @param[in] userPassword - New Password
 *
 *  @return ccSuccess for success, others for failure.
 */
/* @brief 设置特殊用户密码（非 ipmi 账户）
 *   @param[in] userName - 用户名
 *   @param[in] userPassword - 新密码
 *   @return 成功返回 ccSuccess，否则返回其他值。 */
Cc ipmiSetSpecialUserPassword(const std::string& userName,
                              const SecureString& userPassword);

/** @brief get user name
 *
 *  @param[in] userId - user id
 *  @param[out] userName - user name
 *
 *  @return ccSuccess for success, others for failure.
 */
/* @brief 获取用户名
 *   @param[in] userId - 用户 ID
 *   @param[out] userName - 用户名
 *   @return 成功返回 ccSuccess，否则返回其他值。 */
Cc ipmiUserGetUserName(const uint8_t userId, std::string& userName);

/** @brief provides available fixed, max, and enabled user counts
 *
 *  @param[out] maxChUsers - max channel users
 *  @param[out] enabledUsers - enabled user count
 *  @param[out] fixedUsers - fixed user count
 *
 *  @return ccSuccess for success, others for failure.
 */
/* @brief 提供可用的固定用户数、最大用户数和已启用用户数
 *   @param[out] maxChUsers - 通道最大用户数
 *   @param[out] enabledUsers - 已启用用户数
 *   @param[out] fixedUsers - 固定用户数
 *   @return 成功返回 ccSuccess，否则返回其他值。 */
Cc ipmiUserGetAllCounts(uint8_t& maxChUsers, uint8_t& enabledUsers,
                        uint8_t& fixedUsers);

/** @brief function to update user enabled state
 *
 *  @param[in] userId - user id
 *..@param[in] state - state of the user to be updated, true - user enabled.
 *
 *  @return ccSuccess for success, others for failure.
 */
/* @brief 更新用户启用状态的函数
 *   @param[in] userId - 用户 ID
 *  ..@param[in] state - 要更新的用户状态，true 表示用户已启用。
 *   @return 成功返回 ccSuccess，否则返回其他值。 */
Cc ipmiUserUpdateEnabledState(const uint8_t userId, const bool& state);

/** @brief determines whether user is enabled
 *
 *  @param[in] userId - user id
 *..@param[out] state - state of the user
 *
 *  @return ccSuccess for success, others for failure.
 */
/* @brief 判断用户是否已启用
 *   @param[in] userId - 用户 ID
 *  ..@param[out] state - 用户状态
 *   @return 成功返回 ccSuccess，否则返回其他值。 */
Cc ipmiUserCheckEnabled(const uint8_t userId, bool& state);

/** @brief determines whether user is locked for failed login attempts
 *
 *  @param[in] userName - user name
 *  @param[out] locked  - true if locked, false otherwise
 *
 *  @return ccSuccess for success, others for failure.
 */
/* @brief 判断用户是否因登录失败被锁定
 *   @param[in] userName - 用户名
 *   @param[out] locked  - 已锁定为 true，否则为 false
 *   @return 成功返回 ccSuccess，否则返回其他值。 */
Cc ipmiUserCheckLockedForFailedAttempt(const std::string& userName,
                                       bool& locked);

/** @brief determines whether user's password has expired
 *
 *  @param[in] userName - user name
 *  @param[out] expired - true if password has expired, false otherwise
 *
 *  @return ccSuccess for success, others for failure.
 */
/* @brief 判断用户密码是否已过期
 *   @param[in] userName - 用户名
 *   @param[out] expired - 密码已过期为 true，否则为 false
 *   @return 成功返回 ccSuccess，否则返回其他值。 */
Cc ipmiUserCheckPasswordExpired(const std::string& userName, bool& expired);

/** @brief provides user privilege access data
 *
 *  @param[in] userId - user id
 *  @param[in] chNum - channel number
 *  @param[out] privAccess - privilege access data
 *
 *  @return ccSuccess for success, others for failure.
 */
/* @brief 提供用户特权访问数据
 *   @param[in] userId - 用户 ID
 *   @param[in] chNum - 通道号
 *   @param[out] privAccess - 特权访问数据
 *   @return 成功返回 ccSuccess，否则返回其他值。 */
Cc ipmiUserGetPrivilegeAccess(const uint8_t userId, const uint8_t chNum,
                              PrivAccess& privAccess);

/** @brief sets user privilege access data
 *
 *  @param[in] userId - user id
 *  @param[in] chNum - channel number
 *  @param[in] privAccess - privilege access data
 *  @param[in] otherPrivUpdate - flags to indicate other fields update
 *
 *  @return ccSuccess for success, others for failure.
 */
/* @brief 设置用户特权访问数据
 *   @param[in] userId - 用户 ID
 *   @param[in] chNum - 通道号
 *   @param[in] privAccess - 特权访问数据
 *   @param[in] otherPrivUpdate - 指示其他字段更新的标志
 *   @return 成功返回 ccSuccess，否则返回其他值。 */
Cc ipmiUserSetPrivilegeAccess(const uint8_t userId, const uint8_t chNum,
                              const PrivAccess& privAccess,
                              const bool& otherPrivUpdate);

/** @brief sets user payload access data
 *
 *  @param[in] chNum - channel number
 *  @param[in] operation - ENABLE / DISABLE operation
 *  @param[in] userId - user id
 *  @param[in] payloadAccess - payload access data
 *
 *  @return ccSuccess for success, others for failure.
 */
/* @brief 设置用户 payload 访问数据
 *   @param[in] chNum - 通道号
 *   @param[in] operation - 启用 / 禁用操作
 *   @param[in] userId - 用户 ID
 *   @param[in] payloadAccess - payload 访问数据
 *   @return 成功返回 ccSuccess，否则返回其他值。 */
Cc ipmiUserSetUserPayloadAccess(const uint8_t chNum, const uint8_t operation,
                                const uint8_t userId,
                                const PayloadAccess& payloadAccess);

/** @brief provides user payload access data
 *
 *  @param[in] chNum - channel number
 *  @param[in] userId - user id
 *  @param[out] payloadAccess - payload access data
 *
 *  @return ccSuccess for success, others for failure.
 */
/* @brief 提供用户 payload 访问数据
 *   @param[in] chNum - 通道号
 *   @param[in] userId - 用户 ID
 *   @param[out] payloadAccess - payload 访问数据
 *   @return 成功返回 ccSuccess，否则返回其他值。 */
Cc ipmiUserGetUserPayloadAccess(const uint8_t chNum, const uint8_t userId,
                                PayloadAccess& payloadAccess);

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
Cc ipmiUserSetUserGroups(const uint8_t userId, const uint8_t chNum,
                         const std::vector<std::string>& groupAccess);

} // namespace ipmi
