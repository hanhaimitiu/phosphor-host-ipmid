#pragma once

/**
 * @file sessionhelper.hpp
 * @brief 会话辅助：System Interface 会话状态维护。
 */
/*  @file sessionhelper.hpp
 *  @brief 会话辅助：System Interface 会话状态维护。 */

#include <cstdint>
#include <sstream>
#include <string>

/**
 * @brief parse session input payload.
 *
 * This function retrieves the session id and session handle from the session
 * object path.
 * A valid object path will be in the form
 * "/xyz/openbmc_project/ipmi/session/channel/sessionId_sessionHandle"
 *
 * Ex: "/xyz/openbmc_project/ipmi/session/eth0/12a4567d_8a"
 * SessionId    : 0X12a4567d
 * SessionHandle: 0X8a

 * @param[in] objectPath - session object path
 * @param[in] sessionId - retrieved session id will be assigned.
 * @param[in] sessionHandle - retrieved session handle will be assigned.
 *
 * @return true if session id and session handle are retrieved else returns
 * false.
 */
/* @brief 解析会话输入的 payload。
 * 本函数从会话对象路径中获取会话 id
 * 和会话句柄。
 * 有效的对象路径形式如下：
 * "/xyz/openbmc_project/ipmi/session/channel/sessionId_sessionHandle"
 * 例如："/xyz/openbmc_project/ipmi/session/eth0/12a4567d_8a"
 * SessionId    : 0X12a4567d
 * SessionHandle: 0X8a
 * @param[in] objectPath - 会话对象路径
 * @param[in] sessionId - 用于接收获取到的会话 id。
 * @param[in] sessionHandle - 用于接收获取到的会话句柄。
 * @return 成功获取会话 id 和会话句柄时返回 true，
 * 否则返回 false。 */
bool parseCloseSessionInputPayload(const std::string& objectPath,
                                   uint32_t& sessionId, uint8_t& sessionHandle)
{
    if (objectPath.empty())
    {
        return false;
    }
    // getting the position of session id and session handle string from
    // object path.
    // 从对象路径中获取会话 id 和会话句柄
    // 字符串的位置。
    std::size_t ptrPosition = objectPath.rfind("/");

    if (ptrPosition != std::string::npos)
    {
        // get the sessionid & session handle string from the session object
        // path Ex: sessionIdString: "12a4567d_8a"
        // 从会话对象路径中获取 sessionid 和会话句柄字符串
        // 例如：sessionIdString: "12a4567d_8a"
        std::string sessionIdString = objectPath.substr(ptrPosition + 1);
        std::size_t pos = sessionIdString.rfind("_");

        if (pos != std::string::npos)
        {
            // extracting the session handle
            // 提取会话句柄
            std::string sessionHandleString = sessionIdString.substr(pos + 1);
            // extracting the session id
            // 提取会话 id
            sessionIdString.resize(pos);
            // converting session id string  and session handle string to
            // hexadecimal.
            // 将会话 id 字符串和会话句柄字符串
            // 转换为十六进制。
            std::stringstream handle(sessionHandleString);
            uint16_t tempSessionHandle = 0;
            handle >> std::hex >> tempSessionHandle;
            sessionHandle = tempSessionHandle & 0xFF;
            std::stringstream idString(sessionIdString);
            idString >> std::hex >> sessionId;
            return true;
        }
    }
    return false;
}

/**
 * @brief is session object matched.
 *
 * This function checks whether the objectPath contains reqSessionId and
 * reqSessionHandle, e.g., "/xyz/openbmc_project/ipmi/session/eth0/12a4567d_8a"
 * matches sessionId 0x12a4567d and sessionHandle 0x8a.
 *
 * @param[in] objectPath - session object path
 * @param[in] reqSessionId - request session id
 * @param[in] reqSessionHandle - request session handle
 *
 * @return true if the object is matched else return false
 **/
/* @brief 判断会话对象是否匹配。
 * 本函数检查 objectPath 是否包含 reqSessionId 和
 * reqSessionHandle，例如 "/xyz/.../12a4567d_8a"
 * 匹配 sessionId 0x12a4567d 和 sessionHandle 0x8a。
 * @param[in] objectPath - 会话对象路径
 * @param[in] reqSessionId - 请求的会话 id
 * @param[in] reqSessionHandle - 请求的会话句柄
 * @return 对象匹配时返回 true，否则返回 false */
bool isSessionObjectMatched(const std::string& objectPath,
                            const uint32_t reqSessionId,
                            const uint8_t reqSessionHandle)
{
    uint32_t sessionId = 0;
    uint8_t sessionHandle = 0;

    if (parseCloseSessionInputPayload(objectPath, sessionId, sessionHandle))
    {
        return (reqSessionId == sessionId) ||
               (reqSessionHandle == sessionHandle);
    }

    return false;
}
