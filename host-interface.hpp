#pragma once

/**
 * @file host-interface.hpp
 * @brief org.openbmc.HostIpmi 接口的声明。
 */
/* @file host-interface.hpp
 * @brief org.openbmc.HostIpmi 接口的声明。 */

#include <host-cmd-manager.hpp>
#include <sdbusplus/bus.hpp>
#include <xyz/openbmc_project/Condition/HostFirmware/server.hpp>
#include <xyz/openbmc_project/Control/Host/server.hpp>
namespace phosphor
{
namespace host
{
namespace command
{

/** @class Host
 *  @brief OpenBMC control and condition host interface implementation.
 *  @details A concrete implementation for xyz.openbmc_project.Control.Host
 *  and xyz.openbmc_project.Condition.HostFirmware DBus API's.
 */
/* @class Host
 *  @brief OpenBMC 主机控制与状态接口的实现。
 *  @details 是 xyz.openbmc_project.Control.Host 与
 *  xyz.openbmc_project.Condition.HostFirmware DBus API 的具体实现。 */
class Host :
    public sdbusplus::server::object_t<
        sdbusplus::server::xyz::openbmc_project::control::Host,
        sdbusplus::server::xyz::openbmc_project::condition::HostFirmware>
{
  public:
    /** @brief Constructs Host Control and Condition Interfaces
     *
     *  @param[in] bus     - The Dbus bus object
     *  @param[in] objPath - The Dbus object path
     */
    /* @brief 构造 Host Control 与 Condition 接口
     *  @param[in] bus     - Dbus 总线对象
     *  @param[in] objPath - Dbus 对象路径 */
    Host(sdbusplus::bus_t& bus, const char* objPath) :
        sdbusplus::server::object_t<
            sdbusplus::server::xyz::openbmc_project::control::Host,
            sdbusplus::server::xyz::openbmc_project::condition::HostFirmware>(
            bus, objPath)
    {
        // Nothing to do
        // 无需处理
    }

    /** @brief Send input command to host
     *         Note that the command will be queued in a FIFO if
     *         other commands to the host have yet to be run
     *
     *  @param[in] command - Input command to execute
     */
    /* @brief 将输入命令发送给主机
     *         注意，若仍有其他发往主机的命令尚未执行，
     *         该命令将在 FIFO 队列中排队。
     *  @param[in] command - 要执行的输入命令 */
    void execute(Command command) override;

    /** @brief Override reads to CurrentFirmwareCondition */
    /* @brief 覆写对 CurrentFirmwareCondition 的读取 */
    FirmwareCondition currentFirmwareCondition() const override;

  private:
    /** @brief  Callback function to be invoked by command manager
     *
     *  @detail Conveys the status of the last Host bound command.
     *          Depending on the status,  a CommandComplete or
     *          CommandFailure signal would be sent
     *
     *  @param[in] cmd    - IPMI command and data sent to Host
     *  @param[in] status - Success or Failure
     */
    /* @brief  由命令管理器调用的回调函数
     *  @detail 传达上一条绑定到 Host 的命令的状态。
     *          根据该状态，将发送 CommandComplete 或
     *          CommandFailure 信号。
     *  @param[in] cmd    - 发送给 Host 的 IPMI 命令和数据
     *  @param[in] status - 成功或失败 */
    void commandStatusHandler(IpmiCmdData cmd, bool status);
};

} // namespace command
} // namespace host
} // namespace phosphor
