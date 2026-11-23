
/**
 * @file handler.hpp
 * @brief IPMI handler 的泛型注册模板（自动打包/解包参数）。
 */
/*  @file handler.hpp
 *  @brief IPMI handler 的泛型注册模板（自动打包/解包参数）。 */
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
#include <cxxabi.h>

#include <boost/asio/spawn.hpp>
#include <boost/callable_traits.hpp>
#include <ipmid/api-types.hpp>
#include <ipmid/message.hpp>
#include <phosphor-logging/lg2.hpp>
#include <phosphor-logging/log.hpp>
#include <user_channel/channel_layer.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>

#ifdef ALLOW_DEPRECATED_API
#include <ipmid/api.h>

#include <ipmid/oemrouter.hpp>
#endif /* ALLOW_DEPRECATED_API */

namespace ipmi
{

template <typename... Args>
static inline message::Response::ptr errorResponse(
    message::Request::ptr request, ipmi::Cc cc, Args&&... args)
{
    message::Response::ptr response = request->makeResponse();
    response->cc = cc;
    response->pack(args...);
    return response;
}
static inline message::Response::ptr errorResponse(
    message::Request::ptr request, ipmi::Cc cc)
{
    message::Response::ptr response = request->makeResponse();
    response->cc = cc;
    return response;
}

/** @brief 异常扩展：允许附带 IPMI 返回码抛出 */
class HandlerCompletion
{
  public:
    HandlerCompletion(Cc cc) noexcept : cc(cc) {}

    Cc code() const noexcept
    {
        return cc;
    }

  private:
    Cc cc;
};

/** @brief 异常扩展：附带 IPMI 返回码并输出一条日志错误 */
class HandlerException : public HandlerCompletion, public std::runtime_error
{
  public:
    HandlerException(Cc cc, const char* what) :
        HandlerCompletion(cc), std::runtime_error(what)
    {}
    HandlerException(Cc cc, const std::string& what) :
        HandlerException(cc, what.c_str())
    {}
};

// 取当前异常的类型名（demangle 后），用于日志
static inline const char* currentExceptionType()
{
    int status;
    return abi::__cxa_demangle(abi::__cxa_current_exception_type()->name(),
                               nullptr, nullptr, &status);
}

/**
 * @brief 处理 IPMI 请求/应答的 handler 基类
 *
 * 子类全部为模板，从而能适配各种签名形式的命令回调函数。
 */
class HandlerBase
{
  public:
    using ptr = std::shared_ptr<HandlerBase>;

    virtual ~HandlerBase() = default;

    /** @brief 用请求对象包装对已注册 handler 的调用
     *
     * 在运行队列上下文中调用：此时 request 对象已经创建，
     * 携带执行 IPMI 命令所需的全部信息。返回值是应答对象指针，
     * 它持有的应答最终会被发回请求方。
     *
     * 这是对虚函数 executeCallback 的非虚包装——实际工作由后者完成。
     * 之所以这样分层，是由模板与虚函数的协作方式决定的。
     *
     * @param request 指向 Request 对象的 shared_ptr
     *
     * @return 指向 Response 对象的 shared_ptr
     */
    message::Response::ptr call(message::Request::ptr request)
    {
        return executeCallback(request);
    }

  private:
    /** @brief 用请求对象调用已注册的 handler
     *
     * 在运行队列上下文中调用；此时 request 已包含执行 IPMI 命令
     * 所需的全部信息。返回的应答对象最终发回请求方。
     *
     * @param request 指向 Request 对象的 shared_ptr
     *
     * @return 指向 Response 对象的 shared_ptr
     */
    virtual message::Response::ptr executeCallback(
        message::Request::ptr request) = 0;
};

/**
 * @brief 主 IPMI handler 类
 *
 * 新式 IPMI handler 都会落到这个类：它读取注册函数的签名，
 * 从请求中提取匹配的参数，调用函数，再把函数的返回值
 * 打包回 IPMI 应答。
 */
template <typename Handler>
class IpmiHandler final : public HandlerBase
{
  public:
    explicit IpmiHandler(Handler&& handler) :
        handler_(std::forward<Handler>(handler))
    {}

