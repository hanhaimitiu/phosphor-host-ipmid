
/**
 * @file host-interface.cpp
 * @brief org.openbmc.HostIpmi D-Bus 接口的实现（对接 host 内核驱动）。
 */
/* @file host-interface.cpp
 * @brief org.openbmc.HostIpmi D-Bus 接口的实现（对接 host 内核驱动）。 */

#include "config.h"

#include "host-interface.hpp"

#include "systemintfcmds.hpp"

#include <ipmid-host/cmd-utils.hpp>
#include <ipmid-host/cmd.hpp>
#include <ipmid/api.hpp>
#include <ipmid/utils.hpp>
#include <phosphor-logging/lg2.hpp>

#include <functional>
#include <memory>
#include <optional>

namespace phosphor
{
namespace host
{
namespace command
{

// When you see Base:: you know we're referencing our base class
// 看到 Base:: 就知道我们引用的是基类
namespace Base = sdbusplus::server::xyz::openbmc_project::control;

// IPMI OEM command.
// https://github.com/openbmc/openbmc/issues/2082 for handling
// Non-OEM commands that need to send SMS_ATN
// IPMI OEM 命令。
// https://github.com/openbmc/openbmc/issues/2082 中讨论了如何处理
// 需要发送 SMS_ATN 的非 OEM 命令。
using OEMCmd = uint8_t;

// Map of IPMI OEM command to its equivalent interface command.
// This is needed when invoking the callback handler to indicate
// the status of the executed command.
// IPMI OEM 命令到其对应接口命令的映射。
// 调用回调处理程序以指示已执行命令的
// 状态时需要使用此映射。
static const std::map<OEMCmd, Host::Command> intfCommand = {
    {CMD_HEARTBEAT, Base::Host::Command::Heartbeat},
    {CMD_POWER, Base::Host::Command::SoftOff}};

// Map of Interface command to its corresponding IPMI OEM command.
// This is needed when pushing IPMI commands to command manager's
// queue. The same pair will be returned when IPMI asks us
// why a SMS_ATN was sent
// 接口命令到其对应 IPMI OEM 命令的映射。
// 将 IPMI 命令压入命令管理器的队列时需要使用。
// 当 IPMI 询问我们为何发送 SMS_ATN 时，
// 会返回同一对命令。
static const std::map<Host::Command, IpmiCmdData> ipmiCommand = {
    {Base::Host::Command::Heartbeat, std::make_pair(CMD_HEARTBEAT, 0x00)},
    {Base::Host::Command::SoftOff, std::make_pair(CMD_POWER, SOFT_OFF)}};

// Called at user request
// 用户请求时调用
void Host::execute(Base::Host::Command command)
{
    lg2::debug("Pushing cmd on to queue, control host cmd: {CONTROL_HOST_CMD}",
               "CONTROL_HOST_CMD", convertForMessage(command));

    auto cmd = std::make_tuple(
        ipmiCommand.at(command),
        std::bind(&Host::commandStatusHandler, this, std::placeholders::_1,
                  std::placeholders::_2));

    ipmid_send_cmd_to_host(std::move(cmd));
}

// Called into by Command Manager
// 由 Command Manager 调用
void Host::commandStatusHandler(IpmiCmdData cmd, bool status)
{
    // Need to convert <cmd> to the equivalent one mentioned in spec
    // 需要将 <cmd> 转换为规范中所述的对应命令
    auto value = status ? Result::Success : Result::Failure;

    // Fire a signal
    // 发出信号
    this->commandComplete(intfCommand.at(std::get<0>(cmd)), value);
}

Host::FirmwareCondition Host::currentFirmwareCondition() const
{
    // shared object used to wait for host response
    // 用于等待主机响应的共享对象
    auto hostCondition =
        std::make_shared<std::optional<Host::FirmwareCondition>>();

    // callback for command to host
    // 发往主机命令的回调
    auto hostAckCallback = [hostCondition](IpmiCmdData, bool status) {
        auto value = status ? Host::FirmwareCondition::Running
                            : Host::FirmwareCondition::Off;

        lg2::debug("currentFirmwareCondition:hostAckCallback fired, "
                   "control host cmd: {CONTROL_HOST_CMD}",
                   "CONTROL_HOST_CMD", value);

        *(hostCondition.get()) = value;
        return;
    };

    auto cmd = phosphor::host::command::CommandHandler(
        ipmiCommand.at(Base::Host::Command::Heartbeat),
        std::move(hostAckCallback));

    ipmid_send_cmd_to_host(std::move(cmd));

    // Timer to ensure this function returns something within a reasonable time
    // 定时器，用于确保本函数在合理时间内返回
    sdbusplus::Timer hostAckTimer([hostCondition]() {
        lg2::debug("currentFirmwareCondition: timer expired!");
        *(hostCondition.get()) = Host::FirmwareCondition::Off;
    });

    // Wait 1 second past the ATN_ACK timeout to ensure we wait for as
    // long as the timeout
    // 在 ATN_ACK 超时后再等待 1 秒，以确保
    // 等待时长不少于超时时间
    hostAckTimer.start(std::chrono::seconds(IPMI_SMS_ATN_ACK_TIMEOUT_SECS + 1));

    auto io = getIoContext();

    while (!hostCondition.get()->has_value())
    {
        lg2::debug("currentFirmwareCondition: waiting for host response");
        io->run_for(std::chrono::milliseconds(100));
    }
    hostAckTimer.stop();

    lg2::debug("currentFirmwareCondition: hostCondition is ready!");
    return hostCondition.get()->value();
}

} // namespace command
} // namespace host
} // namespace phosphor
