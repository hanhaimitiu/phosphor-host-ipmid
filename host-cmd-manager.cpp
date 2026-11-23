
/**
 * @file host-cmd-manager.cpp
 * @brief host 命令队列管理：把 IPMI 请求排队后发给 host。
 */
/* @file host-cmd-manager.cpp
 * @brief host 命令队列管理：把 IPMI 请求排队后发给 host。 */
#include "config.h"

#include "host-cmd-manager.hpp"

#include "systemintfcmds.hpp"

#include <ipmid/utils.hpp>
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/message/types.hpp>
#include <sdbusplus/timer.hpp>
#include <xyz/openbmc_project/Common/error.hpp>
#include <xyz/openbmc_project/State/Host/server.hpp>

#include <chrono>

using HostState = sdbusplus::common::xyz::openbmc_project::state::Host;

namespace phosphor
{
namespace host
{
namespace command
{

// For throwing exceptions
// 用于抛出异常
using namespace phosphor::logging;
using InternalFailure =
    sdbusplus::error::xyz::openbmc_project::common::InternalFailure;

namespace sdbusRule = sdbusplus::match_rules;

Manager::Manager(sdbusplus::bus_t& bus) :
    bus(bus), timer(std::bind(&Manager::hostTimeout, this)),
    hostTransitionMatch(
        bus,
        sdbusRule::propertiesChanged("/xyz/openbmc_project/state/host0",
                                     HostState::interface),
        std::bind(&Manager::clearQueueOnPowerOn, this, std::placeholders::_1))
{
    // Nothing to do here.
    // 此处无需处理。
}

// Called as part of READ_MSG_DATA command
// 作为 READ_MSG_DATA 命令的一部分被调用
IpmiCmdData Manager::getNextCommand()
{
    // Stop the timer. Don't have to Err failure doing so.
    // 停止定时器。此操作不必报错。
    auto r = timer.stop();
    if (r < 0)
    {
        lg2::error("Failure to STOP the timer: {ERROR}", "ERROR", strerror(-r));
    }

    if (this->workQueue.empty())
    {
        // Just return a heartbeat in this case.  A spurious SMS_ATN was
        // asserted for the host (probably from a previous boot).
        // 这种情况下只需返回心跳。曾为主机置位过一个虚假的
        // SMS_ATN（可能来自上一次启动）。
        lg2::debug("Control Host work queue is empty!");

        return std::make_pair(CMD_HEARTBEAT, 0x00);
    }

    // Pop the processed entry off the queue
    // 从队列中弹出已处理的条目
    auto command = this->workQueue.front();
    this->workQueue.pop();

    // IPMI command is the first element in pair
    // IPMI 命令是 pair 中的第一个元素
    auto ipmiCmdData = std::get<0>(command);

    // Now, call the user registered functions so that
    // implementation specific CommandComplete signals
    // can be sent. `true` indicating Success.
    // 现在调用用户注册的函数，以便
    // 发送各实现特定的 CommandComplete 信号。
    // `true` 表示成功。
    std::get<CallBack>(command)(ipmiCmdData, true);

    // Check for another entry in the queue and kick it off
    // 检查队列中是否还有其他条目并启动它
    this->checkQueueAndAlertHost();

    // Tuple of command and data
    // 命令和数据的元组
    return ipmiCmdData;
}

// Called when initial timer goes off post sending SMS_ATN
// 在发送 SMS_ATN 后初始定时器到期时调用
void Manager::hostTimeout()
{
    lg2::error("Host control timeout hit!");

    clearQueue();
}

void Manager::clearQueue()
{
    // Dequeue all entries and send fail signal
    // 出队所有条目并发送失败信号
    while (!this->workQueue.empty())
    {
        auto command = this->workQueue.front();
        this->workQueue.pop();

        // IPMI command is the first element in pair
        // IPMI 命令是 pair 中的第一个元素
        auto ipmiCmdData = std::get<0>(command);

        // Call the implementation specific Command Failure.
        // `false` indicating Failure
        // 调用各实现特定的 Command Failure。
        // `false` 表示失败。
        std::get<CallBack>(command)(ipmiCmdData, false);
    }
}

// Called for alerting the host
// 用于通知主机
void Manager::checkQueueAndAlertHost()
{
    if (this->workQueue.size() >= 1)
    {
        lg2::debug("Asserting SMS Attention");

        std::string HOST_IPMI_SVC("org.openbmc.HostIpmi");
        std::string IPMI_PATH("/org/openbmc/HostIpmi/1");
        std::string IPMI_INTERFACE("org.openbmc.HostIpmi");

        // Start the timer for this transaction
        // 为该事务启动定时器
        auto time = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::seconds(IPMI_SMS_ATN_ACK_TIMEOUT_SECS));

        auto r = timer.start(time);
        if (r < 0)
        {
            lg2::error("Error starting timer for control host");
            return;
        }

        auto method =
            this->bus.new_method_call(HOST_IPMI_SVC.c_str(), IPMI_PATH.c_str(),
                                      IPMI_INTERFACE.c_str(), "setAttention");

        try
        {
            auto reply = this->bus.call(method);

            lg2::debug("SMS Attention asserted");
        }
        catch (sdbusplus::exception_t& e)
        {
            lg2::error("Error when call setAttention method");
        }
    }
}

// Called by specific implementations that provide commands
// 由提供命令的具体实现调用
void Manager::execute(CommandHandler command)
{
    lg2::debug("Pushing cmd on to queue, command: {COMMAND}", "COMMAND",
               std::get<0>(command).first);

    this->workQueue.emplace(command);

    // Alert host if this is only command in queue otherwise host will
    // be notified of next message after processing the current one
    // 若队列中只有这一条命令则通知主机，否则主机将在
    // 处理完当前消息后收到下一条消息的通知。
    if (this->workQueue.size() == 1)
    {
        this->checkQueueAndAlertHost();
    }
    else
    {
        lg2::info("Command in process, no attention");
    }

    return;
}

void Manager::clearQueueOnPowerOn(sdbusplus::message_t& msg)
{
    namespace server = sdbusplus::server::xyz::openbmc_project::state;

    ::ipmi::DbusInterface interface;
    ::ipmi::PropertyMap properties;

    msg.read(interface, properties);

    if (properties.find(HostState::property_names::requested_host_transition) ==
        properties.end())
    {
        return;
    }

    auto& requestedState = std::get<std::string>(
        properties.at(HostState::property_names::requested_host_transition));

    if (server::Host::convertTransitionFromString(requestedState) ==
        server::Host::Transition::On)
    {
        clearQueue();
    }
}

} // namespace command
} // namespace host
} // namespace phosphor
