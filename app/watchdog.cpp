
/**
 * @file watchdog.cpp
 * @brief IPMI Watchdog（看门狗）命令的实现。
 */
/*  @file watchdog.cpp
 *  @brief IPMI Watchdog（看门狗）命令的实现。 */
#include "watchdog.hpp"

#include "watchdog_service.hpp"

#include <endian.h>

#include <ipmid/api.hpp>
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

#include <bitset>
#include <cstdint>
#include <string>

using phosphor::logging::commit;
using phosphor::logging::level;
using phosphor::logging::log;
using sdbusplus::error::xyz::openbmc_project::common::InternalFailure;

static bool lastCallSuccessful = false;

namespace ipmi
{
constexpr Cc ccWatchdogNotInit = 0x80;

static inline auto responseWatchdogNotInit()
{
    return response(ccWatchdogNotInit);
}
} // namespace ipmi

void reportError()
{
    // We don't want to fill the SEL with errors if the daemon dies and doesn't
    // come back but the watchdog keeps on ticking. Instead, we only report the
    // error if we haven't reported one since the last successful call
    // 如果守护进程挂掉后没有恢复，而看门狗仍在持续计时，
    // 我们不希望 SEL 被错误填满。因此，只有在自上次成功调用
    // 以来尚未报告过错误时，才报告该错误。
    if (!lastCallSuccessful)
    {
        return;
    }
    lastCallSuccessful = false;

    // TODO: This slow down the end of the IPMI transaction waiting
    // for the commit to finish. commit<>() can take at least 5 seconds
    // to complete. 5s is very slow for an IPMI command and ends up
    // congesting the IPMI channel needlessly, especially if the watchdog
    // is ticking fairly quickly and we have some transient issues.
    // TODO：这会拖慢 IPMI 事务的结束，因为要等待
    //  commit 完成。commit<>() 至少需要 5 秒
    //  才能完成。对于一条 IPMI 命令来说 5 秒非常慢，最终会
    //  无谓地堵塞 IPMI 通道，尤其是在看门狗计时较快且
    //  出现一些瞬时问题时。
    commit<InternalFailure>();
}

ipmi::RspType<> ipmiAppResetWatchdogTimer()
{
    try
    {
        WatchdogService wdService;

        // Notify the caller if we haven't initialized our timer yet
        // so it can configure actions and timeouts
        // 如果尚未初始化定时器，则通知调用方，
        // 以便它配置动作和超时时间。
        if (!wdService.getInitialized())
        {
            lastCallSuccessful = true;

            return ipmi::responseWatchdogNotInit();
        }

        // The ipmi standard dictates we enable the watchdog during reset
        // IPMI 标准规定在复位期间启用看门狗
        wdService.resetTimeRemaining(true);
        lastCallSuccessful = true;
        return ipmi::responseSuccess();
    }
    catch (const InternalFailure& e)
    {
        reportError();
        return ipmi::responseUnspecifiedError();
    }
    catch (const std::exception& e)
    {
        lg2::error("wd_reset: {ERROR}", "ERROR", e);
        return ipmi::responseUnspecifiedError();
    }
    catch (...)
    {
        lg2::error("wd_reset: Unknown Error");
        return ipmi::responseUnspecifiedError();
    }
}

static constexpr uint8_t wdTimeoutActionMask = 0x3;

static constexpr uint8_t wdTimerUseResTimer1 = 0x0;
static constexpr uint8_t wdTimerUseResTimer2 = 0x6;
static constexpr uint8_t wdTimerUseResTimer3 = 0x7;

static constexpr uint8_t wdTimeoutActionMax = 3;
static constexpr uint8_t wdTimeoutInterruptTimer = 0x04;

enum class IpmiAction : uint8_t
{
    None = 0x0,
    HardReset = 0x1,
    PowerOff = 0x2,
    PowerCycle = 0x3,
};

/** @brief Converts an IPMI Watchdog Action to DBUS defined action
 *  @param[in] ipmiAction The IPMI Watchdog Action
 *  @return The Watchdog Action that the ipmiAction maps to
 */
