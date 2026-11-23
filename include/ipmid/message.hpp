
/**
 * @file message.hpp
 * @brief ipmi::message 总入口：payload 打包/解包。
 */
/*  @file message.hpp
 *  @brief ipmi::message 总入口：payload 打包/解包。 */
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

#include <boost/asio/spawn.hpp>
#include <ipmid/api-types.hpp>
#include <ipmid/message/types.hpp>
#include <ipmid/types.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/asio/connection.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <tuple>
#include <utility>
#include <vector>

namespace ipmi
{

struct Context
{
    using ptr = std::shared_ptr<Context>;

    Context() = delete;
    Context(const Context&) = default;
    Context& operator=(const Context&) = default;
    Context(Context&&) = delete;
    Context& operator=(Context&&) = delete;

    Context(std::shared_ptr<sdbusplus::asio::connection> bus, NetFn netFn,
            uint8_t lun, Cmd cmd, int channel, int userId, uint32_t sessionId,
            Privilege priv, int rqSA, int hostIdx,
            boost::asio::yield_context& yield) :
        bus(bus), netFn(netFn), lun(lun), cmd(cmd), channel(channel),
        userId(userId), sessionId(sessionId), priv(priv), group(0), rqSA(rqSA),
        hostIdx(hostIdx), yield(yield)
    {}

    std::shared_ptr<sdbusplus::asio::connection> bus;
    // normal IPMI context (what call is this, from whence it came...)
    // 普通的 IPMI context（这是什么调用，它从何而来……）
    NetFn netFn;
    uint8_t lun;
    Cmd cmd;
    int channel;
    int userId;
    uint32_t sessionId;
    Privilege priv;
    // defining body code for netFnGroup
    // netFnGroup 的 defining body code
    Group group;
    // srcAddr is only set on IPMB requests because
    // Platform Event Message needs it to determine the incoming format
    // srcAddr 仅在 IPMB 请求中设置，因为
    // Platform Event Message 需要它来确定传入的格式
    int rqSA;
    int hostIdx;
    boost::asio::yield_context yield;
};

namespace message
{

namespace details
{

template <typename A>
struct UnpackSingle;

template <typename T>
using UnpackSingle_t = UnpackSingle<utility::TypeIdDowncast_t<T>>;

template <typename A>
struct PackSingle;

template <typename T>
using PackSingle_t = PackSingle<utility::TypeIdDowncast_t<T>>;

// size to hold 64 bits plus one (possibly-)partial byte
// 用于容纳 64 位再加一个（可能不完整的）字节的大小
static constexpr size_t bitStreamSize = ((sizeof(uint64_t) + 1) * CHAR_BIT);

} // namespace details

/**
 * @brief a payload class that provides a mechanism to pack and unpack data
 *
 * When a new request is being executed, the Payload class is responsible for
 * attempting to unpack all the required arguments from the incoming blob. For
 * variable-length functions, it is possible to have function signature have a
 * Payload object, which will then allow the remaining data to be extracted as
 * needed.
 *
 * When creating a response, the parameters returned from the callback use a
 * newly created payload object to pack all the parameters into a buffer that is
 * then returned to the requester.
 *
 * These interfaces make calls into the message/pack.hpp and message/unpack.hpp
 * functions.
 */
/* @brief 提供数据打包和解包机制的 payload 类
 * 当执行一个新请求时，Payload 类负责
 * 尝试从传入的 blob 中解包所有
 * 必需的参数。对于变长函数，
 * 函数签名中可以带一个 Payload 对象，
 * 从而允许按需提取剩余的数据。
 * 创建响应时，回调返回的参数会使用一个
 * 新建的 payload 对象，把所有参数打包到
 * 一个缓冲区中，然后返回给请求者。
 * 这些接口会调用 message/pack.hpp 和
 * message/unpack.hpp 中的函数。 */
struct Payload
{
    Payload() = default;
    Payload(const Payload&) = default;
    Payload& operator=(const Payload&) = default;
    Payload(Payload&&) = default;
    Payload& operator=(Payload&&) = default;

