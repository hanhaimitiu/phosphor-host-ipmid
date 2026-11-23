#pragma once

/**
 * @file softoff.hpp
 * @brief 软关机服务的声明。
 */
/*  @file softoff.hpp
 *  @brief 软关机服务的声明。 */

#include "config.h"

#include <sdbusplus/bus.hpp>
#include <sdbusplus/server/object.hpp>
#include <sdbusplus/timer.hpp>
#include <xyz/openbmc_project/Control/Host/server.hpp>
#include <xyz/openbmc_project/Ipmi/Internal/SoftPowerOff/server.hpp>

#include <functional>
namespace phosphor
{
namespace ipmi
{

namespace Base = sdbusplus::server::xyz::openbmc_project::ipmi::internal;
using namespace sdbusplus::server::xyz::openbmc_project::control;

namespace sdbusRule = sdbusplus::match_rules;

namespace
{
using SoftPowerOffInherit = sdbusplus::server::object_t<Base::SoftPowerOff>;
}

/** @class SoftPowerOff
 *  @brief Responsible for coordinating Host SoftPowerOff operation
 */
/* @class SoftPowerOff
 *   @brief 负责协调 Host 的 SoftPowerOff 操作 */
class SoftPowerOff : public SoftPowerOffInherit
{
  public:
    /** @brief Constructs SoftPowerOff object.
     *
     *  @param[in] bus       - system dbus handler
     *  @param[in] event     - sd_event handler
     *  @param[in] objPath   - The Dbus path hosting SoftPowerOff function
     */
    /* @brief 构造 SoftPowerOff 对象。
     *   @param[in] bus       - system dbus 句柄
     *   @param[in] event     - sd_event 句柄
     *   @param[in] objPath   - 承载 SoftPowerOff 功能的 Dbus 路径 */
    SoftPowerOff(sdbusplus::bus_t& bus, sd_event* event, const char* objPath) :
        SoftPowerOffInherit(bus, objPath,
                            SoftPowerOffInherit::action::defer_emit),
        bus(bus), timer(event),
        hostControlSignal(
            bus,
            sdbusRule::type::signal() + sdbusRule::member("CommandComplete") +
                sdbusRule::path("/xyz/openbmc_project/control/host0") +
                sdbusRule::interface(CONTROL_HOST_BUSNAME) +
                sdbusRule::argN(0, convertForMessage(Host::Command::SoftOff)),
            std::bind(std::mem_fn(&SoftPowerOff::hostControlEvent), this,
                      std::placeholders::_1))
    {
        // Need to announce since we may get the response
        // very quickly on host shutdown command
        // 需要先行宣告，因为 host 关机命令的响应
        // 可能来得非常快
        emit_object_added();

        // The whole purpose of this application is to send a host shutdown
        // command and watch for the soft power off to go through. We need
        // the interface added signal emitted before we send the shutdown
        // command just to attend to lightning fast response from host
        // 本应用的全部目的就是发送 host 关机命令
        // 并观察软关机是否完成。我们需要在发送关机命令
        // 之前发出 interface added 信号，以应对 host
        // 闪电般的快速响应。
        sendHostShutDownCmd();
    }

    /** @brief Tells if the objective of this application is completed */
    /* @brief 判断本应用的目标是否已完成 */
    inline auto isCompleted()
    {
        return completed;
    }

    /** @brief Tells if the referenced timer is expired or not */
    /* @brief 判断所引用的定时器是否已到期 */
    inline auto isTimerExpired()
    {
        return timer.isExpired();
    }

    /** @brief overloaded property setter function
     *
     *  @param[in] value - One of SoftOffReceived / HostShutdown
     *
     *  @return Success or exception thrown
     */
    /* @brief 重载的属性设置函数
     *   @param[in] value - SoftOffReceived / HostShutdown 之一
     *   @return 成功或抛出异常 */
    HostResponse responseReceived(HostResponse value) override;

    /** @brief Using the base class's getter method */
    /* @brief 使用基类的 getter 方法 */
    using Base::SoftPowerOff::responseReceived;

    /** @brief Calls to start a timer
     *
     *  @param[in] usec - Time in microseconds
     *
     *  @return Success or exception thrown
     */
    /* @brief 调用以启动定时器
     *   @param[in] usec - 时间，单位微秒
     *   @return 成功或抛出异常 */
    int startTimer(const std::chrono::microseconds& usec);

  private:
    // Need this to send SMS_ATTN
    // TODO : Switch over to using mapper service in a different patch
    // 需要它来发送 SMS_ATTN
    // TODO：在另一个补丁中改用 mapper service
    static constexpr auto HOST_IPMI_BUS = "org.openbmc.HostIpmi";
    static constexpr auto HOST_IPMI_OBJ = "/org/openbmc/HostIpmi/1";
    static constexpr auto HOST_IPMI_INTF = "org.openbmc.HostIpmi";

    /* @brief sdbusplus handle */
    /* @brief sdbusplus 句柄 */
    sdbusplus::bus_t& bus;

    /** @brief Reference to Timer object */
    /* @brief 对 Timer 对象的引用 */
    sdbusplus::Timer timer;

    /** @brief Marks the end of life of this application.
     *
     *  This is set to true if host gives appropriate responses
     *  for the sequence of commands.
     */
    /* @brief 标记本应用生命周期的结束。
     *  当 host 对这一系列命令给出恰当的响应时，
     *  该值会被置为 true。 */
    bool completed = false;

    /** @brief Subscribe to host control signals
     *
     *  Protocol is to send the host power off request to the host
     *  control interface and then wait for a signal indicating pass/fail
     **/
    /* @brief 订阅 host 控制信号
     *  协议是向 host control 接口发送 host 下电请求，
     *  然后等待一个指示成功/失败的信号。 */
    sdbusplus::match hostControlSignal;

    /** @brief Sends host control command to tell host to shut down
     *
     *  After sending the command, wait for a signal indicating the status
     *  of the command.
     *
     *  After receiving the initial response, start a timer for 30 minutes
     *  to let host do a clean shutdown of partitions. When the response is
     *  received from the host, it indicates that BMC can do a power off.
     *  If BMC fails to get any response, then a hard power off would
     *  be forced.
     *
     *  @return - Does not return anything. Error will result in exception
     *            being thrown
     */
    /* @brief 发送 host 控制命令，通知 host 关机
     *  发送命令后，等待指示该命令状态的
     *  信号。
     *  收到初始响应后，启动一个 30 分钟的定时器，
     *  让 host 干净地关闭各分区。当收到来自 host 的
     *  响应时，表明 BMC 可以执行下电。
     *  如果 BMC 未能收到任何响应，则将强制
     *  执行硬下电。
     *  @return - 不返回任何内容。出错时会导致
     *           抛出异常 */
    void sendHostShutDownCmd();

    /** @brief Callback function on host control signals
     *
     * @param[in]  msg       - Data associated with subscribed signal
     *
     */
    /* @brief host 控制信号上的回调函数
     *  @param[in]  msg       - 与所订阅信号关联的数据 */
    void hostControlEvent(sdbusplus::message_t& msg);
};
} // namespace ipmi
} // namespace phosphor
