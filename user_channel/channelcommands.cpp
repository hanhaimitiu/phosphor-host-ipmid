
/**
 * @file channelcommands.cpp
 * @brief Channel IPMI 命令（Get Channel Info/Access 等）。
 */
/*  @file channelcommands.cpp
 *  @brief Channel IPMI 命令（Get Channel Info/Access 等）。 */
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

#include "channel_layer.hpp"

#include <ipmid/api.hpp>
#include <phosphor-logging/lg2.hpp>

#include <regex>

namespace ipmi
{

constexpr Cc ccPayloadTypeNotSupported = 0x80;

static inline auto responsePayloadTypeNotSupported()
{
    return response(ccPayloadTypeNotSupported);
}

/** @brief implements the set channel access command
 *  @ param ctx - context pointer
 *  @ param channel - channel number
 *  @ param reserved - skip 4 bits
 *  @ param accessMode - access mode for IPMI messaging
 *  @ param usrAuth - user level authentication (enable/disable)
 *  @ param msgAuth - per message authentication (enable/disable)
 *  @ param alertDisabled - PEF alerting (enable/disable)
 *  @ param chanAccess - channel access
 *  @ param channelPrivLimit - channel privilege limit
 *  @ param reserved - skip 3 bits
 *  @ param channelPrivMode - channel privilege mode
 *
 *  @ returns IPMI completion code
 **/
/* @brief 实现 set channel access 命令
 *  @ param ctx - 上下文指针
 *  @ param channel - 通道号
 *  @ param reserved - 跳过 4 个比特
 *  @ param accessMode - IPMI 消息的访问模式
 *  @ param usrAuth - 用户级认证（启用/禁用）
 *  @ param msgAuth - 逐消息认证（启用/禁用）
 *  @ param alertDisabled - PEF 告警（启用/禁用）
 *  @ param chanAccess - 通道访问
 *  @ param channelPrivLimit - 通道权限上限
 *  @ param reserved - 跳过 3 个比特
 *  @ param channelPrivMode - 通道权限模式
 *  @ returns IPMI 完成码 */
RspType<> ipmiSetChannelAccess(
    Context::ptr ctx, uint4_t channel, uint4_t reserved1, uint3_t accessMode,
    bool usrAuth, bool msgAuth, bool alertDisabled, uint2_t chanAccess,
    uint4_t channelPrivLimit, uint2_t reserved2, uint2_t channelPrivMode)
{
    if (reserved1 || reserved2 ||
        !isValidPrivLimit(static_cast<uint8_t>(channelPrivLimit)))
    {
        lg2::debug("Set channel access - Invalid field in request");
        return responseInvalidFieldRequest();
    }

    const uint8_t chNum =
        convertCurrentChannelNum(static_cast<uint8_t>(channel), ctx->channel);
    if ((getChannelSessionSupport(chNum) == EChannelSessSupported::none) ||
        (!isValidChannel(chNum)))
    {
        lg2::debug("Set channel access - No support on channel: {CHANNEL}",
                   "CHANNEL", chNum);
        return responseActionNotSupportedForChannel();
    }

    ChannelAccess chActData;
    ChannelAccess chNVData;
    uint8_t setActFlag = 0;
    uint8_t setNVFlag = 0;
    Cc compCode;

    // cannot static cast directly from uint2_t to enum; must go via int
    // 不能直接从 uint2_t 静态转换为 enum；必须先转成 int
    uint8_t channelAccessAction = static_cast<uint8_t>(chanAccess);
    switch (static_cast<EChannelActionType>(channelAccessAction))
    {
        case doNotSet:
            break;
        case nvData:
            getChannelAccessPersistData(chNum, chNVData);
            if (chNVData.accessMode != accessMode)
            {
                lg2::debug("Set channel access - Invalid access set mode");
                return responseAccessModeNotSupportedForChannel();
            }
            chNVData.accessMode = static_cast<uint8_t>(accessMode);
            chNVData.userAuthDisabled = usrAuth;
            chNVData.perMsgAuthDisabled = msgAuth;
            chNVData.alertingDisabled = alertDisabled;
            setNVFlag |= (setAccessMode | setUserAuthEnabled |
                          setMsgAuthEnabled | setAlertingEnabled);
            break;

        case activeData:
            getChannelAccessData(chNum, chActData);
            if (chActData.accessMode != accessMode)
            {
                lg2::debug("Set channel access - Invalid access set mode");
                return responseAccessModeNotSupportedForChannel();
            }
            chActData.accessMode = static_cast<uint8_t>(accessMode);
            chActData.userAuthDisabled = usrAuth;
            chActData.perMsgAuthDisabled = msgAuth;
            chActData.alertingDisabled = alertDisabled;
            setActFlag |= (setAccessMode | setUserAuthEnabled |
                           setMsgAuthEnabled | setAlertingEnabled);
            break;

        case reserved:
        default:
            lg2::debug("Set channel access - Invalid access set mode");
            return responseAccessModeNotSupportedForChannel();
    }

    // cannot static cast directly from uint2_t to enum; must go via int
    // 不能直接从 uint2_t 静态转换为 enum；必须先转成 int
    uint8_t channelPrivAction = static_cast<uint8_t>(channelPrivMode);
    switch (static_cast<EChannelActionType>(channelPrivAction))
    {
        case doNotSet:
            break;
        case nvData:
            chNVData.privLimit = static_cast<uint8_t>(channelPrivLimit);
            setNVFlag |= setPrivLimit;
            break;
        case activeData:
            chActData.privLimit = static_cast<uint8_t>(channelPrivLimit);

            setActFlag |= setPrivLimit;
            break;
        case reserved:
        default:
            lg2::debug("Set channel access - Invalid access priv mode");
            return responseAccessModeNotSupportedForChannel();
    }

    if (setNVFlag != 0)
    {
        compCode = setChannelAccessPersistData(chNum, chNVData, setNVFlag);
        if (compCode != ccSuccess)
        {
            lg2::debug("Set channel access - Failed to set access data");
            return response(compCode);
        }
    }

    if (setActFlag != 0)
    {
        compCode = setChannelAccessData(chNum, chActData, setActFlag);
        if (compCode != ccSuccess)
        {
            lg2::debug("Set channel access - Failed to set access data");
            return response(compCode);
        }
    }

    return responseSuccess();
}

/** @brief implements the get channel access command
 *  @ param ctx - context pointer
 *  @ param channel - channel number
 *  @ param reserved1 - skip 4 bits
 *  @ param reserved2 - skip 6 bits
 *  @ param accessMode - get access mode
 *
 *  @returns ipmi completion code plus response data
 *  - accessMode - get access mode
 *  - usrAuthDisabled - user level authentication status
 *  - msgAuthDisabled - message level authentication status
 *  - alertDisabled - alerting status
 *  - reserved - skip 2 bits
 *  - privLimit - channel privilege limit
 *  - reserved - skip 4 bits
 * */
/* @brief 实现 get channel access 命令
 *  @ param ctx - 上下文指针
 *  @ param channel - 通道号
 *  @ param reserved1 - 跳过 4 个比特
 *  @ param reserved2 - 跳过 6 个比特
 *  @ param accessMode - 获取访问模式
 *  @returns IPMI 完成码以及响应数据
 *  - accessMode - 获取访问模式
 *  - usrAuthDisabled - 用户级认证状态
 *  - msgAuthDisabled - 消息级认证状态
 *  - alertDisabled - 告警状态
 *  - reserved - 跳过 2 个比特
 *  - privLimit - 通道权限上限
 *  - reserved - 跳过 4 个比特 */
ipmi::RspType<uint3_t, // access mode,
              bool,    // user authentication status,
              bool,    // message authentication status,
              bool,    // alerting status,
              uint2_t, // reserved,