    explicit Payload(SecureBuffer&& data) : raw(std::move(data)) {}

    ~Payload()
    {
        if (raw.size() != 0 && std::uncaught_exceptions() == 0 && !trailingOk &&
            !unpackCheck && !unpackError)
        {
            lg2::error(
                "Failed to check request for full unpack: raw size: {RAW_SIZE}",
                "RAW_SIZE", raw.size());
        }
    }

    /******************************************************************
     * raw vector access
     *****************************************************************/
    /* raw vector 访问 */
    /**
     * @brief return the size of the underlying raw buffer
     */
    /* @brief 返回底层 raw 缓冲区的大小 */
    size_t size() const
    {
        return raw.size();
    }
    /**
     * @brief resize the underlying raw buffer to a new size
     *
     * @param sz - new size for the buffer
     */
    /* @brief 将底层 raw 缓冲区调整为新大小
     * @param sz - 缓冲区的新大小 */
    void resize(size_t sz)
    {
        raw.resize(sz);
    }
    /**
     * @brief return a pointer to the underlying raw buffer
     */
    /* @brief 返回指向底层 raw 缓冲区的指针 */
    uint8_t* data()
    {
        return raw.data();
    }
    /**
     * @brief return a const pointer to the underlying raw buffer
     */
    /* @brief 返回指向底层 raw 缓冲区的 const 指针 */
    const uint8_t* data() const
    {
        return raw.data();
    }

    /******************************************************************
     * Response operations
     *****************************************************************/
    /* Response 操作 */
    /**
     * @brief append a series of bytes to the buffer
     *
     * @tparam T - the type pointer to return; must be compatible to a byte
     *
     * @param begin - a pointer to the beginning of the series
     * @param end - a pointer to the end of the series
     */
    /* @brief 向缓冲区追加一系列字节
     * @tparam T - 要返回的类型指针；必须与 byte 兼容
     * @param begin - 指向该序列起始的指针
     * @param end - 指向该序列结束的指针 */
    template <typename T>
    void append(T* begin, T* end)
    {
        static_assert(
            std::is_same_v<utility::TypeIdDowncast_t<T>, int8_t> ||
                std::is_same_v<utility::TypeIdDowncast_t<T>, uint8_t> ||
                std::is_same_v<utility::TypeIdDowncast_t<T>, char>,
            "begin and end must be signed or unsigned byte pointers");
        // this interface only allows full-byte access; pack in partial bytes
        // 此接口只允许整字节访问；部分字节需要打包
        drain();
        raw.insert(raw.end(), reinterpret_cast<const uint8_t*>(begin),
                   reinterpret_cast<const uint8_t*>(end));
    }

    /**
     * @brief append a series of bits to the buffer
     *
     * Only the lowest @count order of bits will be appended, with the most
     * significant of those bits getting appended first.
     *
     * @param count - number of bits to append
     * @param bits - a byte with count significant bits to append
     */
    /* @brief 向缓冲区追加一系列位
     * 只附加最低的 @count 位，
     * 其中最高有效位最先被附加。
     * @param count - 要附加的位数
     * @param bits - 含 count 个有效位的字节，用于附加 */
    void appendBits(size_t count, uint8_t bits)
    {
        // drain whole bytes out
        // 整字节地取出（drain）
        drain(true);

        // add in the new bits as the higher-order bits, filling LSBit first
        // 把新位作为高位加入，先填充最低有效位（LSBit）
        fixed_uint_t<details::bitStreamSize> tmp = bits;
        tmp <<= bitCount;
        bitStream |= tmp;
        bitCount += count;

        // drain any whole bytes we have appended
        // 取出我们已追加的所有整字节
        drain(true);
    }

