
/**
 * @file cipher_mgmt.hpp
 * @brief Cipher Suite 管理的声明。
 */
/*  @file cipher_mgmt.hpp
 *  @brief Cipher Suite 管理的声明。 */
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

#include <ipmid/api-types.hpp>
#include <ipmid/message/types.hpp>
#include <nlohmann/json.hpp>

#include <map>

namespace ipmi
{
static const std::string csPrivDefaultFileName =
    "/usr/share/ipmi-providers/cs_privilege_levels.json";

static const std::string csPrivFileName =
    "/var/lib/ipmi/cs_privilege_levels.json";

static const size_t maxCSRecords = 16;

using ChannelNumCipherIDPair = std::pair<uint8_t, uint8_t>;
using privMap = std::map<ChannelNumCipherIDPair, uint4_t>;

/** @class CipherConfig
 *  @brief Class to provide cipher suite functionalities
 */
/* @class CipherConfig
 *   @brief 提供 cipher suite 功能的类 */
class CipherConfig
{
  public:
    ~CipherConfig() = default;
    explicit CipherConfig(const std::string& csFileName,
                          const std::string& csDefaultFileName);
    CipherConfig() = delete;

    /** @brief function to get cipher suite privileges from config file
     *
     *  @param[in] chNum - channel number for which we want to get cipher suite
     * privilege levels
     *
     *  @param[in] csPrivilegeLevels - gets filled by cipher suite privilege
     * levels
     *
     *  @return 0 for success, non zero value for failure
     */
    /* @brief 从配置文件获取 cipher suite 权限的函数
     *  @param[in] chNum - 要获取 cipher suite 权限级别的
     * 通道号
     *  @param[in] csPrivilegeLevels - 由 cipher suite
     * 权限级别填充
     *  @return 成功返回 0，失败返回非零值 */
    ipmi::Cc getCSPrivilegeLevels(
        uint8_t chNum, std::array<uint4_t, maxCSRecords>& csPrivilegeLevels);

    /** @brief function to set/update cipher suite privileges in config file
     *
     *  @param[in] chNum - channel number for which we want to update cipher
     * suite privilege levels
     *
     *  @param[in] csPrivilegeLevels - cipher suite privilege levels to update
     * in config file
     *
     *  @return 0 for success, non zero value for failure
     */
    /* @brief 在配置文件中设置/更新 cipher suite 权限的函数
     *  @param[in] chNum - 要更新 cipher suite 权限级别的
     * 通道号
     *  @param[in] csPrivilegeLevels - 要在配置文件中更新的
     * cipher suite 权限级别
     *  @return 成功返回 0，失败返回非零值 */
    ipmi::Cc setCSPrivilegeLevels(
        uint8_t chNum,
        const std::array<uint4_t, maxCSRecords>& csPrivilegeLevels);

  private:
    std::string cipherSuitePrivFileName, cipherSuiteDefaultPrivFileName;

    privMap csPrivilegeMap;

    /** @brief function to read json config file
     *
     *  @return nlohmann::json object
     */
    /* @brief 读取 json 配置文件的函数
     *  @return nlohmann::json 对象 */
    nlohmann::json readCSPrivilegeLevels(const std::string& csFileName);

    /** @brief function to write json config file
     *
     *  @param[in] jsonData - json object
     *
     *  @return 0 for success, -errno for failure.
     */
    /* @brief 写入 json 配置文件的函数
     *  @param[in] jsonData - json 对象
     *  @return 成功返回 0，失败返回 -errno。 */
    int writeCSPrivilegeLevels(const nlohmann::json& jsonData);

    /** @brief convert to cipher suite privilege from string to value
     *
     *  @param[in] value - privilege value
     *
     *  @return cipher suite privilege index
     */
    /* @brief 将 cipher suite 权限从字符串转换为值
     *  @param[in] value - 权限值
     *  @return cipher suite 权限索引 */
    uint4_t convertToPrivLimitIndex(const std::string& value);

    /** @brief function to convert privilege value to string
     *
     *  @param[in] value - privilege value
     *
     *  @return privilege in string
     */
    /* @brief 将权限值转换为字符串的函数
     *  @param[in] value - 权限值
     *  @return 字符串形式的权限 */
    std::string convertToPrivLimitString(const uint4_t& value);

    /** @brief function to load CS Privilege Levels from json file/files to map
     *
     */
    /* @brief 从 json 文件加载 CS 权限级别到 map 的函数 */
    void loadCSPrivilegesToMap();

    /** @brief function to update CS privileges map from json object data,
     * jsonData
     *
     */
    /* @brief 从 json 对象数据更新 CS 权限 map 的函数，
     * jsonData */
    void updateCSPrivilegesMap(const nlohmann::json& jsonData);
};

/** @brief function to create static CipherConfig object
 *
 *  @param[in] csFileName - user setting cipher suite privilege file name
 *  @param[in] csDefaultFileName - default cipher suite privilege file name
 *
 *  @return static CipherConfig object
 */
/* @brief 创建静态 CipherConfig 对象的函数
 *  @param[in] csFileName - 用户设置的 cipher suite 权限文件名
 *  @param[in] csDefaultFileName - 默认 cipher suite 权限文件名
 *  @return 静态 CipherConfig 对象 */
CipherConfig& getCipherConfigObject(const std::string& csFileName,
                                    const std::string& csDefaultFileName);
} // namespace ipmi