              uint4_t, // channel privilege,
              uint4_t  // reserved
              >
    ipmiGetChannelAccess(Context::ptr ctx, uint4_t channel, uint4_t reserved1,
                         uint6_t reserved2, uint2_t accessSetMode)
{
    if (reserved1 || reserved2)
    {
        lg2::debug("Get channel access - Invalid field in request");
        return responseInvalidFieldRequest();
    }

    if ((types::enum_cast<EChannelActionType>(accessSetMode) == doNotSet) ||
        (types::enum_cast<EChannelActionType>(accessSetMode) == reserved))
    {
        lg2::debug("Get channel access - Invalid Access mode");
        return responseInvalidFieldRequest();
    }

    const uint8_t chNum =
        convertCurrentChannelNum(static_cast<uint8_t>(channel), ctx->channel);

    if ((getChannelSessionSupport(chNum) == EChannelSessSupported::none) ||
        (!isValidChannel(chNum)))
    {
        lg2::debug("Get channel access - No support on channel: {CHANNEL}",
                   "CHANNEL", chNum);
        return responseActionNotSupportedForChannel();
    }

    ChannelAccess chAccess = {};

    Cc compCode = ipmi::ccUnspecifiedError;

    if (types::enum_cast<EChannelActionType>(accessSetMode) == nvData)
    {
        compCode = getChannelAccessPersistData(chNum, chAccess);
    }
    else if (types::enum_cast<EChannelActionType>(accessSetMode) == activeData)
    {
        compCode = getChannelAccessData(chNum, chAccess);
    }

    if (compCode != ccSuccess)
    {
        return response(compCode);
    }

    constexpr uint2_t reservedOut1 = 0;
    constexpr uint4_t reservedOut2 = 0;

    return responseSuccess(
        types::enum_cast<uint3_t>(chAccess.accessMode),
        chAccess.userAuthDisabled, chAccess.perMsgAuthDisabled,
        chAccess.alertingDisabled, reservedOut1,
        types::enum_cast<uint4_t>(chAccess.privLimit), reservedOut2);
}

/** @brief implements the get channel info command
 *  @ param ctx - context pointer
 *  @ param channel - channel number
 *  @ param reserved - skip 4 bits
 *
 *  @returns ipmi completion code plus response data
 *  - chNum - the channel number for this request
 *  - mediumType - see Table 6-3, Channel Medium Type Numbers
 *  - protocolType - Table 6-2, Channel Protocol Type Numbers
 *  - activeSessionCount - number of active sessions
 *  - sessionType - channel support for sessions
 *  - vendorId - vendor for this channel protocol (IPMI - 7154)
 *  - auxChInfo - auxiliary info for channel
 * */
/* @brief 实现 get channel info 命令
 *  @ param ctx - 上下文指针
 *  @ param channel - 通道号
 *  @ param reserved - 跳过 4 个比特
 *  @returns IPMI 完成码以及响应数据
 *  - chNum - 本次请求的通道号
 *  - mediumType - 参见表 6-3 Channel Medium Type Numbers
 *  - protocolType - 表 6-2 Channel Protocol Type Numbers
 *  - activeSessionCount - 活动会话数
 *  - sessionType - 通道对会话的支持情况
 *  - vendorId - 该通道协议的厂商（IPMI - 7154）
 *  - auxChInfo - 通道的辅助信息 */
RspType<uint4_t,  // chNum
        uint4_t,  // reserved
        uint7_t,  // mediumType
        bool,     // reserved
        uint5_t,  // protocolType
        uint3_t,  // reserved
        uint6_t,  // activeSessionCount
        uint2_t,  // sessionType
        uint24_t, // Vendor IANA
        uint16_t  // aux info
        >
    ipmiGetChannelInfo(Context::ptr ctx, uint4_t channel, uint4_t reserved)
{
    if (reserved)
    {
        lg2::debug("Get channel info - Invalid field in request");
        return responseInvalidFieldRequest();
    }

    uint8_t chNum =
        convertCurrentChannelNum(static_cast<uint8_t>(channel), ctx->channel);
    if (!isValidChannel(chNum))
    {
        lg2::debug("Get channel Info - No support on channel: {CHANNEL}",
                   "CHANNEL", chNum);
        return responseInvalidFieldRequest();
    }

    ChannelInfo chInfo;
    Cc compCode = getChannelInfo(chNum, chInfo);
    if (compCode != ccSuccess)
    {
        lg2::error("Failed to get channel info, channel: {CHANNEL}, "
                   "errno: {ERRNO}",
                   "CHANNEL", chNum, "ERRNO", compCode);
        return response(compCode);
    }

    constexpr uint4_t reserved1 = 0;
    constexpr bool reserved2 = false;
    constexpr uint3_t reserved3 = 0;
    uint8_t mediumType = chInfo.mediumType;
    uint8_t protocolType = chInfo.protocolType;
    uint2_t sessionType = chInfo.sessionSupported;
    uint6_t activeSessionCount = getChannelActiveSessions(chNum);
    // IPMI Spec: The IPMI Enterprise Number is: 7154 (decimal)
    // IPMI 规范：IPMI 企业号为 7154（十进制）
    constexpr uint24_t vendorId = 7154;
    constexpr uint16_t auxChInfo = 0;

    return responseSuccess(chNum, reserved1, mediumType, reserved2,
                           protocolType, reserved3, activeSessionCount,
                           sessionType, vendorId, auxChInfo);
}

namespace
{
constexpr uint16_t standardPayloadBit(PayloadType p)
{
    return (1 << static_cast<size_t>(p));
}

constexpr uint16_t sessionPayloadBit(PayloadType p)
{
    constexpr size_t sessionShift =
        static_cast<size_t>(PayloadType::OPEN_SESSION_REQUEST);
    return ((1 << static_cast<size_t>(p)) >> sessionShift);
}
} // namespace

/** @brief implements get channel payload support command
 *  @ param ctx - ipmi context pointer
 *  @ param chNum - channel number
 *  @ param reserved - skip 4 bits
 *
 *  @ returns IPMI completion code plus response data
 *  - stdPayloadType - bitmask of supported standard payload types
 *  - sessSetupPayloadType - bitmask of supported session setup payload types
 *  - OEMPayloadType - bitmask of supported OEM payload types
 *  - reserved - 2 bytes of 0
 **/
/* @brief 实现 get channel payload support 命令
 *  @ param ctx - ipmi 上下文指针
 *  @ param chNum - 通道号
 *  @ param reserved - 跳过 4 个比特
 *  @ returns IPMI 完成码以及响应数据
 *  - stdPayloadType - 支持的标准 payload 类型位掩码
 *  - sessSetupPayloadType - 支持的会话建立 payload 类型位掩码
 *  - OEMPayloadType - 支持的 OEM payload 类型位掩码
 *  - reserved - 2 字节的 0 */
RspType<uint16_t, // stdPayloadType
        uint16_t, // sessSetupPayloadType
        uint16_t, // OEMPayloadType
        uint16_t  // reserved
        >
    ipmiGetChannelPayloadSupport(Context::ptr ctx, uint4_t channel,
                                 uint4_t reserved)
{
    uint8_t chNum =
        convertCurrentChannelNum(static_cast<uint8_t>(channel), ctx->channel);

    if (!doesDeviceExist(chNum) || !isValidChannel(chNum) || reserved)
    {
        lg2::debug("Get channel payload - Invalid field in request");
        return responseInvalidFieldRequest();
    }

    // Session support is available in active LAN channels.
    // 会话支持仅在处于活动状态的 LAN 通道上可用。
    if (getChannelSessionSupport(chNum) == EChannelSessSupported::none)
    {
        lg2::debug("Get channel payload - No support on channel");
        return responseActionNotSupportedForChannel();
    }
    constexpr uint16_t stdPayloadType = standardPayloadBit(PayloadType::IPMI) |
                                        standardPayloadBit(PayloadType::SOL);
    constexpr uint16_t sessSetupPayloadType =
        sessionPayloadBit(PayloadType::OPEN_SESSION_REQUEST) |
        sessionPayloadBit(PayloadType::OPEN_SESSION_RESPONSE) |
        sessionPayloadBit(PayloadType::RAKP1) |
        sessionPayloadBit(PayloadType::RAKP2) |
        sessionPayloadBit(PayloadType::RAKP3) |
        sessionPayloadBit(PayloadType::RAKP4);
    constexpr uint16_t OEMPayloadType = 0;
    constexpr uint16_t rspRsvd = 0;
    return responseSuccess(stdPayloadType, sessSetupPayloadType, OEMPayloadType,
                           rspRsvd);
}

/** @brief implements the get channel payload version command
 *  @param ctx - IPMI context pointer (for channel)
 *  @param chNum - channel number to get info about
 *  @param reserved - skip 4 bits
 *  @param payloadTypeNum - to get payload type info

 *  @returns IPMI completion code plus response data
 *   - formatVersion - BCD encoded format version info
 */
/* @brief 实现 get channel payload version 命令
 *  @param ctx - IPMI 上下文指针（针对通道）
 *  @param chNum - 要获取信息的通道号
 *  @param reserved - 跳过 4 个比特
 *  @param payloadTypeNum - 要获取的 payload 类型信息
 *  @returns IPMI 完成码以及响应数据
 *   - formatVersion - BCD 编码的格式版本信息 */

RspType<uint8_t> // formatVersion
    ipmiGetChannelPayloadVersion(Context::ptr ctx, uint4_t chNum,
                                 uint4_t reserved, uint8_t payloadTypeNum)
{
    uint8_t channel =
        convertCurrentChannelNum(static_cast<uint8_t>(chNum), ctx->channel);

    if (reserved || !isValidChannel(channel))
    {
        lg2::debug("Get channel payload version - Invalid field in request");
        return responseInvalidFieldRequest();
    }

    if (getChannelSessionSupport(channel) == EChannelSessSupported::none)
    {
        lg2::debug("Get channel payload version - No support on channel");
        return responsePayloadTypeNotSupported();
    }

    if (!isValidPayloadType(static_cast<PayloadType>(payloadTypeNum)))
    {
        lg2::error("Get channel payload version - Payload type unavailable");

        return responsePayloadTypeNotSupported();
    }

    // BCD encoded version representation - 1.0
    // BCD 编码的版本表示 - 1.0
    constexpr uint8_t formatVersion = 0x10;

    return responseSuccess(formatVersion);
}

void registerChannelFunctions() __attribute__((constructor));
void registerChannelFunctions()
{
    ipmiChannelInit();

    registerHandler(prioOpenBmcBase, netFnApp, app::cmdSetChannelAccess,
                    Privilege::Admin, ipmiSetChannelAccess);

    registerHandler(prioOpenBmcBase, netFnApp, app::cmdGetChannelAccess,
                    Privilege::User, ipmiGetChannelAccess);

    registerHandler(prioOpenBmcBase, netFnApp, app::cmdGetChannelInfoCommand,
                    Privilege::User, ipmiGetChannelInfo);

    registerHandler(prioOpenBmcBase, netFnApp, app::cmdGetChannelPayloadSupport,
                    Privilege::User, ipmiGetChannelPayloadSupport);

    registerHandler(prioOpenBmcBase, netFnApp, app::cmdGetChannelPayloadVersion,
                    Privilege::User, ipmiGetChannelPayloadVersion);
}

} // namespace ipmi