/* @brief 将 IPMI Watchdog Action 转换为 DBUS 定义的 action
 *  @param[in] ipmiAction IPMI Watchdog Action
 *  @return ipmiAction 映射到的 Watchdog Action */
WatchdogService::Action ipmiActionToWdAction(IpmiAction ipmiAction)
{
    switch (ipmiAction)
    {
        case IpmiAction::None:
        {
            return WatchdogService::Action::None;
        }
        case IpmiAction::HardReset:
        {
            return WatchdogService::Action::HardReset;
        }
        case IpmiAction::PowerOff:
        {
            return WatchdogService::Action::PowerOff;
        }
        case IpmiAction::PowerCycle:
        {
            return WatchdogService::Action::PowerCycle;
        }
        default:
        {
            throw std::domain_error("IPMI Action is invalid");
        }
    }
}

enum class IpmiTimerUse : uint8_t
{
    Reserved = 0x0,
    BIOSFRB2 = 0x1,
    BIOSPOST = 0x2,
    OSLoad = 0x3,
    SMSOS = 0x4,
    OEM = 0x5,
};

WatchdogService::TimerUse ipmiTimerUseToWdTimerUse(IpmiTimerUse ipmiTimerUse)
{
    switch (ipmiTimerUse)
    {
        case IpmiTimerUse::Reserved:
        {
            return WatchdogService::TimerUse::Reserved;
        }
        case IpmiTimerUse::BIOSFRB2:
        {
            return WatchdogService::TimerUse::BIOSFRB2;
        }
        case IpmiTimerUse::BIOSPOST:
        {
            return WatchdogService::TimerUse::BIOSPOST;
        }
        case IpmiTimerUse::OSLoad:
        {
            return WatchdogService::TimerUse::OSLoad;
        }
        case IpmiTimerUse::SMSOS:
        {
            return WatchdogService::TimerUse::SMSOS;
        }
        case IpmiTimerUse::OEM:
        {
            return WatchdogService::TimerUse::OEM;
        }
        default:
        {
            return WatchdogService::TimerUse::Reserved;
        }
    }
}

static bool timerNotLogFlags = false;
static std::bitset<8> timerUseExpirationFlags = 0;
static uint3_t timerPreTimeoutInterrupt = 0;
static constexpr uint8_t wdExpirationFlagReservedBit0 = 0x0;
static constexpr uint8_t wdExpirationFlagReservedBit6 = 0x6;
static constexpr uint8_t wdExpirationFlagReservedBit7 = 0x7;

/**@brief The Set Watchdog Timer ipmi command.
 *
 * @param
 * - timerUse
 * - dontStopTimer
 * - dontLog
 * - timerAction
 * - pretimeout
 * - expireFlags
 * - initialCountdown
 *
 * @return completion code on success.
 **/
/* @brief Set Watchdog Timer ipmi 命令。
 *  @param
 *  - timerUse
 *  - dontStopTimer
 *  - dontLog
 *  - timerAction
 *  - pretimeout
 *  - expireFlags
 *  - initialCountdown
 *  @return 成功时返回完成码。 */
