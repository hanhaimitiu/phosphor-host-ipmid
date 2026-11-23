#pragma once

/**
 * @file watchdog_service.hpp
 * @brief 看门狗服务 D-Bus 封装的声明。
 */
/*  @file watchdog_service.hpp
 *  @brief 看门狗服务 D-Bus 封装的声明。 */
#include <ipmid/utils.hpp>
#include <sdbusplus/bus.hpp>
#include <xyz/openbmc_project/State/Watchdog/server.hpp>

/** @class WatchdogService
 *  @brief Access to the running OpenBMC watchdog implementation.
 *  @details Easy accessor for servers that implement the
 *  xyz.openbmc_project.State.Watchdog DBus API.
 */
/* @class WatchdogService
 *  @brief 访问正在运行的 OpenBMC 看门狗实现。
 *  @details 为实现了 xyz.openbmc_project.State.Watchdog DBus API
 *  的服务提供便捷的访问器。 */
class WatchdogService
{
  public:
    WatchdogService();

    using Action =
        sdbusplus::server::xyz::openbmc_project::state::Watchdog::Action;
    using TimerUse =
        sdbusplus::server::xyz::openbmc_project::state::Watchdog::TimerUse;

    /** @brief Resets the time remaining on the watchdog.
     *         Equivalent to setTimeRemaining(getInterval()).
     *         Optionally enables the watchdog.
     *
     *  @param[in] enableWatchdog - Should the call also enable the watchdog
     */
    /* @brief 重置看门狗的剩余时间。
     *         等同于 setTimeRemaining(getInterval())。
     *         还可以选择启用看门狗。
     *  @param[in] enableWatchdog - 本次调用是否同时启用看门狗 */
    void resetTimeRemaining(bool enableWatchdog);

    /** @brief Contains a copy of the properties enumerated by the
     *         watchdog service.
     */
    /* @brief 包含看门狗服务所列举
     *        属性的副本。 */
    struct Properties
    {
        bool initialized;
        bool enabled;
        Action expireAction;
        TimerUse timerUse;
        TimerUse expiredTimerUse;
        uint64_t interval;
        uint64_t timeRemaining;
    };

    /** @brief Retrieves a copy of the currently set properties on the
     *         host watchdog
     *
     *  @return A populated WatchdogProperties struct
     */
    /* @brief 获取主机看门狗上当前设置的
     * 属性的副本。
     * @return 填充好的 WatchdogProperties 结构体。 */
    Properties getProperties();

    /** @brief Get the value of the initialized property on the host
     *         watchdog
     *
     *  @return The value of the property
     */
    /* @brief 获取主机看门狗上 initialized 属性的
     * 值。
     * @return 该属性的值。 */
    bool getInitialized();

    /** @brief Sets the value of the initialized property on the host
     *         watchdog
     *
     *  @param[in] initialized - The new initializedvalue
     */
    /* @brief 设置主机看门狗上 initialized 属性的
     * 值。
     * @param[in] initialized - 新的 initialized 值。 */
    void setInitialized(bool initialized);

    /** @brief Sets the value of the enabled property on the host watchdog
     *
     *  @param[in] enabled - The new enabled value
     */
    /* @brief 设置主机看门狗上 enabled 属性的值。
     * @param[in] enabled - 新的 enabled 值。 */
    void setEnabled(bool enabled);

    /** @brief Sets the value of the LogTimeout property on the host watchdog
     *
     *  @param[in] LogTimeout - The new LogTimeout value
     */
    /* @brief 设置主机看门狗上 LogTimeout 属性的值。
     * @param[in] LogTimeout - 新的 LogTimeout 值。 */
    void setLogTimeout(bool LogTimeout);

    /** @brief Sets the value of the expireAction property on the host watchdog
     *
     *  @param[in] expireAction - The new expireAction value
     */
    /* @brief 设置主机看门狗上 expireAction 属性的值。
     * @param[in] expireAction - 新的 expireAction 值。 */
    void setExpireAction(Action expireAction);

    /** @brief Sets the value of the timerUse property on the host watchdog
     *
     *  @param[in] timerUse - The new timerUse value
     */
    /* @brief 设置主机看门狗上 timerUse 属性的值。
     * @param[in] timerUse - 新的 timerUse 值。 */
    void setTimerUse(TimerUse timerUse);

    /** @brief Sets the value of the ExpiredTimerUse property on the host
     * watchdog
     *
     *  @param[in] timerUse - The new timerUse value
     */
    /* @brief 设置主机看门狗上 ExpiredTimerUse 属性的
     * 值。
     * @param[in] timerUse - 新的 timerUse 值。 */
    void setExpiredTimerUse(TimerUse timerUse);

    /** @brief Sets the value of the interval property on the host watchdog
     *
     *  @param[in] interval - The new interval value
     */
    /* @brief 设置主机看门狗上 interval 属性的值。
     * @param[in] interval - 新的 interval 值。 */
    void setInterval(uint64_t interval);

  private:
    /** @brief sdbusplus handle */
    /* @brief sdbusplus 句柄 */
    sdbusplus::bus_t bus;
    /** @brief The name of the mapped host watchdog service */
    /* @brief 映射到的主机看门狗服务的名称 */
    static ipmi::ServiceCache wdService;

    /** @brief Gets the value of the property on the host watchdog
     *
     *  @param[in] key - The name of the property
     *  @return The value of the property
     */
    /* @brief 获取主机看门狗上属性的值。
     * @param[in] key - 属性的名称。
     * @return 该属性的值。 */
    template <typename T>
    T getProperty(const std::string& key);

    /** @brief Sets the value of the property on the host watchdog
     *
     *  @param[in] key - The name of the property
     *  @param[in] val - The new value
     */
    /* @brief 设置主机看门狗上属性的值。
     * @param[in] key - 属性的名称。
     * @param[in] val - 新值。 */
    template <typename T>
    void setProperty(const std::string& key, const T& val);
};