  private:
    Handler handler_;

    /** @brief 用请求对象调用已注册的 handler
     *
     * 在运行队列上下文中调用；此时 request 已包含执行 IPMI 命令
     * 所需的全部信息。返回的应答对象最终发回请求方。
     *
     * 作为新式 IPMI handler，本函数负责从请求中提取参数传给回调，
     * 并把返回值打包成普通 vector 交回调用者。
     *
     * @param request 指向 Request 对象的 shared_ptr
     *
     * @return 指向 Response 对象的 shared_ptr
     */
    message::Response::ptr executeCallback(
        message::Request::ptr request) override
    {
        message::Response::ptr response = request->makeResponse();

        using CallbackSig = boost::callable_traits::args_t<Handler>;
        using InputArgsType = typename utility::DecayTuple<CallbackSig>::type;
        using UnpackArgsType = typename utility::StripFirstArgs<
            utility::NonIpmiArgsCount<InputArgsType>::size(),
            InputArgsType>::type;
        using ResultType = boost::callable_traits::return_type_t<Handler>;

        UnpackArgsType unpackArgs{};
        request->payload.trailingOk = false;
        ipmi::Cc unpackError = request->unpack(unpackArgs);
        if (unpackError != ipmi::ccSuccess)
        {
            response->cc = unpackError;
            return response;
        }
        /* 回调的首参可以是以下可选项之一：
         * 1) boost::asio::yield_context（协程上下文）
         * 2) ipmi::Context::ptr（命令上下文）
         * 3) ipmi::message::Request::ptr（原始请求）
         *
         * 若回调签名的首参是上述类型之一，这里会自动把它
         * 填进参数包。
         *
         * 另一个特殊的可选参数是 ipmi::message::Payload。它可以出现在
         * 任意位置（逻辑上放最后最合理）。若 handler 签名中包含它，
         * 便可以在 handler 内自行解包可选参数。例如 Set LAN
         * Configuration Parameters 命令的每个 LAN 参数都是变长
         * （且类型可变）的，固定数据只有通道与参数选择子，其余数据
         * 都要通过 Payload 类及其 unpack API 提取。
         */
        ResultType result;
        try
        {
            std::optional<InputArgsType> inputArgs;
            if constexpr (std::tuple_size<InputArgsType>::value > 0)
            {
                if constexpr (std::is_same<
                                  std::tuple_element_t<0, InputArgsType>,
                                  boost::asio::yield_context>::value)
                {
                    inputArgs.emplace(std::tuple_cat(
                        std::forward_as_tuple(request->ctx->yield),
                        std::move(unpackArgs)));
                }
                else if constexpr (std::is_same<
                                       std::tuple_element_t<0, InputArgsType>,
                                       ipmi::Context::ptr>::value)
                {
                    inputArgs.emplace(
                        std::tuple_cat(std::forward_as_tuple(request->ctx),
                                       std::move(unpackArgs)));
                }
                else if constexpr (std::is_same<
                                       std::tuple_element_t<0, InputArgsType>,
                                       ipmi::message::Request::ptr>::value)
                {
                    inputArgs.emplace(std::tuple_cat(
                        std::forward_as_tuple(request), std::move(unpackArgs)));
                }
                else
                {
                    // 未请求特殊参数（但请求了其他参数）
                    inputArgs.emplace(std::move(unpackArgs));
                }
            }
            else
            {
                // 未请求任何参数
                inputArgs = std::move(unpackArgs);
            }

            // 执行已注册的回调函数，取得 ipmi::RspType<> 结果
            result = std::apply(handler_, *inputArgs);
        }
        catch (const HandlerException& e)
        {
            lg2::info("Handler produced exception, NetFn: {NETFN}, "
                      "Cmd: {CMD}: {ERROR}",
                      "NETFN", lg2::hex, request->ctx->netFn, "CMD", lg2::hex,
                      request->ctx->cmd, "ERROR", e);
            return errorResponse(request, e.code());
        }
        catch (const std::exception& e)
        {
            lg2::error("Handler failed to catch exception, NetFn: {NETFN}, "
                       "Cmd: {CMD}: {ERROR}",
                       "NETFN", lg2::hex, request->ctx->netFn, "CMD", lg2::hex,
                       request->ctx->cmd, "ERROR", e);
            return errorResponse(request, ccUnspecifiedError);
        }
        catch (const HandlerCompletion& c)
        {
            return errorResponse(request, c.code());
        }
        catch (...)
        {
            const char* what = currentExceptionType();
            lg2::error("Handler failed to catch exception, NetFn: {NETFN}, "
                       "Cmd: {CMD}: {ERROR}",
                       "NETFN", lg2::hex, request->ctx->netFn, "CMD", lg2::hex,
                       request->ctx->cmd, "ERROR", what);
            return errorResponse(request, ccUnspecifiedError);
        }

        response->cc = std::get<0>(result);
        auto payload = std::get<1>(result);
        // check for optional payload
        // 检查可选的 payload
        if (payload)
        {
            response->pack(*payload);
        }
        return response;
    }
};

#ifdef ALLOW_DEPRECATED_API
static constexpr size_t maxLegacyBufferSize = 64 * 1024;
/**
 * @brief Legacy IPMI handler class
 *
 * Legacy IPMI handlers will resolve into this class, which will behave the same
 * way as the legacy IPMI queue, passing in a big buffer for the request and a
 * big buffer for the response.
 *
 * As soon as all the handlers have been rewritten, this class will be marked as
 * deprecated and eventually removed.
 */
/* @brief Legacy IPMI handler 类
 * Legacy IPMI handler 将解析成该类，其行为与
 * legacy IPMI 队列相同，传入一个大的请求缓冲区和
 * 一个大的响应缓冲区。
 * 一旦所有 handler 都被重写，该类将被标记为
 * 弃用并最终移除。 */
template <>
class IpmiHandler<ipmid_callback_t> final : public HandlerBase
{
  public:
    explicit IpmiHandler(const ipmid_callback_t& handler, void* ctx = nullptr) :
        handler_(handler), handlerCtx(ctx)
    {}