ipmi::RspType<> ipmiSetWatchdogTimer(
    uint3_t timerUse, uint3_t reserved, bool dontStopTimer, bool dontLog,
    uint3_t timeoutAction, uint1_t reserved1, uint3_t preTimeoutInterrupt,
    uint1_t reserved2, uint8_t preTimeoutInterval, std::bitset<8> expFlagValue,
    uint16_t initialCountdown)
{
    if ((timerUse == wdTimerUseResTimer1) ||
        (timerUse == wdTimerUseResTimer2) ||
        (timerUse == wdTimerUseResTimer3) ||
        (timeoutAction > wdTimeoutActionMax) ||
        (preTimeoutInterrupt == wdTimeoutInterruptTimer) ||
        (reserved | reserved1 | reserved2 |
         expFlagValue.test(wdExpirationFlagReservedBit0) |
         expFlagValue.test(wdExpirationFlagReservedBit6) |
         expFlagValue.test(wdExpirationFlagReservedBit7)))
    {
        return ipmi::responseInvalidFieldRequest();
    }

    if (preTimeoutInterval > (initialCountdown / 10))
    {
        return ipmi::responseInvalidFieldRequest();
    }

    timerNotLogFlags = dontLog;
    timerPreTimeoutInterrupt = preTimeoutInterrupt;

    try
    {
        WatchdogService wdService;
        // Stop the timer if the don't stop bit is not set
        // 如果 don't stop 位未置位，则停止定时器
        if (!(dontStopTimer))
        {
            wdService.setEnabled(false);
        }

        // Set the action based on the request
        // 根据请求设置动作
        const auto ipmiAction = static_cast<IpmiAction>(
            static_cast<uint8_t>(timeoutAction) & wdTimeoutActionMask);
        wdService.setExpireAction(ipmiActionToWdAction(ipmiAction));

        const auto ipmiTimerUse = types::enum_cast<IpmiTimerUse>(timerUse);
        wdService.setTimerUse(ipmiTimerUseToWdTimerUse(ipmiTimerUse));

        wdService.setExpiredTimerUse(WatchdogService::TimerUse::Reserved);

        timerUseExpirationFlags &= ~expFlagValue;

        // Set the new interval and the time remaining deci -> mill seconds
        // 设置新的时间间隔和剩余时间，由分秒（deci）转换为毫秒（mill）
        const uint64_t interval = initialCountdown * 100;
        wdService.setInterval(interval);
        wdService.resetTimeRemaining(false);

        // Mark as initialized so that future resets behave correctly
        // 标记为已初始化，以便后续的复位操作行为正确
        wdService.setInitialized(true);
        wdService.setLogTimeout(!dontLog);

        lastCallSuccessful = true;
        return ipmi::responseSuccess();
    }
    catch (const std::domain_error&)
    {
        return ipmi::responseInvalidFieldRequest();
    }
    catch (const InternalFailure& e)
    {
        reportError();
        return ipmi::responseUnspecifiedError();
    }
    catch (const std::exception& e)
    {
        lg2::error("wd_set: {ERROR}", "ERROR", e);
        return ipmi::responseUnspecifiedError();
    }
    catch (...)
    {
        lg2::error("wd_set: Unknown Error");
        return ipmi::responseUnspecifiedError();
    }
}

/** @brief Converts a DBUS Watchdog Action to IPMI defined action
 *  @param[in] wdAction The DBUS Watchdog Action
 *  @return The IpmiAction that the wdAction maps to
 */
/* @brief 将 DBUS Watchdog Action 转换为 IPMI 定义的 action
 *  @param[in] wdAction DBUS Watchdog Action
 *  @return wdAction 映射到的 IpmiAction */
IpmiAction wdActionToIpmiAction(WatchdogService::Action wdAction)
{
    switch (wdAction)
    {
        case WatchdogService::Action::None:
        {
            return IpmiAction::None;
        }
        case WatchdogService::Action::HardReset:
        {
            return IpmiAction::HardReset;
        }
        case WatchdogService::Action::PowerOff:
        {
            return IpmiAction::PowerOff;
        }
        case WatchdogService::Action::PowerCycle:
        {
            return IpmiAction::PowerCycle;
        }
        default:
        {
            // We have no method via IPMI to signal that the action is unknown
            // or unmappable in some way.
            // Just ignore the error and return NONE so the host can reconcile.
            // 我们无法通过 IPMI 发出动作未知
            // 或无法映射等信号。
            // 直接忽略该错误并返回 NONE，让主机自行协调。
            return IpmiAction::None;
        }
    }
}

