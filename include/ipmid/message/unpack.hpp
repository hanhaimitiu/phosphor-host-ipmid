
/**
 * @file unpack.hpp
 * @brief 从 IPMI payload 解包 C++ 参数的实现。
 */
/*  @file unpack.hpp
 *  @brief 从 IPMI payload 解包 C++ 参数的实现。 */
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
/*  版权所有 © 2018 Intel Corporation
 *  根据 Apache 许可证 2.0 版（"License"）授权；
 *  除非符合许可证的规定，否则不得使用本文件。
 *  你可以从以下地址获取许可证副本：
 *      http://www.apache.org/licenses/LICENSE-2.0
 *  除非适用法律要求或书面同意，否则本软件
 *  按"原样"分发，
 *  不提供任何明示或暗示的保证或条件。
 *  有关许可证下的具体权限和限制，请参阅许可证
 *  本身。 */
#pragma once

#include <ipmid/message.hpp>
#include <ipmid/message/types.hpp>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <vector>

namespace ipmi
{

namespace message
{

namespace details
{

/**************************************
 * ipmi return type helpers
 **************************************/
/* ipmi 返回类型辅助函数 */

template <typename NumericType, size_t byteIndex = 0>
void UnpackBytes(uint8_t* pointer, NumericType& i)
{
    if constexpr (byteIndex < sizeof(NumericType))
    {
        i |= static_cast<NumericType>(*pointer) << (CHAR_BIT * byteIndex);
        UnpackBytes<NumericType, byteIndex + 1>(pointer + 1, i);
    }
}

template <typename NumericType, size_t byteIndex = 0>
void UnpackBytesUnaligned(Payload& p, NumericType& i)
{
    if constexpr (byteIndex < sizeof(NumericType))
    {
        i |= static_cast<NumericType>(p.popBits(CHAR_BIT))
             << (CHAR_BIT * byteIndex);
        UnpackBytesUnaligned<NumericType, byteIndex + 1>(p, i);
    }
}

/** @struct UnpackSingle
 *  @brief Utility to unpack a single C++ element from a Payload
 *
 *  User-defined types are expected to specialize this template in order to
 *  get their functionality.
 *
 *  @tparam T - Type of element to unpack.
 */
/* @struct UnpackSingle
 * @brief 从 Payload 解包单个 C++ 元素的实用工具
 * 用户定义类型应特化该模板
 * 以实现其功能。
 * @tparam T - 要解包的元素类型。 */
template <typename T>
struct UnpackSingle
{
    /** @brief Do the operation to unpack element.
     *
     *  @param[in] p - Payload to unpack from.
     *  @param[out] t - The reference to unpack item into.
     */
    /* @brief 执行解包元素的操作。
     *  @param[in] p - 要从中解包的 Payload。
     *  @param[out] t - 要解包入的项的引用。 */
    static int op(Payload& p, T& t)
    {
        if constexpr (std::is_fundamental<T>::value)
        {
            t = 0;
            if (p.bitCount)
            {
                if (p.fillBits(CHAR_BIT * sizeof(t)))
                {
                    return 1;
                }
                UnpackBytesUnaligned<T>(p, t);
            }
            else
            {
                // copy out bits from vector....
                // 把位从 vector 中复制出来……
                if (p.raw.size() < (p.rawIndex + sizeof(t)))
                {
                    return 1;
                }
                auto iter = p.raw.data() + p.rawIndex;
                t = 0;
                UnpackBytes<T>(iter, t);
                p.rawIndex += sizeof(t);
            }
            return 0;
        }
        else if constexpr (utility::is_tuple<T>::value)
        {
            bool priorError = p.unpackError;
            size_t priorIndex = p.rawIndex;
            // more stuff to unroll if partial bytes are out
            // 若还有不完整的字节，需要展开更多内容
            size_t priorBitCount = p.bitCount;
            fixed_uint_t<details::bitStreamSize> priorBits = p.bitStream;
            int ret = p.unpack(t);
            if (ret != 0)
            {
                t = T();
                p.rawIndex = priorIndex;
                p.bitStream = priorBits;
                p.bitCount = priorBitCount;
                p.unpackError = priorError;
            }
            return ret;
        }
        else
        {
            static_assert(
                utility::dependent_false<T>::value,
                "Attempt to unpack a type that has no IPMI unpack operation");
        }
    }
};

/** @struct UnpackSingle
 *  @brief Utility to unpack a single C++ element from a Payload
 *
 *  Specialization to unpack std::string represented as a
 *  UCSD-Pascal style string
 */
/* @struct UnpackSingle
 * @brief 从 Payload 解包单个 C++ 元素的实用工具
 * 特化：解包表示为 UCSD-Pascal 风格字符串的
 * std::string */
template <>
struct UnpackSingle<std::string>
{
    static int op(Payload& p, std::string& t)
    {
        // pop len first
        // 先 pop 出长度
        if (p.rawIndex > (p.raw.size() - sizeof(uint8_t)))
        {
            return 1;
        }
        uint8_t len = p.raw[p.rawIndex++];
        // check to see that there are n bytes left
        // 检查是否还剩 n 个字节
        auto [first, last] = p.pop<char>(len);
        if (first == last)
        {
            return 1;
        }
        t.reserve(last - first);
        t.insert(0, first, (last - first));
        return 0;
    }
};

/** @brief Specialization of UnpackSingle for fixed_uint_t types
 */
/* @brief UnpackSingle 对 fixed_uint_t 类型的特化 */
template <bitcount_t N>
struct UnpackSingle<fixed_uint_t<N>>
{
    static int op(Payload& p, fixed_uint_t<N>& t)
    {
        static_assert(N <= (details::bitStreamSize - CHAR_BIT));
        constexpr size_t count = N;
        // acquire enough bits in the stream to fulfill the Payload
        // 从流中获取足够的比特以满足 Payload 的需要
        if (p.fillBits(count))
        {
            return -1;
        }
        fixed_uint_t<details::bitStreamSize> bitmask = ((1 << count) - 1);
        t = (p.bitStream & bitmask).convert_to<fixed_uint_t<N>>();
        p.bitStream >>= count;
        p.bitCount -= count;
        return 0;
    }
};

/** @brief Specialization of UnpackSingle for bool. */
/* @brief UnpackSingle 针对 bool 的特化。 */
template <>
struct UnpackSingle<bool>
{
    static int op(Payload& p, bool& b)
    {
        // acquire enough bits in the stream to fulfill the Payload
        // 从流中获取足够的比特以满足 Payload 的需要
        if (p.fillBits(1))
        {
            return -1;
        }
        b = static_cast<bool>(p.bitStream & 0x01);
        // clear bits from stream
        // 从流中清除比特
        p.bitStream >>= 1;
        p.bitCount -= 1;
        return 0;
    }
};

/** @brief Specialization of UnpackSingle for std::bitset<N>
 */
/* @brief UnpackSingle 针对 std::bitset<N> 的特化。 */
template <size_t N>
struct UnpackSingle<std::bitset<N>>
{
    static int op(Payload& p, std::bitset<N>& t)
    {
        static_assert(N <= (details::bitStreamSize - CHAR_BIT));
        size_t count = N;
        // acquire enough bits in the stream to fulfill the Payload
        // 从流中获取足够的比特以满足 Payload 的需要
        if (p.fillBits(count))
        {
            return -1;
        }
        fixed_uint_t<details::bitStreamSize> bitmask =
            ~fixed_uint_t<details::bitStreamSize>(0) >>
            (details::bitStreamSize - count);
        t |= (p.bitStream & bitmask).convert_to<unsigned long long>();
        p.bitStream >>= count;
        p.bitCount -= count;
        return 0;
    }
};

/** @brief Specialization of UnpackSingle for std::optional<T> */
/* @brief UnpackSingle 针对 std::optional<T> 的特化。 */
template <typename T>
struct UnpackSingle<std::optional<T>>
{
    static int op(Payload& p, std::optional<T>& t)
    {
        bool priorError = p.unpackError;
        size_t priorIndex = p.rawIndex;
        // more stuff to unroll if partial bytes are out
        // 如果还有不完整的字节，需要展开更多内容
        size_t priorBitCount = p.bitCount;
        fixed_uint_t<details::bitStreamSize> priorBits = p.bitStream;
        T value;
        int ret = UnpackSingle<T>::op(p, value);
        if (ret != 0)
        {
            t.reset();
            p.rawIndex = priorIndex;
            p.bitStream = priorBits;
            p.bitCount = priorBitCount;
            p.unpackError = priorError;
        }
        else
        {
            t.emplace(std::move(value));
        }
        return 0;
    }
};

/** @brief Specialization of UnpackSingle for std::array<T, N> */
/* @brief UnpackSingle 针对 std::array<T, N> 的特化。 */
template <typename T, size_t N>
struct UnpackSingle<std::array<T, N>>
{
    static int op(Payload& p, std::array<T, N>& t)
    {
        int ret = 0;
        size_t priorIndex = p.rawIndex;
        for (auto& v : t)
        {
            ret = UnpackSingle<T>::op(p, v);
            if (ret)
            {
                p.rawIndex = priorIndex;
                t = std::array<T, N>();
                break;
            }
        }
        return ret;
    }
};

/** @brief Specialization of UnpackSingle for std::array<uint8_t> */
/* @brief UnpackSingle 针对 std::array<uint8_t> 的特化。 */
template <size_t N>
struct UnpackSingle<std::array<uint8_t, N>>
{
    static int op(Payload& p, std::array<uint8_t, N>& t)
    {
        if (p.raw.size() - p.rawIndex < N)
        {
            t.fill(0);
            return -1;
        }
        // copy out the bytes
        // 拷贝出这些字节
        std::copy(p.raw.begin() + p.rawIndex, p.raw.begin() + p.rawIndex + N,
                  t.begin());
        p.rawIndex += N;
        return 0;
    }
};

/** @brief Specialization of UnpackSingle for std::vector<T> */
/* @brief UnpackSingle 针对 std::vector<T> 的特化。 */
template <typename T>
struct UnpackSingle<std::vector<T>>
{
    static int op(Payload& p, std::vector<T>& t)
    {
        size_t nrReqd = t.size();
        size_t unpackIndex{0};
        while (p.rawIndex < p.raw.size())
        {
            if (nrReqd && nrReqd == unpackIndex)
            {
                // user requested exactly N items
                // 用户正好请求了 N 个元素
                break;
            }
            if (unpackIndex == t.size())
            {
                t.emplace_back();
            }
            // empty T unpack will break out of loop
            // 空的 T 解包会跳出循环
            const size_t bitsBefore = p.bitCount;
            const size_t indexBefore = p.rawIndex;

            int unpackOK = UnpackSingle<T>::op(p, t[unpackIndex]);

            bool emptyUnpack = bitsBefore == p.bitCount &&
                               indexBefore == p.rawIndex;
            if (unpackOK || emptyUnpack)
            {
                if (!nrReqd)
                {
                    t.pop_back();
                }
                break;
            }
            unpackIndex++;
        }
        // if the user requested exactly N, return failure if
        // not exactly N were extracted
        // 如果用户正好请求了 N 个，若解出的
        // 数量不正好是 N 个则返回失败
        if (nrReqd && nrReqd != unpackIndex)
        {
            return 1;
        }
        // Otherwise, unpacking a vector is always successful:
        // either stuff was unpacked successfully (return 0)
        // or stuff was not unpacked, but should still return
        // success because an empty vector or a not-fully-unpacked
        // payload is not a failure.
        // 否则，解包 vector 总是成功的：
        // 要么内容被成功解出（返回 0）
        // 要么内容没有被解出，但仍应返回
        // 成功，因为空 vector 或未完全解包的
        // payload 都不算失败。
        return 0;
    }
};

/** @brief Specialization of UnpackSingle for std::vector<uint8_t> */
/* @brief UnpackSingle 针对 std::vector<uint8_t> 的特化。 */
template <>
struct UnpackSingle<std::vector<uint8_t>>
{
    static int op(Payload& p, std::vector<uint8_t>& t)
    {
        size_t cpBytes = p.raw.size() - p.rawIndex;
        // check for a request of a specific number of bytes
        // allows for <count>, <count bytes> to be part of the request
        // 检查是否请求了特定数量的字节
        // 允许 <count>、<count bytes> 作为请求的一部分
        if (t.size())
        {
            // only failure is when a specific number of bytes was requested
            // but there are not that many bytes remaining in the raw request
            // 唯一的失败情况是请求了特定数量的字节
            // 但原始请求中剩余字节不足
            if (cpBytes < t.size())
            {
                return 1;
            }
            cpBytes = t.size();
        }
        else
        {
            t.resize(cpBytes);
        }
        auto firstOut = p.raw.begin() + p.rawIndex;
        std::copy_n(firstOut, cpBytes, t.begin());
        p.rawIndex += cpBytes;
        return 0;
    }
};

/** @brief Specialization of UnpackSingle for SecureBuffer */
/* @brief UnpackSingle 针对 SecureBuffer 的特化。 */
template <>
struct UnpackSingle<SecureBuffer>
{
    static int op(Payload& p, SecureBuffer& t)
    {
        // copy out the remainder of the message
        // 拷贝出消息的剩余部分
        t.reserve(p.raw.size() - p.rawIndex);
        t.insert(t.begin(), p.raw.begin() + p.rawIndex, p.raw.end());
        p.rawIndex = p.raw.size();
        return 0;
    }
};

/** @brief Specialization of UnpackSingle for std::span<const uint8_t> */
/* @brief UnpackSingle 针对 std::span<const uint8_t> 的特化。 */
template <>
struct UnpackSingle<std::span<const uint8_t>>
{
    static int op(Payload& p, std::span<const uint8_t>& t)
    {
        // copy out the remainder of the message
        // 拷贝出消息的剩余部分
        t = std::span<const uint8_t>(p.raw.begin() + p.rawIndex, p.raw.end());
        p.rawIndex = p.raw.size();
        return 0;
    }
};

/** @brief Specialization of UnpackSingle for Payload */
/* @brief UnpackSingle 针对 Payload 的特化。 */
template <>
struct UnpackSingle<Payload>
{
    static int op(Payload& p, Payload& t)
    {
        t = p;
        // mark that this payload is being included in the args
        // 标记此 payload 正被纳入参数中
        p.trailingOk = true;
        return 0;
    }
};

} // namespace details

} // namespace message

} // namespace ipmi
