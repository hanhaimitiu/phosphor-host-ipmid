
/**
 * @file filter.hpp
 * @brief IPMI 命令过滤器的注册接口。
 */
/*  @file filter.hpp
 *  @brief IPMI 命令过滤器的注册接口。 */
/**
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
#include <boost/callable_traits.hpp>
#include <ipmid/api-types.hpp>
#include <ipmid/message.hpp>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <tuple>
#include <utility>

namespace ipmi
{

using FilterFunction = ipmi::Cc(ipmi::message::Request::ptr);

/**
 * @brief Filter base class for dealing with IPMI request/response
 *
 * The subclasses are all templated so they can provide access to any type of
 * command callback functions.
 */
/* @brief 用于处理 IPMI 请求/响应的过滤器基类
 * 所有子类都是模板化的，因此它们可以提供对任意类型
 * 命令回调函数的访问。 */
class FilterBase
{
  public:
    using ptr = std::shared_ptr<FilterBase>;

    virtual ~FilterBase() = default;

    virtual ipmi::Cc call(message::Request::ptr request) = 0;
};

/**
 * @brief filter concrete class
 *
 * This is the base template that ipmi filters will resolve into. This is
 * essentially just a wrapper to hold the filter callback so it can be stored in
 * the filter list.
 *
 * Filters are called with a ipmi::message::Request shared_ptr on all IPMI
 * commands in priority order and each filter has the opportunity to reject the
 * command (by returning an IPMI error completion code.) If all the filters
 * return success, the actual IPMI command will be executed. Filters can reject
 * the command for any reason, based on system state, the context, the command
 * payload, etc.
 */
/* @brief filter 具体类
 * 这是 ipmi 过滤器最终解析成的基模板。
 * 它本质上只是一个包装器，用于保存过滤器回调，
 * 以便将其存入过滤器列表。
 * 过滤器会以 ipmi::message::Request shared_ptr
 * 按优先级顺序在所有 IPMI 命令上被调用，每个过滤器
 * 都有机会拒绝该命令（通过返回 IPMI 错误完成码）。
 * 如果所有过滤器都返回成功，则执行实际的 IPMI 命令。
 * 过滤器可以基于系统状态、上下文、
 * 命令负载等任何原因拒绝命令。 */
template <typename Filter>
class IpmiFilter : public FilterBase
{
  public:
    IpmiFilter(Filter&& filter) : filter_(std::move(filter)) {}

    ipmi::Cc call(message::Request::ptr request) override
    {
        return filter_(request);
    }

  private:
    Filter filter_;
};

/**
 * @brief helper function to construct a filter object
 *
 * This is called internally by the ipmi::registerFilter function.
 */
/* @brief 构造过滤器对象的辅助函数
 * 它由 ipmi::registerFilter 函数在内部调用。 */
template <typename Filter>
static inline auto makeFilter(Filter&& filter)
{
    FilterBase::ptr ptr(new IpmiFilter<Filter>(std::forward<Filter>(filter)));
    return ptr;
}
template <typename Filter>
static inline auto makeFilter(const Filter& filter)
{
    Filter lFilter = filter;
    return makeFilter(std::forward<Filter>(lFilter));
}

namespace impl
{

// IPMI command filter registration implementation
// IPMI 命令过滤器注册实现
void registerFilter(int prio, ::ipmi::FilterBase::ptr filter);

} // namespace impl

/**
 * @brief IPMI command filter registration function
 *
 * This function should be used to register IPMI command filter functions.
 * This function just passes the callback to makeFilter, which creates a
 * wrapper functor object that ultimately calls the callback.
 *
 * Filters are called with a ipmi::message::Request shared_ptr on all IPMI
 * commands in priority order and each filter has the opportunity to reject the
 * command (by returning an IPMI error completion code.) If all the filters
 * return success, the actual IPMI command will be executed. Filters can reject
 * the command for any reason, based on system state, the context, the command
 * payload, etc.
 *
 * @param prio - priority at which to register; see api.hpp
 * @param filter - the callback function that will handle this request
 *
 * @return bool - success of registering the handler
 */
/* @brief IPMI 命令过滤器注册函数
 * 该函数用于注册 IPMI 命令过滤器函数。
 * 它只是把回调传给 makeFilter，后者创建一个包装函子对象，
 * 最终由它调用该回调。
 * 过滤器会以 ipmi::message::Request shared_ptr
 * 按优先级顺序在所有 IPMI 命令上被调用，每个过滤器
 * 都有机会拒绝该命令（通过返回 IPMI 错误完成码）。
 * 如果所有过滤器都返回成功，则执行实际的 IPMI 命令。
 * 过滤器可以基于系统状态、上下文、
 * 命令负载等任何原因拒绝命令。
 * @param prio - 注册优先级；参见 api.hpp
 * @param filter - 将处理此请求的回调函数
 * @return bool - handler 注册是否成功 */
template <typename Filter>
void registerFilter(int prio, Filter&& filter)
{
    auto f = ipmi::makeFilter(std::forward<Filter>(filter));
    impl::registerFilter(prio, f);
}

template <typename Filter>
void registerFilter(int prio, const Filter& filter)
{
    auto f = ipmi::makeFilter(filter);
    impl::registerFilter(prio, f);
}

} // namespace ipmi
