
/**
 * @file passwd_mgr.hpp
 * @brief 密码管理的声明。
 */
/*  @file passwd_mgr.hpp
 *  @brief 密码管理的声明。 */
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
#include <openssl/evp.h>

#include <ipmid/types.hpp>

#include <ctime>
#include <string>
#include <unordered_map>
#include <vector>

namespace ipmi
{

class PasswdMgr
{
  public:
    ~PasswdMgr() = default;
    PasswdMgr(const PasswdMgr&) = delete;
    PasswdMgr& operator=(const PasswdMgr&) = delete;
    PasswdMgr(PasswdMgr&&) = delete;
    PasswdMgr& operator=(PasswdMgr&&) = delete;

    /** @brief Constructs user password list
     *
     */
    /* @brief 构造用户密码列表 */
    PasswdMgr();

    /** @brief Get password for the user
     *
     *  @param[in] userName - user name
     *
     * @return password string. will return empty string, if unable to locate
     * the user
     */
    /* @brief 获取用户的密码
     *  @param[in] userName - 用户名
     * @return 密码字符串。如果找不到该用户，
     * 将返回空字符串 */
    SecureString getPasswdByUserName(const std::string& userName);

    /** @brief Update / clear username and password entry for the specified
     * user
     *
     *  @param[in] userName - user name that has to be renamed / deleted
     *  @param[in] newUserName - new user name. If empty, userName will be
     *   deleted.
     *
     * @return error response
     */
    /* @brief 更新 / 清除指定用户的用户名和密码
     * 条目
     *  @param[in] userName - 要重命名 / 删除的用户名
     *  @param[in] newUserName - 新用户名。如果为空，将删除
     *  userName。
     * @return 错误响应 */
    int updateUserEntry(const std::string& userName,
                        const std::string& newUserName);

  private:
    using UserName = std::string;
    using Password = SecureString;
    std::unordered_map<UserName, Password> passwdMapList;
    std::time_t fileLastUpdatedTime;

    /** @brief restrict file permission
     *
     */
    /* @brief 限制文件权限 */
    void restrictFilesPermission(void);
    /** @brief check timestamp and reload password map if required
     *
     */
    /* @brief 检查时间戳，必要时重新加载密码 map */
    void checkAndReload(void);
    /** @brief initializes passwdMapList by reading the encrypted file
     *
     * Initializes the passwordMapList members after decrypting the
     * password file. passwordMapList will be used further in IPMI
     * authentication.
     */
    /* @brief 通过读取加密文件初始化 passwdMapList
     * 解密密码文件后初始化 passwordMapList 成员。
     * passwordMapList 将用于后续的 IPMI
     * 认证。 */
    void initPasswordMap(void);

    /** @brief Function to read the encrypted password file data
     *
     *  @param[out] outBytes - vector to hold decrypted password file data
     *
     * @return error response
     */
    /* @brief 读取加密密码文件数据的函数
     *   @param[out] outBytes - 用于保存解密后密码文件数据的 vector
     *  @return 错误响应 */
    int readPasswdFileData(SecureString& outBytes);
    /** @brief  Updates special password file by clearing the password entry
     *  for the user specified.
     *
     *  @param[in] userName - user name that has to be renamed / deleted
     *  @param[in] newUserName - new user name. If empty, userName will be
     *   deleted.
     *
     * @return error response
     */
    /* @brief 通过清除指定用户的密码条目来更新
     * 特殊密码文件
     *  @param[in] userName - 要重命名 / 删除的用户名
     *  @param[in] newUserName - 新用户名。如果为空，将删除
     *  userName。
     * @return 错误响应 */
    int updatePasswdSpecialFile(const std::string& userName,
                                const std::string& newUserName);
    /** @brief encrypts or decrypt the data provided
     *
     *  @param[in] doEncrypt - do encrypt if set to true, else do decrypt.
     *  @param[in] cipher - cipher to be used
     *  @param[in] key - pointer to the key
     *  @param[in] keyLen - Length of the key to be used
     *  @param[in] iv - pointer to initialization vector
     *  @param[in] ivLen - Length of the iv
     *  @param[in] inBytes - input data to be encrypted / decrypted
     *  @param[in] inBytesLen - input size to be encrypted / decrypted
     *  @param[in] mac - message authentication code - to figure out corruption
     *  @param[in] macLen - size of MAC
     *  @param[in] outBytes - ptr to store output bytes
     *  @param[in] outBytesLen - output data length.
     *
     * @return error response
     */
    /* @brief 对提供的数据进行加密或解密
     *   @param[in] doEncrypt - 为 true 时加密，否则解密。
     *   @param[in] cipher - 要使用的 cipher
     *   @param[in] key - 指向 key 的指针
     *   @param[in] keyLen - 要使用的 key 长度
     *   @param[in] iv - 指向初始化向量的指针
     *   @param[in] ivLen - iv 的长度
     *   @param[in] inBytes - 要加密 / 解密的输入数据
     *   @param[in] inBytesLen - 要加密 / 解密的输入大小
     *   @param[in] mac - 消息认证码，用于发现数据损坏
     *   @param[in] macLen - MAC 的大小
     *   @param[in] outBytes - 保存输出字节的指针
     *   @param[in] outBytesLen - 输出数据长度。
     *  @return 错误响应 */
    int encryptDecryptData(
        bool doEncrypt, const EVP_CIPHER* cipher, uint8_t* key, size_t keyLen,
        uint8_t* iv, size_t ivLen, uint8_t* inBytes, size_t inBytesLen,
        uint8_t* mac, size_t* macLen, uint8_t* outBytes, size_t* outBytesLen);

    /** @brief  returns updated file time of passwd file entry.
     *
     * @return timestamp or -1 for error.
     */
    /* @brief 返回密码文件条目的最近更新时间。
     *  @return 时间戳，出错返回 -1。 */
    std::time_t getUpdatedFileTime();
};

} // namespace ipmi
