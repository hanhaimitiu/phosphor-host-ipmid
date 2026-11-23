#pragma once

/**
 * @file cmd-utils.hpp
 * @brief host 侧 IPMI 命令（ipmid-host 协议）的公共定义。
 */
/*  @file cmd-utils.hpp
 *  @brief host 侧 IPMI 命令（ipmid-host 协议）的公共定义。 */

#include <unistd.h>

#include <cstdint>
#include <functional>
#include <tuple>

namespace phosphor
{
namespace host
{
namespace command
{
/** @detail After sending SMS_ATN to the Host, Host comes down and
 *          asks why an 'SMS_ATN` was sent.
 *          BMC then sends 'There is a Message to be Read` as response.
 *          Host then comes down asks for Message and the specified
 *          commands and data would go as data conforming to IPMI spec.
 *
 *          Refer: 6.13.2 Send Message Command From System Interface
 *          in IPMI V2.0 spec.
 */
/* @detail 向 Host 发送 SMS_ATN 后，Host 会下来询问
 * 发送 'SMS_ATN' 的原因。
 * BMC 随后发送 'There is a Message to be Read' 作为响应。
 * Host 再下来请求消息，指定的命令和数据
 * 将作为符合 IPMI 规范的数据发出。
 *          参考：IPMI V2.0 规范中 6.13.2 Send Message Command From System Interface
 * （从系统接口发送消息命令）一节。 */

/** @brief IPMI command */
/* @brief IPMI 命令 */
using IPMIcmd = uint8_t;

/** @brief Data associated with command */
/* @brief 与命令关联的数据 */
using Data = uint8_t;

/** @brief <IPMI command, Data> to be sent as payload when Host asks for
 *          the message that can be associated with the previous SMS_ATN
 */
/* @brief 当 Host 请求与上一次 SMS_ATN 相关联的消息时，
 * 作为 payload 发送的 <IPMI command, Data> */
using IpmiCmdData = std::pair<IPMIcmd, Data>;

/** @detail Implementation specific callback function to be invoked
 *          conveying the status of the executed command. Specific
 *          implementations may then broadcast an agreed signal
 */
/* @detail 特定实现的回调函数，在被调用时
 * 传达已执行命令的状态。具体的实现
 * 随后可广播一个约定的信号。 */
using CallBack = std::function<void(IpmiCmdData, bool)>;

/** @detail Tuple encapsulating above 2 to enable using Manager by
 *          different implementations. Users of Manager will supply
 *          <Ipmi command, Data> along with the callback handler.
 *          Manager will invoke the handler conveying the status of
 *          the command.
 */
/* @detail 封装上述两项的 tuple，便于不同实现使用 Manager。
 * Manager 的使用者将提供
 * <Ipmi command, Data> 以及回调处理函数。
 * Manager 将调用该处理函数，传达命令的
 * 执行状态。 */
using CommandHandler = std::tuple<IpmiCmdData, CallBack>;

} // namespace command
} // namespace host
} // namespace phosphor