    /**
     * @brief empty out the bucket and pack it as bytes LSB-first
     *
     * @param wholeBytesOnly - if true, only the whole bytes will be drained
     */
    /* @brief 清空 bucket 并按 LSB 优先打包为字节
     * @param wholeBytesOnly - 为 true 时只取出整字节 */
    void drain(bool wholeBytesOnly = false)
    {
        while (bitCount > 0)
        {
            uint8_t retVal;
            if (bitCount < CHAR_BIT)
            {
                if (wholeBytesOnly)
                {
                    break;
                }
            }
            size_t bitsOut = std::min(static_cast<size_t>(CHAR_BIT), bitCount);
            retVal = static_cast<uint8_t>(bitStream);
            raw.push_back(retVal);
            bitStream >>= bitsOut;
            bitCount -= bitsOut;
        }
    }

    // base empty pack
    // 基础的空打包函数
    int pack()
    {
        return 0;
    }

    /**
     * @brief pack arbitrary values (of any supported type) into the buffer
     *
     * @tparam Arg - the type of the first argument
     * @tparam Args - the type of the optional remaining arguments
     *
     * @param arg - the first argument to pack
     * @param args... - the optional remaining arguments to pack
     *
     * @return int - non-zero on pack errors
     */
    /* @brief 将任意值（任意支持的类型）打包到缓冲区
     * @tparam Arg - 第一个参数的类型
     * @tparam Args - 其余可选参数的类型
     * @param arg - 要打包的第一个参数
     * @param args... - 要打包的其余可选参数
     * @return int - 打包出错时返回非零值 */
    template <typename Arg, typename... Args>
    int pack(Arg&& arg, Args&&... args)
    {
        int packRet =
            details::PackSingle_t<Arg>::op(*this, std::forward<Arg>(arg));
        if (packRet)
        {
            return packRet;
        }
        packRet = pack(std::forward<Args>(args)...);
        drain();
        return packRet;
    }

    /**
     * @brief Prepends another payload to this one
     *
     * Avoid using this unless absolutely required since it inserts into the
     * front of the response payload.
     *
     * @param p - The payload to prepend
     *
     * @return int - non-zero on prepend errors
     */
    /* @brief 在本 payload 前面 prepend 另一个 payload
     * 除非绝对必要，否则避免使用，因为它会插入到
     * 响应 payload 的前端。
     * @param p - 要前置的 payload
     * @return int - prepend 出错时返回非零值 */
    int prepend(const ipmi::message::Payload& p)
    {
        if (bitCount != 0 || p.bitCount != 0)
        {
            return 1;
        }
        raw.reserve(raw.size() + p.raw.size());
        raw.insert(raw.begin(), p.raw.begin(), p.raw.end());
        return 0;
    }

    /******************************************************************
     * Request operations
     *****************************************************************/
    /* Request 操作 */
    /**
     * @brief pop a series of bytes from the raw buffer
     *
     * @tparam T - the type pointer to return; must be compatible to a byte
     *
     * @param count - the number of bytes to return
     *
     * @return - a tuple of pointers (begin,begin+count)
     */
    /* @brief 从 raw 缓冲区 pop 出一系列字节
     * @tparam T - 要返回的类型指针；必须与 byte 兼容
     * @param count - 要返回的字节数
     * @return - 由指针组成的 tuple (begin, begin+count) */
    template <typename T>
    auto pop(size_t count)
    {
        static_assert(
            std::is_same_v<utility::TypeIdDowncast_t<T>, int8_t> ||
                std::is_same_v<utility::TypeIdDowncast_t<T>, uint8_t> ||
                std::is_same_v<utility::TypeIdDowncast_t<T>, char>,
            "T* must be signed or unsigned byte pointers");
        // this interface only allows full-byte access; skip partial bits
        // 此接口只允许整字节访问；跳过部分位
        if (bitCount)
        {
            // WARN on unused bits?
            // 对未使用的位发出 WARN？
            discardBits();
        }
        if (count <= (raw.size() - rawIndex))
        {
            auto range = std::make_tuple(
                reinterpret_cast<T*>(raw.data() + rawIndex),
                reinterpret_cast<T*>(raw.data() + rawIndex + count));
            rawIndex += count;
            return range;
        }
        unpackError = true;
        return std::make_tuple(reinterpret_cast<T*>(NULL),
                               reinterpret_cast<T*>(NULL));
    }

