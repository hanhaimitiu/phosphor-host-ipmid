#pragma once

/**
 * @file host-cmd-manager.hpp
 * @brief host 命令队列管理的声明。
 */
/* @file host-cmd-manager.hpp
 * @brief host 命令队列管理的声明。 */

#include <ipmid-host/cmd-utils.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/timer.hpp>

#include <queue>
#include <tuple>

namespace phosphor
{
namespace host
{
namespace command
{

/** @class
 *  @brief Manages commands that are to be sent to Host
 */
/* @class
 *  @brief 管理要发送给 Host 的命令 */
class Manager
{
  public:
    Manager() = delete;
    ~Manager() = default;
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;
    Manager(Manager&&) = delete;
    Manager& operator=(Manager&&) = delete;

    /** @brief Constructs Manager object
     *
     *  @param[in] bus   - dbus handler
     *  @param[in] event - pointer to sd_event
     */
    /* @brief 构造 Manager 对象
     *  @param[in] bus   - dbus handler
     *  @param[in] event - 指向 sd_event 的指针 */
    explicit Manager(sdbusplus::bus_t& bus);

    /** @brief  Extracts the next entry in the queue and returns
     *          Command and data part of it.
     *
     *  @detail Also calls into the registered handlers so that they can now
     *          send the CommandComplete signal since the interface contract
     *          is that we emit this signal once the message has been
     *          passed to the host (which is required when calling this)
     *
     *          Also, if the queue has more commands, then it will alert the
     *          host
     */
    /* @brief  取出队列中的下一条目，并返回
     *          其中的命令和数据部分。
     *  @detail 同时调用已注册的处理程序，以便它们
     *          现在可以发送 CommandComplete 信号，因为接口约定
     *          是：一旦消息已传递给主机（调用本函数即要求如此），
     *          我们就发出该信号。
     *          此外，若队列中还有更多命令，则会通知
     *          主机。 */
    IpmiCmdData getNextCommand();

    /** @brief  Pushes the command onto the queue.
     *
     *  @detail If the queue is empty, then it alerts the Host. If not,
     *          then it returns and the API documented above will handle
     *          the commands in Queue.
     *
     *  @param[in] command - tuple of <IPMI command, data, callback>
     */
    /* @brief  将命令压入队列。
     *  @detail 若队列为空，则通知 Host。否则
     *          直接返回，由上文记录的 API 处理
     *          队列中的命令。
     *  @param[in] command - <IPMI 命令, 数据, 回调> 的元组 */
    void execute(CommandHandler command);

  private:
    /** @brief Check if anything in queue and alert host if so */
    /* @brief 检查队列中是否有内容，若有则通知主机 */
    void checkQueueAndAlertHost();

    /** @brief  Call back interface on message timeouts to host.
     *
     *  @detail When this happens, a failure message would be sent
     *          to all the commands that are in the Queue and queue
     *          will be purged
     */
    /* @brief  向主机发送消息超时时的回调接口。
     *  @detail 发生这种情况时，会向队列中的所有
     *          命令发送失败消息，并清除
     *          队列。 */
    void hostTimeout();

    /** @brief Clears the command queue
     *
     *  @detail Clears the command queue and calls all callbacks
     *          specifying the command wasn't successful.
     */
    /* @brief 清空命令队列
     *  @detail 清空命令队列并调用所有回调，
     *          指明命令未成功执行。 */
    void clearQueue();

    /** @brief Clears the command queue on a power on
     *
     *  @detail The properties changed handler for the
     *          RequestedHostTransition property.  When this property
     *          changes to 'On', this function will purge the command
     *          queue.
     *
     *          This is done to avoid having commands that were issued
     *          before the host powers on from getting sent to the host,
     *          either due to race conditions around state transitions
     *          or from a user doing something like requesting an already
     *          powered off system to power off again and then immediately
     *          requesting a power on.
     *
     *  @param[in] msg - the sdbusplus message containing the property
     */
    /* @brief 在上电时清空命令队列
     *  @detail RequestedHostTransition 属性的
     *          属性变更处理器。当该属性
     *          变为 'On' 时，本函数将清除命令
     *          队列。
     *          这样做是为了避免主机上电前发出的命令
     *          被发送给主机，无论是由于状态转换时的
     *          竞态条件，还是由于用户做出如下操作：
     *          请求一个已经断电的系统再次断电，
     *          然后立即
     *          请求上电。
     *  @param[in] msg - 包含该属性的 sdbusplus 消息 */
    void clearQueueOnPowerOn(sdbusplus::message_t& msg);

    /** @brief Reference to the dbus handler */
    /* @brief 对 dbus handler 的引用 */
    sdbusplus::bus_t& bus;

    /** @brief Queue to store the requested commands */
    /* @brief 用于存储所请求命令的队列 */
    std::queue<CommandHandler> workQueue{};

    /** @brief Timer for commands to host */
    /* @brief 发往主机命令的定时器 */
    sdbusplus::Timer timer;

    /** @brief Match handler for the requested host state */
    /* @brief 所请求主机状态的 match 处理器 */
    sdbusplus::match hostTransitionMatch;
};

} // namespace command
} // namespace host
} // namespace phosphor
