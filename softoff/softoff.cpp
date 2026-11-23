
/**
 * @file softoff.cpp
 * @brief 软关机流程实现：通知 host 执行软关闭。
 */
/*  @file softoff.cpp
 *  @brief 软关机流程实现：通知 host 执行软关闭。 */
/**
 * Copyright © 2016 IBM Corporation
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
 */
/*  Copyright © 2016 IBM Corporation
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *      http://www.apache.org/licenses/LICENSE-2.0
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License. */
#include "config.h"

#include "softoff.hpp"

#include <ipmid/utils.hpp>
#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Control/Host/server.hpp>

#include <chrono>
namespace phosphor
{
namespace ipmi
{

using namespace sdbusplus::server::xyz::openbmc_project::control;

void SoftPowerOff::sendHostShutDownCmd()
{
    auto ctrlHostPath =
        std::string{CONTROL_HOST_OBJ_MGR} + '/' + HOST_NAME + '0';
    auto host = ::ipmi::getService(this->bus, CONTROL_HOST_BUSNAME,
                                   ctrlHostPath.c_str());

    auto method = bus.new_method_call(host.c_str(), ctrlHostPath.c_str(),
                                      CONTROL_HOST_BUSNAME, "Execute");

    method.append(convertForMessage(Host::Command::SoftOff).c_str());
    try
    {
        auto reply = bus.call(method);
    }
    catch (const std::exception& e)
    {
        lg2::error("Error in call to control host Execute: {ERROR}", "ERROR",
                   e);
        // TODO openbmc/openbmc#851 - Once available, throw returned error
        // TODO openbmc/openbmc#851 - 一旦可用，就抛出返回的错误
        throw std::runtime_error("Error in call to control host Execute");
    }
}

// Function called on host control signals
// 在 host 控制信号上调用的函数
void SoftPowerOff::hostControlEvent(sdbusplus::message_t& msg)
{
    std::string cmdCompleted{};
    std::string cmdStatus{};

    msg.read(cmdCompleted, cmdStatus);

    lg2::debug(
        "Host control signal values, command: {COMMAND}, status:{STATUS}",
        "COMMAND", cmdCompleted, "STATUS", cmdStatus);

    if (Host::convertResultFromString(cmdStatus) == Host::Result::Success)
    {
        // Set our internal property indicating we got host attention
        // 设置内部属性，表示已收到 host 的注意信号
        sdbusplus::server::xyz::openbmc_project::ipmi::internal::SoftPowerOff::
            responseReceived(HostResponse::SoftOffReceived);

        // Start timer for host shutdown
        // 为 host 关机启动定时器
        using namespace std::chrono;
        auto time = duration_cast<microseconds>(
            seconds(IPMI_HOST_SHUTDOWN_COMPLETE_TIMEOUT_SECS));
        auto r = startTimer(time);
        if (r < 0)
        {
            lg2::error(
                "Failure to start Host shutdown wait timer, ERRNO: {ERRNO}",
                "ERRNO", lg2::hex, -r);
        }
        else
        {
            lg2::info("Timer started waiting for host to shutdown, "
                      "TIMEOUT_IN_MSEC: {TIMEOUT_IN_MSEC}",
                      "TIMEOUT_IN_MSEC",
                      (duration_cast<milliseconds>(
                           seconds(IPMI_HOST_SHUTDOWN_COMPLETE_TIMEOUT_SECS)))
                          .count());
        }
    }
    else
    {
        // An error on the initial attention is not considered an error, just
        // exit normally and allow remaining shutdown targets to run
        // 初始 attention 出错不视为错误，只需
        // 正常退出，让其余关机 target 继续执行
        lg2::info("Timeout on host attention, continue with power down");
        completed = true;
    }
    return;
}

// Starts a timer
// 启动定时器
int SoftPowerOff::startTimer(const std::chrono::microseconds& usec)
{
    return timer.start(usec);
}

// Host Response handler
// Host 响应处理函数
auto SoftPowerOff::responseReceived(HostResponse response) -> HostResponse
{
    using namespace std::chrono;

    if (response == HostResponse::HostShutdown)
    {
        // Disable the timer since Host has quiesced and we are
        // done with soft power off part
        // 由于 Host 已静默、软关机部分已完成，
        // 停用定时器
        auto r = timer.stop();
        if (r < 0)
        {
            lg2::error("Failure to STOP the timer, ERRNO: {ERRNO}", "ERRNO",
                       lg2::hex, -r);
        }

        // This marks the completion of soft power off sequence.
        // 这标志着软关机流程完成。
        completed = true;
    }

    return sdbusplus::server::xyz::openbmc_project::ipmi::internal::
        SoftPowerOff::responseReceived(response);
}

} // namespace ipmi
} // namespace phosphor