    /**
     * @brief fill bit stream with at least count bits for consumption
     *
     * @param count - number of bit needed
     *
     * @return - unpackError
     */
    /* @brief 用至少 count 个位填充位流以供消费
     * @param count - 需要的位数
     * @return - unpackError */
    bool fillBits(size_t count)
    {
        // add more bits to the top end of the bitstream
        // so we consume bits least-significant first
        // 在位流的高端添加更多位，
        // 这样我们按最低有效位优先消费
        if (count > (details::bitStreamSize - CHAR_BIT))
        {
            unpackError = true;
            return unpackError;
        }
        while (bitCount < count)
        {
            if (rawIndex < raw.size())
            {
                fixed_uint_t<details::bitStreamSize> tmp = raw[rawIndex++];
                tmp <<= bitCount;
                bitStream |= tmp;
                bitCount += CHAR_BIT;
            }
            else
            {
                // raw has run out of bytes to pop
                // raw 缓冲区已无字节可 pop
                unpackError = true;
                return unpackError;
            }
        }
        return false;
    }

    /**
     * @brief consume count bits from bitstream (must call fillBits first)
     *
     * @param count - number of bit needed
     *
     * @return - count bits from stream
     */
    /* @brief 从位流消费 count 个位（必须先调用 fillBits）
     * @param count - 需要的位数
     * @return - 从流中取出的 count 个位 */
    uint8_t popBits(size_t count)
    {
        if (bitCount < count)
        {
            unpackError = true;
            return 0;
        }
        // consume bits low-order bits first
        // 先消费低阶位
        auto bits = bitStream.convert_to<uint8_t>();
        bits &= ((1 << count) - 1);
        bitStream >>= count;
        bitCount -= count;
        return bits;
    }

    /**
     * @brief discard all partial bits
     */
    /* @brief 丢弃所有部分位 */
    void discardBits()
    {
        bitStream = 0;
        bitCount = 0;
    }

    /**
     * @brief fully reset the unpack stream
     */
    /* @brief 完全重置解包流 */
    void reset()
    {
        discardBits();
        rawIndex = 0;
        unpackError = false;
    }

    /**
     * @brief check to see if the stream has been fully unpacked
     *
     * @return bool - true if the stream has been unpacked and has no errors
     */
    /* @brief 检查流是否已完全解包
     * @return bool - 已解包且无错误时返回 true */
    bool fullyUnpacked()
    {
        unpackCheck = true;
        return raw.size() == rawIndex && bitCount == 0 && !unpackError;
    }

    // base empty unpack
    // 基础的空解包函数
    int unpack()
    {
        return 0;
    }

    /**
     * @brief unpack arbitrary values (of any supported type) from the buffer
     *
     * @tparam Arg - the type of the first argument
     * @tparam Args - the type of the optional remaining arguments
     *
     * @param arg - the first argument to unpack
     * @param args... - the optional remaining arguments to unpack
     *
     * @return int - non-zero for unpack error
     */
    /* @brief 从缓冲区解包任意值（任意支持的类型）
     * @tparam Arg - 第一个参数的类型
     * @tparam Args - 其余可选参数的类型
     * @param arg - 要解包的第一个参数
     * @param args... - 要解包的其余可选参数
     * @return int - 解包出错时返回非零值 */
    template <typename Arg, typename... Args>
    int unpack(Arg&& arg, Args&&... args)
    {
        int unpackRet =
            details::UnpackSingle_t<Arg>::op(*this, std::forward<Arg>(arg));
        if (unpackRet)
        {
            unpackError = true;
            return unpackRet;
        }
        return unpack(std::forward<Args>(args)...);
    }