  private:
    ipmid_callback_t handler_;
    void* handlerCtx;

    /** @brief call the registered handler with the request
     *
     * This is called from the running queue context after it has already
     * created a request object that contains all the information required to
     * execute the ipmi command. This function will return the response object
     * pointer that owns the response object that will ultimately get sent back
     * to the requester.
     *
     * Because this is the legacy variety of IPMI handler, this function does
     * not really have to do much other than pass the payload to the callback
     * and return response to the caller.
     *
     * @param request a shared_ptr to a Request object
     *
     * @return a shared_ptr to a Response object
     */
    /* @brief 用请求调用已注册的 handler
     * 它在运行中的队列上下文里被调用，此时
     * 已经创建了一个包含执行 ipmi 命令所需
     * 全部信息的请求对象。该函数返回响应对象
     * 指针，该指针拥有最终将发送回请求者的
     * 响应对象。
     * 由于这是 legacy 类型的 IPMI handler，
     * 该函数实际上只需把 payload 传给回调
     * 并把响应返回给调用方。
     * @param request 指向 Request 对象的 shared_ptr
     * @return 指向 Response 对象的 shared_ptr */
    message::Response::ptr executeCallback(
        message::Request::ptr request) override
    {
        message::Response::ptr response = request->makeResponse();
        // allocate a big response buffer here
        // 在这里分配一个大的响应缓冲区
        response->payload.resize(maxLegacyBufferSize);

        size_t len = request->payload.size() - request->payload.rawIndex;
        Cc ccRet{ccSuccess};
        try
        {
            ccRet =
                handler_(request->ctx->netFn, request->ctx->cmd,
                         request->payload.data() + request->payload.rawIndex,
                         response->payload.data(), &len, handlerCtx);
        }
        catch (const HandlerException& e)
        {
            lg2::info("Legacy Handler produced exception, NetFn: {NETFN}, "
                      "Cmd: {CMD}: {ERROR}",
                      "NETFN", lg2::hex, request->ctx->netFn, "CMD", lg2::hex,
                      request->ctx->cmd, "ERROR", e);
            return errorResponse(request, e.code());
        }
        catch (const std::exception& e)
        {
            lg2::error("Legacy Handler failed to catch exception, "
                       "NetFn: {NETFN}, Cmd: {CMD}: {ERROR}",
                       "NETFN", lg2::hex, request->ctx->netFn, "CMD", lg2::hex,
                       request->ctx->cmd, "ERROR", e);
            return errorResponse(request, ccUnspecifiedError);
        }
        catch (const HandlerCompletion& c)
        {
            return errorResponse(request, c.code());
        }
        catch (...)
        {
            const char* what = currentExceptionType();
            lg2::error("Handler failed to catch exception, NetFn: {NETFN}, "
                       "Cmd: {CMD}: {ERROR}",
                       "NETFN", lg2::hex, request->ctx->netFn, "CMD", lg2::hex,
                       request->ctx->cmd, "ERROR", what);
            return errorResponse(request, ccUnspecifiedError);
        }
        response->cc = ccRet;
        response->payload.resize(len);
        return response;
    }
};

/**
 * @brief Legacy IPMI OEM handler class
 *
 * Legacy IPMI OEM handlers will resolve into this class, which will behave the
 * same way as the legacy IPMI queue, passing in a big buffer for the request
 * and a big buffer for the response.
 *
 * As soon as all the handlers have been rewritten, this class will be marked as
 * deprecated and eventually removed.
 */
/* @brief Legacy IPMI OEM handler 类
 * Legacy IPMI OEM handler 将解析成该类，其行为与
 * legacy 队列相同，传入一个大的请求缓冲区和
 * 一个大的响应缓冲区。
 * 一旦所有 handler 都被重写，该类将被标记为
 * 弃用并最终移除。 */
template <>
class IpmiHandler<oem::Handler> final : public HandlerBase
{
  public:
    explicit IpmiHandler(const oem::Handler& handler) : handler_(handler) {}

