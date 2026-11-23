
/**
 * @file mainapp.cpp
 * @brief softoff（软关机服务）入口。
 */
/*  @file mainapp.cpp
 *  @brief softoff（软关机服务）入口。 */
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

#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdeventplus/event.hpp>
#include <sdeventplus/exception.hpp>
#include <xyz/openbmc_project/State/Host/error.hpp>

// Return -1 on any errors to ensure we follow the calling targets OnFailure=
// path
// 发生任何错误都返回 -1，以确保遵循调用方 target 的
// OnFailure= 路径
int main(int, char**)
{
    using namespace phosphor::logging;

    // Get a handle to system dbus.
    // 获取 system dbus 的句柄。
    auto bus = sdbusplus::bus::new_default();

    // Add systemd object manager.
    // 添加 systemd object manager。
    sdbusplus::server::manager_t(bus, SOFTOFF_OBJPATH);

    // Get default event loop
    // 获取默认事件循环
    auto event = sdeventplus::Event::get_default();

    // Attach the bus to sd_event to service user requests
    // 将 bus 附加到 sd_event，以处理用户请求
    bus.attach_event(event.get(), SD_EVENT_PRIORITY_NORMAL);

    // Claim the bus. Delaying it until sending SMS_ATN may result
    // in a race condition between this available and IPMI trying to send
    // message as a response to ack from host.
    // 占用 bus。若延迟到发送 SMS_ATN 之后才占用，可能导致
    // 本服务可用与 IPMI 试图发送消息作为对 host ack 的响应
    // 之间产生竞态条件。
    bus.request_name(SOFTOFF_BUSNAME);

    // Create the SoftPowerOff object.
    // 创建 SoftPowerOff 对象。
    phosphor::ipmi::SoftPowerOff powerObj(bus, event.get(), SOFTOFF_OBJPATH);

    // Wait for client requests until this application has processed
    // at least one successful SoftPowerOff or we timed out
    // 等待客户端请求，直到本应用至少成功处理了一次
    // SoftPowerOff，或发生超时
    while (!powerObj.isCompleted() && !powerObj.isTimerExpired())
    {
        try
        {
            event.run(std::nullopt);
        }
        catch (const sdeventplus::SdEventError& e)
        {
            lg2::error("Failure in processing request: {ERROR}", "ERROR", e);
            return 1;
        }
    }

    // Log an error if we timed out after getting Ack for SMS_ATN and before
    // getting the Host Shutdown response
    // 如果在收到 SMS_ATN 的 Ack 之后、收到 Host Shutdown 响应之前
    // 发生超时，则记录错误
    if (powerObj.isTimerExpired() &&
        (powerObj.responseReceived() ==
         phosphor::ipmi::Base::SoftPowerOff::HostResponse::SoftOffReceived))
    {
        using error =
            sdbusplus::error::xyz::openbmc_project::state::host::SoftOffTimeout;
        using errorMetadata = xyz::openbmc_project::state::host::SoftOffTimeout;
        report<error>(prev_entry<errorMetadata::TIMEOUT_IN_MSEC>());
        return -1;
    }

    return 0;
}
