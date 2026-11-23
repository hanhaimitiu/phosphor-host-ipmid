
/**
 * @file api-types.hpp
 * @brief IPMI 回调相关的枚举与类型定义。
 */
/*  @file api-types.hpp
 *  @brief IPMI 回调相关的枚举与类型定义。 */
/*
 * Copyright © 2018 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
/*  版权所有 (c) 2018 Intel Corporation
 *  根据 Apache 许可证 2.0 版（"License"）授权；
 *  除非符合许可证的规定，否则不得使用本文件。
 *  你可以从以下地址获取许可证副本：
 *       http://www.apache.org/licenses/LICENSE-2.0
 *  除非适用法律要求或书面同意，否则本软件
 *  按"原样"分发，
 *  不提供任何明示或暗示的保证或条件。
 *  有关许可证下的具体权限和限制，请参阅许可证
 *  本身。 */
#pragma once
#include <ipmid/iana.hpp>

#include <cstdint>
#include <optional>
#include <tuple>

namespace ipmi
{

using Iana = oem::Number;

using Group = uint8_t;
constexpr Group groupPICMG = 0x00;
constexpr Group groupDMTG = 0x01;
constexpr Group groupSSI = 0x02;
constexpr Group groupVSO = 0x03;
constexpr Group groupRedfish = 0x52;
#ifdef ARM_SBMR_SUPPORT
constexpr Group groupSBMR = 0xAE;
#endif
constexpr Group groupDCMI = 0xDC;

/*
 * Set the priority as the lowest number that is necessary so
 * it is possible that others can override it if desired.
 * This may be linked to what level of integration the handler
 * is being created at.
 */
/* 将优先级设置为所需的最小数值，
 * 这样如果需要的话，其他人可以覆盖它。
 * 这可能与 handler 被创建时的
 * 集成层次有关。 */
constexpr int prioOpenBmcBase = 10;
constexpr int prioOemBase = 20;
constexpr int prioOdmBase = 30;
constexpr int prioCustomBase = 40;
constexpr int prioMax = 50;

/*
 * Channel IDs pulled from the IPMI 2.0 specification
 */
/* 取自 IPMI 2.0 规范的通道 ID */
constexpr int channelPrimaryIpmb = 0x00;
// 0x01-0x0B Implementation specific
// Implementation specific channel numbers are specified
// by a configuration file external to ipmid
// 0x0C-0x0D reserved
// 0x01-0x0B 特定于实现
// 特定于实现的通道号由
// ipmid 外部的配置文件指定
// 0x0C-0x0D 保留
constexpr int channelCurrentIface = 0x0E; // 'Present I/F'
constexpr int channelSystemIface = 0x0F;

/*
 * Specifies the minimum privilege level required to execute the command
 * This means the command can be executed at a given privilege level or higher
 * privilege level. Those commands which can be executed via system interface
 * only should use SYSTEM_INTERFACE
 */
/* 指定执行命令所需的最低特权级
 * 这意味着命令可在给定特权级或更高特权级上执行。
 * 那些只能通过系统接口执行的命令
 * 应使用 SYSTEM_INTERFACE */
enum class Privilege : uint8_t
{
    None = 0x00,
    Callback,
    User,
    Operator,
    Admin,
    Oem,
};

// IPMI Net Function number as specified by IPMI V2.0 spec.
// IPMI V2.0 规范定义的 Net Function number。
using NetFn = uint8_t;

// IPMI Command for a Net Function number as specified by IPMI V2.0 spec.
// IPMI V2.0 规范定义的某个 Net Function number 的 IPMI Command。
using Cmd = uint8_t;

// ipmi function return the status code
// ipmi 函数返回状态码
using Cc = uint8_t;

// IPMI 2.0 and DCMI 1.5 standard commands, namespaced by NetFn
// OEM and non-standard commands should be defined where they are used
// IPMI 2.0 和 DCMI 1.5 标准命令，按 NetFn 划分命名空间
// OEM 和非标准命令应在使用处定义
namespace app
{
// 0x00 reserved
// 0x00 保留
constexpr Cmd cmdGetDeviceId = 0x01;
constexpr Cmd cmdColdReset = 0x02;
constexpr Cmd cmdWarmReset = 0x03;
constexpr Cmd cmdGetSelfTestResults = 0x04;
constexpr Cmd cmdManufacturingTestOn = 0x05;
constexpr Cmd cmdSetAcpiPowerState = 0x06;
constexpr Cmd cmdGetAcpiPowerState = 0x07;
constexpr Cmd cmdGetDeviceGuid = 0x08;
constexpr Cmd cmdGetNetFnSupport = 0x09;
constexpr Cmd cmdGetCmdSupport = 0x0A;
constexpr Cmd cmdGetCmdSubFnSupport = 0x0B;
constexpr Cmd cmdGetConfigurableCmds = 0x0C;
constexpr Cmd cmdGetConfigurableCmdSubFns = 0x0D;
// 0x0E-0x21 unassigned
// 0x0E-0x21 未分配
constexpr Cmd cmdResetWatchdogTimer = 0x22;
// 0x23 unassigned
// 0x23 未分配
constexpr Cmd cmdSetWatchdogTimer = 0x24;
constexpr Cmd cmdGetWatchdogTimer = 0x25;
// 0x26-0x2D unassigned
// 0x26-0x2D 未分配
constexpr Cmd cmdSetBmcGlobalEnables = 0x2E;
constexpr Cmd cmdGetBmcGlobalEnables = 0x2F;
constexpr Cmd cmdClearMessageFlags = 0x30;
constexpr Cmd cmdGetMessageFlags = 0x31;
constexpr Cmd cmdEnableMessageChannelRcv = 0x32;
constexpr Cmd cmdGetMessage = 0x33;
constexpr Cmd cmdSendMessage = 0x34;
constexpr Cmd cmdReadEventMessageBuffer = 0x35;
constexpr Cmd cmdGetBtIfaceCapabilities = 0x36;
constexpr Cmd cmdGetSystemGuid = 0x37;
constexpr Cmd cmdGetChannelAuthCapabilities = 0x38;
constexpr Cmd cmdGetSessionChallenge = 0x39;
constexpr Cmd cmdActivateSession = 0x3A;
constexpr Cmd cmdSetSessionPrivilegeLevel = 0x3B;
constexpr Cmd cmdCloseSession = 0x3C;
constexpr Cmd cmdGetSessionInfo = 0x3D;
// 0x3E unassigned
// 0x3E 未分配
constexpr Cmd cmdGetAuthCode = 0x3F;
constexpr Cmd cmdSetChannelAccess = 0x40;
constexpr Cmd cmdGetChannelAccess = 0x41;
constexpr Cmd cmdGetChannelInfoCommand = 0x42;
constexpr Cmd cmdSetUserAccessCommand = 0x43;
constexpr Cmd cmdGetUserAccessCommand = 0x44;
constexpr Cmd cmdSetUserName = 0x45;
constexpr Cmd cmdGetUserNameCommand = 0x46;
constexpr Cmd cmdSetUserPasswordCommand = 0x47;
constexpr Cmd cmdActivatePayload = 0x48;
constexpr Cmd cmdDeactivatePayload = 0x49;
constexpr Cmd cmdGetPayloadActivationStatus = 0x4A;
constexpr Cmd cmdGetPayloadInstanceInfo = 0x4B;
constexpr Cmd cmdSetUserPayloadAccess = 0x4C;
constexpr Cmd cmdGetUserPayloadAccess = 0x4D;
constexpr Cmd cmdGetChannelPayloadSupport = 0x4E;
constexpr Cmd cmdGetChannelPayloadVersion = 0x4F;
constexpr Cmd cmdGetChannelOemPayloadInfo = 0x50;
// 0x51 unassigned
// 0x51 未分配
constexpr Cmd cmdMasterWriteRead = 0x52;
// 0x53 unassigned
// 0x53 未分配
constexpr Cmd cmdGetChannelCipherSuites = 0x54;
constexpr Cmd cmdSuspendResumePayloadEnc = 0x55;
constexpr Cmd cmdSetChannelSecurityKeys = 0x56;
constexpr Cmd cmdGetSystemIfCapabilities = 0x57;
constexpr Cmd cmdSetSystemInfoParameters = 0x58;
constexpr Cmd cmdGetSystemInfoParameters = 0x59;
// 0x5A-0x5F unassigned
// 0x5A-0x5F 未分配
constexpr Cmd cmdSetCommandEnables = 0x60;
constexpr Cmd cmdGetCommandEnables = 0x61;
constexpr Cmd cmdSetCommandSubFnEnables = 0x62;
constexpr Cmd cmdGetCommandSubFnEnables = 0x63;
constexpr Cmd cmdGetOemNetFnIanaSupport = 0x64;
// 0x65-0xff unassigned
// 0x65-0xff 未分配
} // namespace app

namespace chassis
{
constexpr Cmd cmdGetChassisCapabilities = 0x00;
constexpr Cmd cmdGetChassisStatus = 0x01;
constexpr Cmd cmdChassisControl = 0x02;
constexpr Cmd cmdChassisReset = 0x03;
constexpr Cmd cmdChassisIdentify = 0x04;
constexpr Cmd cmdSetChassisCapabilities = 0x05;
constexpr Cmd cmdSetPowerRestorePolicy = 0x06;
constexpr Cmd cmdGetSystemRestartCause = 0x07;
constexpr Cmd cmdSetSystemBootOptions = 0x08;
constexpr Cmd cmdGetSystemBootOptions = 0x09;
constexpr Cmd cmdSetFrontPanelButtonEnables = 0x0A;
constexpr Cmd cmdSetPowerCycleInterval = 0x0B;
// 0x0C-0x0E unassigned
// 0x0C-0x0E 未分配
constexpr Cmd cmdGetPohCounter = 0x0F;
// 0x10-0xFF unassigned
// 0x10-0xFF 未分配
} // namespace chassis

namespace sensor_event
{
constexpr Cmd cmdSetEventReceiver = 0x00;
constexpr Cmd cmdGetEventReceiver = 0x01;
constexpr Cmd cmdPlatformEvent = 0x02;
// 0x03-0x0F unassigned
// 0x03-0x0F 未分配
constexpr Cmd cmdGetPefCapabilities = 0x10;
constexpr Cmd cmdArmPefPostponeTimer = 0x11;
constexpr Cmd cmdSetPefConfigurationParams = 0x12;
constexpr Cmd cmdGetPefConfigurationParams = 0x13;
constexpr Cmd cmdSetLastProcessedEventId = 0x14;
constexpr Cmd cmdGetLastProcessedEventId = 0x15;
constexpr Cmd cmdAlertImmediate = 0x16;
constexpr Cmd cmdPetAcknowledge = 0x17;
constexpr Cmd cmdGetDeviceSdrInfo = 0x20;
constexpr Cmd cmdGetDeviceSdr = 0x21;
constexpr Cmd cmdReserveDeviceSdrRepository = 0x22;
constexpr Cmd cmdGetSensorReadingFactors = 0x23;
constexpr Cmd cmdSetSensorHysteresis = 0x24;
constexpr Cmd cmdGetSensorHysteresis = 0x25;
constexpr Cmd cmdSetSensorThreshold = 0x26;
constexpr Cmd cmdGetSensorThreshold = 0x27;
constexpr Cmd cmdSetSensorEventEnable = 0x28;
constexpr Cmd cmdGetSensorEventEnable = 0x29;
constexpr Cmd cmdRearmSensorEvents = 0x2A;
constexpr Cmd cmdGetSensorEventStatus = 0x2B;
constexpr Cmd cmdGetSensorReading = 0x2D;
constexpr Cmd cmdSetSensorType = 0x2E;
constexpr Cmd cmdGetSensorType = 0x2F;
constexpr Cmd cmdSetSensorReadingAndEvtSts = 0x30;
// 0x31-0xFF unassigned
// 0x31-0xFF 未分配
} // namespace sensor_event

namespace storage
{
// 0x00-0x0F unassigned
// 0x00-0x0F 未分配
constexpr Cmd cmdGetFruInventoryAreaInfo = 0x10;
constexpr Cmd cmdReadFruData = 0x11;
constexpr Cmd cmdWriteFruData = 0x12;
// 0x13-0x1F unassigned
// 0x13-0x1F 未分配
constexpr Cmd cmdGetSdrRepositoryInfo = 0x20;
constexpr Cmd cmdGetSdrRepositoryAllocInfo = 0x21;
constexpr Cmd cmdReserveSdrRepository = 0x22;
constexpr Cmd cmdGetSdr = 0x23;
constexpr Cmd cmdAddSdr = 0x24;
constexpr Cmd cmdPartialAddSdr = 0x25;
constexpr Cmd cmdDeleteSdr = 0x26;
constexpr Cmd cmdClearSdrRepository = 0x27;
constexpr Cmd cmdGetSdrRepositoryTime = 0x28;
constexpr Cmd cmdSetSdrRepositoryTime = 0x29;
constexpr Cmd cmdEnterSdrRepoUpdateMode = 0x2A;
constexpr Cmd cmdExitSdrReposUpdateMode = 0x2B;
constexpr Cmd cmdRunInitializationAgent = 0x2C;
// 0x2D-0x3F unassigned
// 0x2D-0x3F 未分配
constexpr Cmd cmdGetSelInfo = 0x40;
constexpr Cmd cmdGetSelAllocationInfo = 0x41;
constexpr Cmd cmdReserveSel = 0x42;
constexpr Cmd cmdGetSelEntry = 0x43;
constexpr Cmd cmdAddSelEntry = 0x44;
constexpr Cmd cmdPartialAddSelEntry = 0x45;
constexpr Cmd cmdDeleteSelEntry = 0x46;
constexpr Cmd cmdClearSel = 0x47;
constexpr Cmd cmdGetSelTime = 0x48;
constexpr Cmd cmdSetSelTime = 0x49;
constexpr Cmd cmdGetAuxiliaryLogStatus = 0x5A;
constexpr Cmd cmdSetAuxiliaryLogStatus = 0x5B;
constexpr Cmd cmdGetSelTimeUtcOffset = 0x5C;
constexpr Cmd cmdSetSelTimeUtcOffset = 0x5D;
// 0x5E-0xFF unassigned
// 0x5E-0xFF 未分配
} // namespace storage

namespace transport
{
constexpr Cmd cmdSetLanConfigParameters = 0x01;
constexpr Cmd cmdGetLanConfigParameters = 0x02;
constexpr Cmd cmdSuspendBmcArps = 0x03;
constexpr Cmd cmdGetIpUdpRmcpStatistics = 0x04;
constexpr Cmd cmdSetSerialModemConfig = 0x10;
constexpr Cmd cmdGetSerialModemConfig = 0x11;
constexpr Cmd cmdSetSerialModemMux = 0x12;
constexpr Cmd cmdGetTapResponseCodes = 0x13;
constexpr Cmd cmdSetPppUdpProxyTransmitData = 0x14;
constexpr Cmd cmdGetPppUdpProxyTransmitData = 0x15;
constexpr Cmd cmdSendPppUdpProxyPacket = 0x16;
constexpr Cmd cmdGetPppUdpProxyReceiveData = 0x17;
constexpr Cmd cmdSerialModemConnActive = 0x18;
constexpr Cmd cmdCallback = 0x19;
constexpr Cmd cmdSetUserCallbackOptions = 0x1A;
constexpr Cmd cmdGetUserCallbackOptions = 0x1B;
constexpr Cmd cmdSetSerialRoutingMux = 0x1C;
constexpr Cmd cmdSolActivating = 0x20;
constexpr Cmd cmdSetSolConfigParameters = 0x21;
constexpr Cmd cmdGetSolConfigParameters = 0x22;
constexpr Cmd cmdForwardedCommand = 0x30;
constexpr Cmd cmdSetForwardedCommands = 0x31;
constexpr Cmd cmdGetForwardedCommands = 0x32;
constexpr Cmd cmdEnableForwardedCommands = 0x33;
} // namespace transport

namespace bridge
{
constexpr Cmd cmdGetBridgeState = 0x00;
constexpr Cmd cmdSetBridgeState = 0x01;
constexpr Cmd cmdGetIcmbAddress = 0x02;
constexpr Cmd cmdSetIcmbAddress = 0x03;
constexpr Cmd cmdSetBridgeProxyAddress = 0x04;
constexpr Cmd cmdGetBridgeStatistics = 0x05;
constexpr Cmd cmdGetIcmbCapabilities = 0x06;
constexpr Cmd cmdClearBridgeStatistics = 0x08;
constexpr Cmd cmdGetBridgeProxyAddress = 0x09;
constexpr Cmd cmdGetIcmbConnectorInfo = 0x0A;
constexpr Cmd cmdGetIcmbConnectionId = 0x0B;
constexpr Cmd cmdSendIcmbConnectionId = 0x0C;
constexpr Cmd cmdPrepareForDiscovery = 0x10;
constexpr Cmd cmdGetAddresses = 0x11;
constexpr Cmd cmdSetDiscovered = 0x12;
constexpr Cmd cmdGetChassisDeviceId = 0x13;
constexpr Cmd cmdSetChassisDeviceId = 0x14;
constexpr Cmd cmdBridgeRequest = 0x20;
constexpr Cmd cmdBridgeMessage = 0x21;
// 0x22-0x2F unassigned
// 0x22-0x2F 未分配
constexpr Cmd cmdGetEventCount = 0x30;
constexpr Cmd cmdSetEventDestination = 0x31;
constexpr Cmd cmdSetEventReceptionState = 0x32;
constexpr Cmd cmdSendIcmbEventMessage = 0x33;
constexpr Cmd cmdGetEventDestination = 0x34;
constexpr Cmd cmdGetEventReceptionState = 0x35;
// 0xC0-0xFE OEM Commands
// 0xC0-0xFE OEM Commands
constexpr Cmd cmdErrorReport = 0xFF;
} // namespace bridge

namespace dcmi
{
constexpr Cmd cmdGetDcmiCapabilitiesInfo = 0x01;
constexpr Cmd cmdGetPowerReading = 0x02;
constexpr Cmd cmdGetPowerLimit = 0x03;
constexpr Cmd cmdSetPowerLimit = 0x04;
constexpr Cmd cmdActDeactivatePwrLimit = 0x05;
constexpr Cmd cmdGetAssetTag = 0x06;
constexpr Cmd cmdGetDcmiSensorInfo = 0x07;
constexpr Cmd cmdSetAssetTag = 0x08;
constexpr Cmd cmdGetMgmtCntlrIdString = 0x09;
constexpr Cmd cmdSetMgmtCntlrIdString = 0x0A;
constexpr Cmd cmdSetThermalLimit = 0x0B;
constexpr Cmd cmdGetThermalLimit = 0x0C;
constexpr Cmd cmdGetTemperatureReadings = 0x10;
constexpr Cmd cmdSetDcmiConfigParameters = 0x12;
constexpr Cmd cmdGetDcmiConfigParameters = 0x13;
} // namespace dcmi

#ifdef ARM_SBMR_SUPPORT
namespace sbmr
{
constexpr Cmd cmdSendBootProgressCode = 0x02;
constexpr Cmd cmdGetBootProgressCode = 0x03;
} // namespace sbmr
#endif

// These are the command network functions, the response
// network functions are the function + 1. So to determine
// the proper network function which issued the command
// associated with a response, subtract 1.
// Note: these will be left shifted when combined with the LUN
// 这些是命令的网络功能号，响应的
// 网络功能号是功能号 + 1。因此要确定
// 发出与某个响应相关联的命令的
// 正确网络功能号，需减去 1。
// 注意：与 LUN 组合时这些值将左移。
constexpr NetFn netFnChassis = 0x00;
constexpr NetFn netFnBridge = 0x02;
constexpr NetFn netFnSensor = 0x04;
constexpr NetFn netFnApp = 0x06;
constexpr NetFn netFnFirmware = 0x08;
constexpr NetFn netFnStorage = 0x0A;
constexpr NetFn netFnTransport = 0x0C;
// reserved 0Eh..28h
// 保留 0Eh..28h
constexpr NetFn netFnGroup = 0x2C;
constexpr NetFn netFnOem = 0x2E;
constexpr NetFn netFnOemOne = 0x30;
constexpr NetFn netFnOemTwo = 0x32;
constexpr NetFn netFnOemThree = 0x34;
constexpr NetFn netFnOemFour = 0x36;
constexpr NetFn netFnOemFive = 0x38;
constexpr NetFn netFnOemSix = 0x3A;
constexpr NetFn netFnOemSeven = 0x3C;
constexpr NetFn netFnOemEight = 0x3E;

// IPMI commands for net functions. Callbacks using this should be careful to
// parse arguments to the sub-functions and can take advantage of the built-in
// message handling mechanism to create custom routing
// 用于网络功能号的 IPMI 命令。使用它的回调应注意
// 解析子函数的参数，并可以利用内置的
// 消息处理机制来创建自定义路由
constexpr Cmd cmdWildcard = 0xFF;

// IPMI standard completion codes specified by the IPMI V2.0 spec.
//
// This might have been an enum class, but that would make it hard for
// OEM- and command-specific completion codes to be added elsewhere.
//
// Custom completion codes can be defined in individual modules for
// command specific errors in the 0x80-0xBE range
//
// Alternately, OEM completion codes are in the 0x01-0x7E range
// IPMI V2.0 规范定义的 IPMI 标准完成码。
// 这本来可以是 enum class，但那样会让
// 在其他地方添加 OEM 和命令特定的完成码变得困难。
// 自定义完成码可以在各个模块中定义，
// 用于 0x80-0xBE 范围内的命令特定错误
// 或者，OEM 完成码在 0x01-0x7E 范围内
constexpr Cc ccSuccess = 0x00;
constexpr Cc ccBusy = 0xC0;
constexpr Cc ccInvalidCommand = 0xC1;
constexpr Cc ccInvalidCommandOnLun = 0xC2;
constexpr Cc ccTimeout = 0xC3;
constexpr Cc ccOutOfSpace = 0xC4;
constexpr Cc ccInvalidReservationId = 0xC5;
constexpr Cc ccReqDataTruncated = 0xC6;
constexpr Cc ccReqDataLenInvalid = 0xC7;
constexpr Cc ccReqDataLenExceeded = 0xC8;
constexpr Cc ccParmOutOfRange = 0xC9;
constexpr Cc ccRetBytesUnavailable = 0xCA;
constexpr Cc ccSensorInvalid = 0xCB;
constexpr Cc ccInvalidFieldRequest = 0xCC;
constexpr Cc ccIllegalCommand = 0xCD;
constexpr Cc ccResponseError = 0xCE;
constexpr Cc ccDuplicateRequest = 0xCF;
constexpr Cc ccCmdFailSdrMode = 0xD0;
constexpr Cc ccCmdFailFwUpdMode = 0xD1;
constexpr Cc ccCmdFailInitAgent = 0xD2;
constexpr Cc ccDestinationUnavailable = 0xD3;
constexpr Cc ccInsufficientPrivilege = 0xD4;
constexpr Cc ccCommandNotAvailable = 0xD5;
constexpr Cc ccCommandDisabled = 0xD6;
constexpr Cc ccUnspecifiedError = 0xFF;

/* ipmi often has two return types:
 * 1. Failure: CC is non-zero; no trailing data
 * 2. Success: CC is zero; trailing data (usually a fixed type)
 *
 * using ipmi::response(cc, ...), it will automatically always pack
 * the correct type for the response without having to explicitly type out all
 * the parameters that the function would return.
 *
 * To enable this feature, you just define the ipmi function as returning an
 * ipmi::RspType which has the optional trailing data built in, with your types
 * defined as parameters.
 */
/* ipmi 通常有两种返回类型：
 * 1. 失败：CC 非零；没有尾随数据
 * 2. 成功：CC 为零；有尾随数据（通常是固定类型）
 * 使用 ipmi::response(cc, ...)，它会自动为响应打包
 * 正确的类型，无需显式写出函数将返回的
 * 所有参数。
 * 要启用此特性，只需将 ipmi 函数定义为返回
 * 一个内置可选尾随数据的 ipmi::RspType，
 * 并将你的类型定义为参数。 */

template <typename... RetTypes>
using RspType = std::tuple<ipmi::Cc, std::optional<std::tuple<RetTypes...>>>;

/**
 * @brief helper function to create an IPMI response tuple
 *
 * IPMI handlers all return a tuple with two parts: a completion code and an
 * optional tuple containing the rest of the data to return. This helper
 * function makes it easier by constructing that out of an arbitrary number of
 * arguments.
 *
 * @param cc - the completion code for the response
 * @param args... - the optional list of values to return
 *
 * @return a standard IPMI return type (as described above)
 */
/* @brief 创建 IPMI 响应 tuple 的辅助函数
 * IPMI handler 都返回一个由两部分组成的 tuple：完成码和
 * 一个包含其余要返回数据的可选 tuple。该辅助函数
 * 通过从任意数量的参数构造它
 * 来简化操作。
 * @param cc - 响应的完成码
 * @param args... - 要返回的可选值列表
 * @return 标准的 IPMI 返回类型（如上所述） */
template <typename... Args>
static inline auto response(ipmi::Cc cc, Args&&... args)
{
    return std::make_tuple(cc, std::make_optional(std::make_tuple(args...)));
}
static inline auto response(ipmi::Cc cc)
{
    return std::make_tuple(cc, std::nullopt);
}

/**
 * @brief helper function to create an IPMI success response tuple
 *
 * IPMI handlers all return a tuple with two parts: a completion code and an
 * optional tuple containing the rest of the data to return. This helper
 * function makes it easier by constructing that out of an arbitrary number of
 * arguments. Because it is a success response, this automatically packs
 * the completion code, without needing to explicitly pass it in.
 *
 * @param args... - the optional list of values to return
 *
 * @return a standard IPMI return type (as described above)
 */
/* @brief 创建 IPMI 成功响应 tuple 的辅助函数
 * IPMI handler 都返回一个由两部分组成的 tuple：完成码和
 * 一个包含其余要返回数据的可选 tuple。该辅助函数
 * 通过从任意数量的参数构造它来简化操作。
 * 由于是成功响应，它会自动打包完成码，
 * 无需显式传入。
 * @param args... - 要返回的可选值列表
 * @return 标准的 IPMI 返回类型（如上所述） */
template <typename... Args>
static inline auto responseSuccess(Args&&... args)
{
    return response(ipmi::ccSuccess, args...);
}
static inline auto responseSuccess()
{
    return response(ipmi::ccSuccess);
}

/* helper functions for the various standard error response types */
/* 各种标准错误响应类型的辅助函数 */
static inline auto responseBusy()
{
    return response(ccBusy);
}
static inline auto responseInvalidCommand()
{
    return response(ccInvalidCommand);
}
static inline auto responseInvalidCommandOnLun()
{
    return response(ccInvalidCommandOnLun);
}
static inline auto responseTimeout()
{
    return response(ccTimeout);
}
static inline auto responseOutOfSpace()
{
    return response(ccOutOfSpace);
}
static inline auto responseInvalidReservationId()
{
    return response(ccInvalidReservationId);
}
static inline auto responseReqDataTruncated()
{
    return response(ccReqDataTruncated);
}
static inline auto responseReqDataLenInvalid()
{
    return response(ccReqDataLenInvalid);
}
static inline auto responseReqDataLenExceeded()
{
    return response(ccReqDataLenExceeded);
}
static inline auto responseParmOutOfRange()
{
    return response(ccParmOutOfRange);
}
static inline auto responseRetBytesUnavailable()
{
    return response(ccRetBytesUnavailable);
}
static inline auto responseSensorInvalid()
{
    return response(ccSensorInvalid);
}
static inline auto responseInvalidFieldRequest()
{
    return response(ccInvalidFieldRequest);
}
static inline auto responseIllegalCommand()
{
    return response(ccIllegalCommand);
}
static inline auto responseResponseError()
{
    return response(ccResponseError);
}
static inline auto responseDuplicateRequest()
{
    return response(ccDuplicateRequest);
}
static inline auto responseCmdFailSdrMode()
{
    return response(ccCmdFailSdrMode);
}
static inline auto responseCmdFailFwUpdMode()
{
    return response(ccCmdFailFwUpdMode);
}
static inline auto responseCmdFailInitAgent()
{
    return response(ccCmdFailInitAgent);
}
static inline auto responseDestinationUnavailable()
{
    return response(ccDestinationUnavailable);
}
static inline auto responseInsufficientPrivilege()
{
    return response(ccInsufficientPrivilege);
}
static inline auto responseCommandNotAvailable()
{
    return response(ccCommandNotAvailable);
}
static inline auto responseCommandDisabled()
{
    return response(ccCommandDisabled);
}
static inline auto responseUnspecifiedError()
{
    return response(ccUnspecifiedError);
}

} // namespace ipmi