  private:
    oem::Handler handler_;

    /** @brief call the registered handler with the request
     *
     * This is called from the running queue context after it has already
     * created a request object that contains all the information required to
     * execute the ipmi command. This function will return the response object
     * pointer that owns the response object that will ultimately get sent back
     * to the requester.
     *
     * Because this is the legacy variety of IPMI handler, this function does
     * not really have to do much other than pass the payload to the callback
     * and return response to the caller.
     *
     * @param request a shared_ptr to a Request object
     *
     * @return a shared_ptr to a Response object
     */
    /* @brief 用请求调用已注册的 handler
     * 它在运行中的队列上下文里被调用，此时
     * 已经创建了一个包含执行 ipmi 命令所需
     * 全部信息的请求对象。该函数返回响应对象
     * 指针，该指针拥有最终将发送回请求者的
     * 响应对象。
     * 由于这是 legacy 类型的 IPMI handler，
     * 该函数实际上只需把 payload 传给回调
     * 并把响应返回给调用方。
     * @param request 指向 Request 对象的 shared_ptr
     * @return 指向 Response 对象的 shared_ptr */
    message::Response::ptr executeCallback(
        message::Request::ptr request) override
    {
        message::Response::ptr response = request->makeResponse();
        // allocate a big response buffer here
        // 在这里分配一个大的响应缓冲区
        response->payload.resize(maxLegacyBufferSize);

        size_t len = request->payload.size() - request->payload.rawIndex;
        Cc ccRet{ccSuccess};
        try
        {
            ccRet =
                handler_(request->ctx->cmd,
                         request->payload.data() + request->payload.rawIndex,
                         response->payload.data(), &len);
        }
        catch (const HandlerException& e)
        {
            lg2::info("Legacy OEM Handler produced exception, NetFn: {NETFN}, "
                      "Cmd: {CMD}: {ERROR}",
                      "NETFN", lg2::hex, request->ctx->netFn, "CMD", lg2::hex,
                      request->ctx->cmd, "ERROR", e);
            return errorResponse(request, e.code());
        }
        catch (const std::exception& e)
        {
            lg2::error("Legacy OEM Handler failed to catch exception, "
                       "NetFn: {NETFN}, Cmd: {CMD}: {ERROR}",
                       "NETFN", lg2::hex, request->ctx->netFn, "CMD", lg2::hex,
                       request->ctx->cmd, "ERROR", e);
            return errorResponse(request, ccUnspecifiedError);
        }
        catch (const HandlerCompletion& c)
        {
            return errorResponse(request, c.code());
        }
        catch (...)
        {
            const char* what = currentExceptionType();
            lg2::error("Legacy failed to catch exception, NetFn: {NETFN}, "
                       "Cmd: {CMD}: {ERROR}",
                       "NETFN", lg2::hex, request->ctx->netFn, "CMD", lg2::hex,
                       request->ctx->cmd, "ERROR", what);
            return errorResponse(request, ccUnspecifiedError);
        }
        response->cc = ccRet;
        response->payload.resize(len);
        return response;
    }
};

/**
 * @brief create a legacy IPMI handler class and return a shared_ptr
 *
 * The queue uses a map of pointers to do the lookup. This function returns the
 * shared_ptr that owns the Handler object.
 *
 * This is called internally via the ipmi_register_callback function.
 *
 * @param handler the function pointer to the callback
 *
 * @return A shared_ptr to the created handler object
 */
/* @brief 创建一个 legacy IPMI handler 类并返回 shared_ptr
 * 队列使用指针映射来进行查找；该函数返回拥有 Handler 对象的
 * shared_ptr。
 * 它由 ipmi_register_callback 函数在内部调用。
 * @param handler 指向回调的函数指针
 * @return 指向所创建 handler 对象的 shared_ptr */
inline auto makeLegacyHandler(const ipmid_callback_t& handler,
                              void* ctx = nullptr)
{
    HandlerBase::ptr ptr(new IpmiHandler<ipmid_callback_t>(handler, ctx));
    return ptr;
}

/**
 * @brief create a legacy IPMI OEM handler class and return a shared_ptr
 *
 * The queue uses a map of pointers to do the lookup. This function returns the
 * shared_ptr that owns the Handler object.
 *
 * This is called internally via the Router::registerHandler method.
 *
 * @param handler the function pointer to the callback
 *
 * @return A shared_ptr to the created handler object
 */
/* @brief 创建一个 legacy IPMI OEM handler 类并返回 shared_ptr
 * 队列使用指针映射来进行查找；该函数返回拥有 Handler 对象的
 * shared_ptr。
 * 它由 Router::registerHandler 方法在内部调用。
 * @param handler 指向回调的函数指针
 * @return 指向所创建 handler 对象的 shared_ptr */
inline auto makeLegacyHandler(oem::Handler&& handler)
{
    HandlerBase::ptr ptr(
        new IpmiHandler<oem::Handler>(std::forward<oem::Handler>(handler)));
    return ptr;
}
#endif // ALLOW_DEPRECATED_API

/**
 * @brief create an IPMI handler class and return a shared_ptr
 *
 * The queue uses a map of pointers to do the lookup. This function returns the
 * shared_ptr that owns the Handler object.
 *
 * This is called internally via the ipmi::registerHandler function.
 *
 * @param handler the function pointer to the callback
 *
 * @return A shared_ptr to the created handler object
 */
/* @brief 创建一个 IPMI handler 类并返回 shared_ptr
 * 队列使用指针映射来进行查找；该函数返回拥有 Handler 对象的
 * shared_ptr。
 * 它由 ipmi::registerHandler 函数在内部调用。
 * @param handler 指向回调的函数指针
 * @return 指向所创建 handler 对象的 shared_ptr */
template <typename Handler>
inline auto makeHandler(Handler&& handler)
{
    HandlerBase::ptr ptr(
        new IpmiHandler<Handler>(std::forward<Handler>(handler)));
    return ptr;
}

namespace impl
{

// IPMI command handler registration implementation
// IPMI 命令 handler 注册实现
bool registerHandler(int prio, NetFn netFn, Cmd cmd, Privilege priv,
                     ::ipmi::HandlerBase::ptr handler);
bool registerGroupHandler(int prio, Group group, Cmd cmd, Privilege priv,
                          ::ipmi::HandlerBase::ptr handler);
bool registerOemHandler(int prio, Iana iana, Cmd cmd, Privilege priv,
                        ::ipmi::HandlerBase::ptr handler);

} // namespace impl

/**
 * @brief main IPMI handler registration function
 *
 * This function should be used to register all new-style IPMI handler
 * functions. This function just passes the callback to makeHandler, which
 * creates a new wrapper object that will automatically extract the appropriate
 * parameters for the callback function as well as pack up the response.
 *
 * @param prio - priority at which to register; see api.hpp
 * @param netFn - the IPMI net function number to register
 * @param cmd - the IPMI command number to register
 * @param priv - the IPMI user privilege required for this command
 * @param handler - the callback function that will handle this request
 *
 * @return bool - success of registering the handler
 */
/* @brief 主 IPMI handler 注册函数
 * 该函数应用于注册所有新式 IPMI handler
 * 函数。它只是把回调传给 makeHandler，
 * 后者创建一个新的包装对象，自动为回调函数
 * 提取适当的参数并打包响应。
 * @param prio - 注册优先级；参见 api.hpp
 * @param netFn - 要注册的 IPMI 网络功能号
 * @param cmd - 要注册的 IPMI 命令号
 * @param priv - 该命令所需的 IPMI 用户特权
 * @param handler - 将处理此请求的回调函数
 * @return bool - handler 注册是否成功 */
template <typename Handler>
bool registerHandler(int prio, NetFn netFn, Cmd cmd, Privilege priv,
                     Handler&& handler)
{
    auto h = ipmi::makeHandler(std::forward<Handler>(handler));
    return impl::registerHandler(prio, netFn, cmd, priv, h);
}

/**
 * @brief register a IPMI OEM group handler
 *
 * From IPMI 2.0 spec Network Function Codes Table (Row 2Ch):
 * The first data byte position in requests and responses under this network
 * function identifies the defining body that specifies command functionality.
 * Software assumes that the command and completion code field positions will
 * hold command and completion code values.
 *
 * The following values are used to identify the defining body:
 * 00h PICMG - PCI Industrial Computer Manufacturer’s Group.  (www.picmg.com)
 * 01h DMTF Pre-OS Working Group ASF Specification (www.dmtf.org)
 * 02h Server System Infrastructure (SSI) Forum (www.ssiforum.org)
 * 03h VITA Standards Organization (VSO) (www.vita.com)
 * DCh DCMI Specifications (www.intel.com/go/dcmi)
 * all other Reserved
 *
 * When this network function is used, the ID for the defining body occupies
 * the first data byte in a request, and the second data byte (following the
 * completion code) in a response.
 *
 * @tparam Handler - implicitly specified callback function type
 * @param prio - priority at which to register; see api.hpp
 * @param netFn - the IPMI net function number to register
 * @param cmd - the IPMI command number to register
 * @param priv - the IPMI user privilege required for this command
 * @param handler - the callback function that will handle this request
 *
 * @return bool - success of registering the handler
 *
 */
/* @brief 注册 IPMI OEM group handler
 * 根据 IPMI 2.0 规范 Network Function Codes 表（第 2Ch 行）：
 * 该网络功能下请求和响应中的第一个数据字节，
 * 用于标识定义命令功能的机构（defining body）。
 * 软件假定命令和完成码字段的位置分别存放
 * 命令值和完成码值。
 * 以下值用于标识该机构：
 * 00h PICMG - PCI Industrial Computer Manufacturer's Group. (www.picmg.com)
 * 01h DMTF Pre-OS Working Group ASF Specification (www.dmtf.org)
 * 02h Server System Infrastructure (SSI) Forum (www.ssiforum.org)
 * 03h VITA Standards Organization (VSO) (www.vita.com)
 * DCh DCMI Specifications (www.intel.com/go/dcmi)
 * 其他值均为保留
 * 使用该网络功能时，定义机构的 ID 占用
 * 请求中的第一个数据字节，以及响应中
 * （完成码之后）的第二个数据字节。
 * @tparam Handler - 隐式指定的回调函数类型
 * @param prio - 注册优先级；参见 api.hpp
 * @param netFn - 要注册的 IPMI 网络功能号
 * @param cmd - 要注册的 IPMI 命令号
 * @param priv - 该命令所需的 IPMI 用户特权
 * @param handler - 将处理此请求的回调函数
 * @return bool - handler 注册是否成功 */
template <typename Handler>
void registerGroupHandler(int prio, Group group, Cmd cmd, Privilege priv,
                          Handler&& handler)
{
    auto h = ipmi::makeHandler(handler);
    impl::registerGroupHandler(prio, group, cmd, priv, h);
}

/**
 * @brief register a IPMI OEM IANA handler
 *
 * From IPMI spec Network Function Codes Table (Row 2Eh):
 * The first three data bytes of requests and responses under this network
 * function explicitly identify the OEM or non-IPMI group that specifies the
 * command functionality. While the OEM or non-IPMI group defines the
 * functional semantics for the cmd and remaining data fields, the cmd field
 * is required to hold the same value in requests and responses for a given
 * operation in order to be supported under the IPMI message handling and
 * transport mechanisms.
 *
 * When this network function is used, the IANA Enterprise Number for the
 * defining body occupies the first three data bytes in a request, and the
 * first three data bytes following the completion code position in a
 * response.
 *
 * @tparam Handler - implicitly specified callback function type
 * @param prio - priority at which to register; see api.hpp
 * @param netFn - the IPMI net function number to register
 * @param cmd - the IPMI command number to register
 * @param priv - the IPMI user privilege required for this command
 * @param handler - the callback function that will handle this request
 *
 * @return bool - success of registering the handler
 *
 */
/* @brief 注册 IPMI OEM IANA handler
 * 根据 IPMI 规范 Network Function Codes 表（第 2Eh 行）：
 * 该网络功能下请求和响应的前三个数据字节，
 * 明确标识定义命令功能的 OEM 或非 IPMI 机构。
 * 虽然该机构定义了 cmd 和其余数据字段的
 * 功能语义，但对于给定的操作，
 * cmd 字段在请求和响应中必须保持相同的值，
 * 才能受 IPMI 消息处理和传输机制支持。
 * 使用该网络功能时，定义机构的 IANA
 * Enterprise Number 占用请求中的
 * 前三个数据字节，以及响应中
 * 完成码位置之后的
 * 前三个数据字节。
 * @tparam Handler - 隐式指定的回调函数类型
 * @param prio - 注册优先级；参见 api.hpp
 * @param netFn - 要注册的 IPMI 网络功能号
 * @param cmd - 要注册的 IPMI 命令号
 * @param priv - 该命令所需的 IPMI 用户特权
 * @param handler - 将处理此请求的回调函数
 * @return bool - handler 注册是否成功 */
template <typename Handler>
void registerOemHandler(int prio, Iana iana, Cmd cmd, Privilege priv,
                        Handler&& handler)
{
    auto h = ipmi::makeHandler(handler);
    impl::registerOemHandler(prio, iana, cmd, priv, h);
}

} // namespace ipmi

#ifdef ALLOW_DEPRECATED_API
/**
 * @brief legacy IPMI handler registration function
 *
 * This function should be used to register all legacy IPMI handler
 * functions. This function just behaves just as the legacy registration
 * mechanism did, silently replacing any existing handler with a new one.
 *
 * @param netFn - the IPMI net function number to register
 * @param cmd - the IPMI command number to register
 * @param context - ignored
 * @param handler - the callback function that will handle this request
 * @param priv - the IPMI user privilege required for this command
 */
// [[deprecated("Use ipmi::registerHandler() instead")]]
// [[deprecated("Use ipmi::registerHandler() instead")]]  // 已弃用：请改用 ipmi::registerHandler()
void ipmi_register_callback(ipmi_netfn_t netFn, ipmi_cmd_t cmd,
                            ipmi_context_t context, ipmid_callback_t handler,
                            ipmi_cmd_privilege_t priv);

#endif /* ALLOW_DEPRECATED_API */