    /**
     * @brief unpack a tuple of values (of any supported type) from the buffer
     *
     * This will unpack the elements of the tuple as if each one was passed in
     * individually, as if passed into the above variadic function.
     *
     * @tparam Types - the implicitly declared list of the tuple element types
     *
     * @param t - the tuple of values to unpack
     *
     * @return int - non-zero on unpack error
     */
    /* @brief 从缓冲区解包一个值 tuple（任意支持的类型）
     * 这会像逐个传入一样解包 tuple 的各个元素，
     * 就像传给上面的变长函数那样。
     * @tparam Types - 隐式声明的 tuple 元素类型列表
     * @param t - 要解包的值 tuple
     * @return int - 解包出错时返回非零值 */
    template <typename... Types>
    int unpack(std::tuple<Types...>& t)
    {
        // roll back checkpoint so that unpacking a tuple is atomic
        // 回滚检查点，使解包 tuple 具有原子性
        size_t priorBitCount = bitCount;
        size_t priorIndex = rawIndex;
        fixed_uint_t<details::bitStreamSize> priorBits = bitStream;

        int ret =
            std::apply([this](Types&... args) { return unpack(args...); }, t);
        if (ret)
        {
            bitCount = priorBitCount;
            bitStream = priorBits;
            rawIndex = priorIndex;
        }

        return ret;
    }

    // partial bytes in the form of bits
    // 以位的形式表示的不完整字节
    fixed_uint_t<details::bitStreamSize> bitStream;
    size_t bitCount = 0;
    SecureBuffer raw;
    size_t rawIndex = 0;
    bool trailingOk = true;
    bool unpackCheck = false;
    bool unpackError = false;
};

/**
 * @brief high-level interface to an IPMI response
 *
 * Make it easy to just pack in the response args from the callback into a
 * buffer that goes back to the requester.
 */
/* @brief IPMI 响应的高层接口
 * 让回调中的响应参数能轻松打包到
 * 返回给请求者的缓冲区中。 */
struct Response
{
    /* Define all of the basic class operations:
     *     Not allowed:
     *         - Default constructor to avoid nullptrs.
     *     Allowed:
     *         - Copy operations.
     *         - Move operations.
     *         - Destructor.
     */
    /* 定义所有基本的类操作：
     *     不允许：
     *         - 默认构造函数，以避免空指针。
     *     允许：
     *         - 拷贝操作。
     *         - 移动操作。
     *         - 析构函数。 */
    Response() = delete;
    Response(const Response&) = default;
    Response& operator=(const Response&) = default;
    Response(Response&&) = default;
    Response& operator=(Response&&) = default;
    ~Response() = default;

    using ptr = std::shared_ptr<Response>;

    explicit Response(Context::ptr& context) :
        payload(), ctx(context), cc(ccSuccess)
    {}

    /**
     * @brief pack arbitrary values (of any supported type) into the payload
     *
     * @tparam Args - the type of the optional arguments
     *
     * @param args... - the optional arguments to pack
     *
     * @return int - non-zero on pack errors
     */
    /* @brief 将任意值（任意支持的类型）打包到 payload
     * @tparam Args - 可选参数的类型
     * @param args... - 要打包的可选参数
     * @return int - 打包出错时返回非零值 */
    template <typename... Args>
    int pack(Args&&... args)
    {
        return payload.pack(std::forward<Args>(args)...);
    }

    /**
     * @brief pack a tuple of values (of any supported type) into the payload
     *
     * This will pack the elements of the tuple as if each one was passed in
     * individually, as if passed into the above variadic function.
     *
     * @tparam Types - the implicitly declared list of the tuple element types
     *
     * @param t - the tuple of values to pack
     *
     * @return int - non-zero on pack errors
     */
    /* @brief 将一个值 tuple（任意支持的类型）打包到 payload
     * 这会像逐个传入一样打包 tuple 的各个元素，
     * 就像传给上面的变长函数那样。
     * @tparam Types - 隐式声明的 tuple 元素类型列表
     * @param t - 要打包的值 tuple
     * @return int - 打包出错时返回非零值 */
    template <typename... Types>
    int pack(std::tuple<Types...>& t)
    {
        return payload.pack(t);
    }