IpmiTimerUse wdTimerUseToIpmiTimerUse(WatchdogService::TimerUse wdTimerUse)
{
    switch (wdTimerUse)
    {
        case WatchdogService::TimerUse::Reserved:
        {
            return IpmiTimerUse::Reserved;
        }
        case WatchdogService::TimerUse::BIOSFRB2:
        {
            return IpmiTimerUse::BIOSFRB2;
        }
        case WatchdogService::TimerUse::BIOSPOST:
        {
            return IpmiTimerUse::BIOSPOST;
        }
        case WatchdogService::TimerUse::OSLoad:
        {
            return IpmiTimerUse::OSLoad;
        }

        case WatchdogService::TimerUse::SMSOS:
        {
            return IpmiTimerUse::SMSOS;
        }
        case WatchdogService::TimerUse::OEM:
        {
            return IpmiTimerUse::OEM;
        }
        default:
        {
            return IpmiTimerUse::Reserved;
        }
    }
}

/**@brief The getWatchdogTimer ipmi command.
 *
 * @return Completion code plus timer details.
 * - timerUse
 * - timerAction
 * - pretimeout
 * - expireFlags
 * - initialCountdown
 * - presentCountdown
 **/
/* @brief getWatchdogTimer ipmi 命令。
 *  @return 完成码加上定时器详情。
 *  - timerUse
 *  - timerAction
 *  - pretimeout
 *  - expireFlags
 *  - initialCountdown
 *  - presentCountdown */
ipmi::RspType<uint3_t,        // timerUse - timer use
              uint3_t,        // timerUse - reserved
              bool,           // timerUse - timer is started
              bool,           // timerUse - don't log

              uint3_t,        // timerAction - timeout action
              uint1_t,        // timerAction - reserved
              uint3_t,        // timerAction - pre-timeout interrupt
              uint1_t,        // timerAction - reserved

              uint8_t,        // pretimeout
              std::bitset<8>, // expireFlags
              uint16_t,       // initial Countdown - Little Endian (deciseconds)
              uint16_t        // present Countdown - Little Endian (deciseconds)
              >
    ipmiGetWatchdogTimer()
{
    uint16_t presentCountdown = 0;
    uint8_t pretimeout = 0;

    try
    {
        WatchdogService wdService;
        WatchdogService::Properties wdProp = wdService.getProperties();

        // Build and return the response
        // Interval and timeRemaining need converted from milli -> deci seconds
        // 构建并返回响应。
        //  Interval 和 timeRemaining 需要从毫秒转换为分秒（milli -> deci）
        uint16_t initialCountdown = htole16(wdProp.interval / 100);

        if (wdProp.expiredTimerUse != WatchdogService::TimerUse::Reserved)
        {
            timerUseExpirationFlags.set(static_cast<uint8_t>(
                wdTimerUseToIpmiTimerUse(wdProp.expiredTimerUse)));
        }

        if (wdProp.enabled)
        {
            presentCountdown = htole16(wdProp.timeRemaining / 100);
        }
        else
        {
            if (wdProp.expiredTimerUse == WatchdogService::TimerUse::Reserved)
            {
                presentCountdown = initialCountdown;
            }
            else
            {
                presentCountdown = 0;
                // Automatically clear it whenever a timer expiration occurs.
                // 每当定时器到期时自动清除它。
                timerNotLogFlags = false;
            }
        }

        // TODO: Do something about having pretimeout support
        // TODO：处理预超时（pretimeout）支持的相关事宜
        pretimeout = 0;

        lastCallSuccessful = true;
        return ipmi::responseSuccess(
            types::enum_cast<uint3_t>(
                wdTimerUseToIpmiTimerUse(wdProp.timerUse)),
            0, wdProp.enabled, timerNotLogFlags,
            types::enum_cast<uint3_t>(
                wdActionToIpmiAction(wdProp.expireAction)),
            0, timerPreTimeoutInterrupt, 0, pretimeout, timerUseExpirationFlags,
            initialCountdown, presentCountdown);
    }
    catch (const InternalFailure& e)
    {
        reportError();
        return ipmi::responseUnspecifiedError();
    }
    catch (const std::exception& e)
    {
        lg2::error("wd_get: {ERROR}", "ERROR", e);
        return ipmi::responseUnspecifiedError();
    }
    catch (...)
    {
        lg2::error("wd_get: Unknown Error");
        return ipmi::responseUnspecifiedError();
    }
}
