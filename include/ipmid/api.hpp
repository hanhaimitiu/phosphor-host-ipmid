
/**
 * @file api.hpp
 * @brief C++ 插件 API 的封装（基于 api.h）。
 */
/*  @file api.hpp
 *  @brief C++ 插件 API 的封装（基于 api.h）。 */
/*
 * Copyright © 2018 Intel Corporation
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
 *
 */
/*  版权所有 (c) 2018 Intel Corporation
 *  根据 Apache 许可证 2.0 版（"License"）授权；
 *  除非符合许可证的规定，否则不得使用本文件。
 *  你可以从以下地址获取许可证副本：
 *       http://www.apache.org/licenses/LICENSE-2.0
 *  除非适用法律要求或书面同意，否则本软件
 *  按"原样"分发，
 *  不提供任何明示或暗示的保证或条件。
 *  有关许可证下的具体权限和限制，请参阅许可证
 *  本身。 */
#pragma once

#define ALLOW_DEPRECATED_API 1
// make it possible to ONLY include api.hpp during the transition
// 在过渡期间可以只 include api.hpp
#ifdef ALLOW_DEPRECATED_API
#include <ipmid/api.h>
#endif

#include <boost/asio/io_context.hpp>
#include <ipmid/api-types.hpp>
#include <ipmid/filter.hpp>
#include <ipmid/handler.hpp>
#include <ipmid/message/types.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>

// any client can interact with the main asio context
// 任何客户端都可以与主 asio context 交互
std::shared_ptr<boost::asio::io_context> getIoContext();

// any client can interact with the main sdbus
// 任何客户端都可以与主 sdbus 交互
std::shared_ptr<sdbusplus::asio::connection> getSdBus();

/**
 * @brief post some work to the async execution queue
 *
 * The IPMI daemon runs an async execution queue; this allows any function to
 * pass in work to be executed in that context
 *
 * @tparam WorkFn - a function of type void(void)
 * @param work - the callback function to be executed
 */
/* @brief 把工作投递到异步执行队列
 * IPMI 守护进程运行一个异步执行队列；这允许任何函数
 * 把工作传入该上下文中执行
 * @tparam WorkFn - 类型为 void(void) 的函数
 * @param work - 要执行的回调函数 */
template <typename WorkFn>
static inline void post_work(WorkFn work)
{
    boost::asio::post(*getIoContext(), std::forward<WorkFn>(work));
}

enum class SignalResponse : int
{
    breakExecution,
    continueExecution,
};

/**
 * @brief add a signal handler
 *
 * This registers a handler to be called asynchronously via the execution
 * queue when the specified signal is received.
 *
 * Priority allows a signal handler to specify what order in the handler
 * chain it gets called. Lower priority numbers will cause the handler to
 * be executed later in the chain, while the highest priority numbers will cause
 * the handler to be executed first.
 *
 * In order to facilitate a chain of handlers, each handler in the chain will be
 * able to return breakExecution or continueExecution. Returning breakExecution
 * will break the chain and no further handlers will execute for that signal.
 * Returning continueExecution will allow lower-priority handlers to execute.
 *
 * By default, the main asio execution loop will register a low priority
 * (prioOpenBmcBase) handler for SIGINT and SIGTERM to cause the process to stop
 * on either of those signals. To prevent one of those signals from causing the
 * process to stop, simply register a higher priority handler that returns
 * breakExecution.
 *
 * @param int - priority of handler
 * @param int - signal number to wait for
 * @param handler - the callback function to be executed
 */
/* @brief 添加信号 handler
 * 这注册一个 handler，当收到指定信号时，
 * 通过执行队列异步调用它。
 * 优先级让信号 handler 可以指定它在
 * handler 链中的调用顺序。较低的优先级
 * 数值会使 handler 在链中较晚执行，而最高的
 * 优先级数值会使 handler 最先执行。
 * 为了便于形成 handler 链，链中的每个 handler
 * 都能返回 breakExecution 或 continueExecution。
 * 返回 breakExecution 将中断链，该信号不会再执行
 * 后续的 handler。返回 continueExecution 则允许
 * 较低优先级的 handler 执行。
 * 默认情况下，主 asio 执行循环会为 SIGINT 和
 * SIGTERM 注册一个低优先级（prioOpenBmcBase）handler，
 * 使进程在这些信号之一到来时停止。要防止某个信号
 * 导致进程停止，只需注册一个返回 breakExecution 的
 * 更高优先级 handler。
 * @param int - handler 的优先级；@param int - 要等待的信号编号
 * @param handler - 要执行的回调函数 */
void registerSignalHandler(int priority, int signalNumber,
                           const std::function<SignalResponse(int)>& handler);