    /**
     * @brief Prepends another payload to this one
     *
     * Avoid using this unless absolutely required since it inserts into the
     * front of the response payload.
     *
     * @param p - The payload to prepend
     *
     * @return int - non-zero on prepend errors
     */
    /* @brief 在本 payload 前面 prepend 另一个 payload
     * 除非绝对必要，否则避免使用，因为它会插入到
     * 响应 payload 的前端。
     * @param p - 要前置的 payload
     * @return int - prepend 出错时返回非零值 */
    int prepend(const ipmi::message::Payload& p)
    {
        return payload.prepend(p);
    }

    Payload payload;
    Context::ptr ctx;
    Cc cc;
};

/**
 * @brief high-level interface to an IPMI request
 *
 * Make it easy to unpack the buffer into the request args for the callback.
 */
/* @brief IPMI 请求的高层接口
 * 让缓冲区能轻松解包为回调所需的请求参数。 */
struct Request
{
    /* Define all of the basic class operations:
     *     Not allowed:
     *         - Default constructor to avoid nullptrs.
     *     Allowed:
     *         - Copy operations.
     *         - Move operations.
     *         - Destructor.
     */
    /* 定义所有基本的类操作：
     *     不允许：
     *         - 默认构造函数，以避免空指针。
     *     允许：
     *         - 拷贝操作。
     *         - 移动操作。
     *         - 析构函数。 */
    Request() = delete;
    Request(const Request&) = default;
    Request& operator=(const Request&) = default;
    Request(Request&&) = default;
    Request& operator=(Request&&) = default;
    ~Request() = default;

    using ptr = std::shared_ptr<Request>;

    explicit Request(Context::ptr context, SecureBuffer&& d) :
        payload(std::forward<SecureBuffer>(d)), ctx(context)
    {}

    /**
     * @brief unpack arbitrary values (of any supported type) from the payload
     *
     * @tparam Args - the type of the optional arguments
     *
     * @param args... - the optional arguments to unpack
     *
     * @return int - non-zero for unpack error
     */
    /* @brief 从 payload 解包任意值（任意支持的类型）
     * @tparam Args - 可选参数的类型
     * @param args... - 要解包的可选参数
     * @return int - 解包出错时返回非零值 */
    template <typename... Args>
    int unpack(Args&&... args)
    {
        int unpackRet = payload.unpack(std::forward<Args>(args)...);
        if (unpackRet != ipmi::ccSuccess)
        {
            // not all bits were consumed by requested parameters
            // 请求的参数并未消费完所有位
            return ipmi::ccReqDataLenInvalid;
        }
        if (!payload.trailingOk)
        {
            if (!payload.fullyUnpacked())
            {
                // not all bits were consumed by requested parameters
                // 请求的参数并未消费完所有位
                return ipmi::ccReqDataLenInvalid;
            }
        }
        return ipmi::ccSuccess;
    }

    /**
     * @brief unpack a tuple of values (of any supported type) from the payload
     *
     * This will unpack the elements of the tuple as if each one was passed in
     * individually, as if passed into the above variadic function.
     *
     * @tparam Types - the implicitly declared list of the tuple element types
     *
     * @param t - the tuple of values to unpack
     *
     * @return int - non-zero on unpack error
     */
    /* @brief 从 payload 解包一个值 tuple（任意支持的类型）
     * 这会像逐个传入一样解包 tuple 的各个元素，
     * 就像传给上面的变长函数那样。
     * @tparam Types - 隐式声明的 tuple 元素类型列表
     * @param t - 要解包的值 tuple
     * @return int - 解包出错时返回非零值 */
    template <typename... Types>
    int unpack(std::tuple<Types...>& t)
    {
        return std::apply([this](Types&... args) { return unpack(args...); },
                          t);
    }

    /** @brief Create a response message that corresponds to this request
     *
     * @return A shared_ptr to the response message created
     */
    /* @brief 创建与本请求对应的响应消息
     * @return 指向所创建响应消息的 shared_ptr */
    Response::ptr makeResponse()
    {
        return std::make_shared<Response>(ctx);
    }

    Payload payload;
    Context::ptr ctx;
};

} // namespace message

} // namespace ipmi

// include packing and unpacking of types
// 包含类型的打包与解包
#include <ipmid/message/pack.hpp>
#include <ipmid/message/unpack.hpp>
