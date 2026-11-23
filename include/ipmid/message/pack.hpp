
/**
 * @file pack.hpp
 * @brief 把 C++ 参数打包为 IPMI payload 的实现。
 */
/*  @file pack.hpp
 *  @brief 把 C++ 参数打包为 IPMI payload 的实现。 */
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
#include <phosphor-logging/lg2.hpp>
#include <phosphor-logging/log.hpp>

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>
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
void PackBytes(uint8_t* pointer, const NumericType& i)
{
    if constexpr (byteIndex < sizeof(NumericType))
    {
        *pointer = static_cast<uint8_t>(i >> (8 * byteIndex));
        PackBytes<NumericType, byteIndex + 1>(pointer + 1, i);
    }
}

template <typename NumericType, size_t byteIndex = 0>
void PackBytesUnaligned(Payload& p, const NumericType& i)
{
    if constexpr (byteIndex < sizeof(NumericType))
    {
        p.appendBits(CHAR_BIT, static_cast<uint8_t>(i >> (8 * byteIndex)));
        PackBytesUnaligned<NumericType, byteIndex + 1>(p, i);
    }
}

/** @struct PackSingle
 *  @brief Utility to pack a single C++ element into a Payload
 *
 *  User-defined types are expected to specialize this template in order to
 *  get their functionality.
 *
 *  @tparam S - Type of element to pack.
 */
/* @struct PackSingle
 * @brief 把单个 C++ 元素打包到 Payload 的实用工具
 * 用户定义类型应特化该模板
 * 以实现其功能。
 * @tparam S - 要打包的元素类型。 */
template <typename T>
struct PackSingle
{
    /** @brief Do the operation to pack element.
     *
     *  @param[in] p - Payload to pack into.
     *  @param[out] t - The reference to pack item into.
     */
    /* @brief 执行打包元素的操作。
     *  @param[in] p - 要打包到的 Payload。
     *  @param[out] t - 要打包入的项的引用。 */
    static int op(Payload& p, const T& t)
    {
        static_assert(std::is_integral_v<T>,
                      "Attempt to pack a type that has no IPMI pack operation");
        // if not on a byte boundary, must pack values LSbit/LSByte first
        // 如果不在字节边界上，必须先打包 LSbit/LSByte
        if (p.bitCount)
        {
            PackBytesUnaligned<T>(p, t);
        }
        else
        {
            // copy in bits to vector....
            // 把位复制进 vector……
            p.raw.resize(p.raw.size() + sizeof(T));
            uint8_t* out = p.raw.data() + p.raw.size() - sizeof(T);
            PackBytes<T>(out, t);
        }
        return 0;
    }
};

/** @brief Specialization of PackSingle for std::tuple<T> */
/* @brief PackSingle 对 std::tuple<T> 的特化 */
template <typename... T>
struct PackSingle<std::tuple<T...>>
{
    static int op(Payload& p, const std::tuple<T...>& v)
    {
        return std::apply([&p](const T&... args) { return p.pack(args...); },
                          v);
    }
};

/** @brief Specialization of PackSingle for std::string
 *  represented as a UCSD-Pascal style string
 */
/* @brief PackSingle 对 std::string 的特化，
 * 表示为 UCSD-Pascal 风格的字符串 */
template <>
struct PackSingle<std::string>
{
    static int op(Payload& p, const std::string& t)
    {
        // check length first
        // 先检查长度
        uint8_t len;
        if (t.length() > std::numeric_limits<decltype(len)>::max())
        {
            lg2::error("long string truncated on IPMI message pack");
            return 1;
        }
        len = static_cast<uint8_t>(t.length());
        PackSingle<uint8_t>::op(p, len);
        p.append(t.c_str(), t.c_str() + t.length());
        return 0;
    }
};

/** @brief Specialization of PackSingle for fixed_uint_t types
 */
/* @brief PackSingle 对 fixed_uint_t 类型的特化 */
template <bitcount_t N>
struct PackSingle<fixed_uint_t<N>>
{
    static int op(Payload& p, const fixed_uint_t<N>& t)
    {
        size_t count = N;
        static_assert(N <= (details::bitStreamSize - CHAR_BIT));
        static_assert(N <= std::numeric_limits<uint64_t>::digits,
                      "Type exceeds uint64_t limit");
        uint64_t bits = static_cast<uint64_t>(t);
        while (count > 0)
        {
            size_t appendCount = std::min(count, static_cast<size_t>(CHAR_BIT));
            p.appendBits(appendCount, static_cast<uint8_t>(bits));
            bits >>= CHAR_BIT;
            count -= appendCount;
        }
        return 0;
    }
};

/** @brief Specialization of PackSingle for bool. */
/* @brief PackSingle 对 bool 的特化。 */
template <>
struct PackSingle<bool>
{
    static int op(Payload& p, const bool& b)
    {
        p.appendBits(1, b);
        return 0;
    }
};

/** @brief Specialization of PackSingle for std::bitset<N> */
/* @brief PackSingle 对 std::bitset<N> 的特化 */
template <size_t N>
struct PackSingle<std::bitset<N>>
{
    static int op(Payload& p, const std::bitset<N>& t)
    {
        size_t count = N;
        static_assert(N <= (details::bitStreamSize - CHAR_BIT));
        unsigned long long bits = t.to_ullong();
        while (count > 0)
        {
            size_t appendCount = std::min(count, size_t(CHAR_BIT));
            p.appendBits(appendCount, static_cast<uint8_t>(bits));
            bits >>= CHAR_BIT;
            count -= appendCount;
        }
        return 0;
    }
};

/** @brief Specialization of PackSingle for std::optional<T> */
/* @brief PackSingle 对 std::optional<T> 的特化 */
template <typename T>
struct PackSingle<std::optional<T>>
{
    static int op(Payload& p, const std::optional<T>& t)
    {
        int ret = 0;
        if (t)
        {
            ret = PackSingle<T>::op(p, *t);
        }
        return ret;
    }
};

/** @brief Specialization of PackSingle for std::array<T, N> */
/* @brief PackSingle 对 std::array<T, N> 的特化 */
template <typename T, size_t N>
struct PackSingle<std::array<T, N>>
{
    static int op(Payload& p, const std::array<T, N>& t)
    {
        int ret = 0;
        for (const auto& v : t)
        {
            int ret = PackSingle<T>::op(p, v);
            if (ret)
            {
                break;
            }
        }
        return ret;
    }
};

/** @brief Specialization of PackSingle for std::vector<T> */
/* @brief PackSingle 对 std::vector<T> 的特化 */
template <typename T>
struct PackSingle<std::vector<T>>
{
    static int op(Payload& p, const std::vector<T>& t)
    {
        int ret = 0;
        for (const auto& v : t)
        {
            int ret = PackSingle<T>::op(p, v);
            if (ret)
            {
                break;
            }
        }
        return ret;
    }
};

/** @brief Specialization of PackSingle for std::vector<uint8_t> */
/* @brief PackSingle 对 std::vector<uint8_t> 的特化 */
template <>
struct PackSingle<std::vector<uint8_t>>
{
    static int op(Payload& p, const std::vector<uint8_t>& t)
    {
        if (p.bitCount != 0)
        {
            return 1;
        }
        p.raw.reserve(p.raw.size() + t.size());
        p.raw.insert(p.raw.end(), t.begin(), t.end());
        return 0;
    }
};

/** @brief Specialization of PackSingle for SecureBuffer */
/* @brief PackSingle 对 SecureBuffer 的特化 */
template <>
struct PackSingle<SecureBuffer>
{
    static int op(Payload& p, const SecureBuffer& t)
    {
        if (p.bitCount != 0)
        {
            return 1;
        }
        p.raw.reserve(p.raw.size() + t.size());
        p.raw.insert(p.raw.end(), t.begin(), t.end());
        return 0;
    }
};

/** @brief Specialization of PackSingle for std::span<const uint8_t> */
/* @brief PackSingle 对 std::span<const uint8_t> 的特化 */
template <>
struct PackSingle<std::span<const uint8_t>>
{
    static int op(Payload& p, const std::span<const uint8_t>& t)
    {
        if (p.bitCount != 0)
        {
            return 1;
        }
        p.raw.reserve(p.raw.size() + t.size());
        p.raw.insert(p.raw.end(), t.begin(), t.end());
        return 0;
    }
};

/** @brief Specialization of PackSingle for std::string_view */
/* @brief PackSingle 对 std::string_view 的特化 */
template <>
struct PackSingle<std::string_view>
{
    static int op(Payload& p, const std::string_view& t)
    {
        if (p.bitCount != 0)
        {
            return 1;
        }
        p.raw.reserve(p.raw.size() + t.size());
        p.raw.insert(p.raw.end(), t.begin(), t.end());
        return 0;
    }
};

/** @brief Specialization of PackSingle for std::variant<T, N> */
/* @brief PackSingle 对 std::variant<T, N> 的特化 */
template <typename... T>
struct PackSingle<std::variant<T...>>
{
    static int op(Payload& p, const std::variant<T...>& v)
    {
        return std::visit(
            [&p](const auto& arg) {
                return PackSingle<std::decay_t<decltype(arg)>>::op(p, arg);
            },
            v);
    }
};

/** @brief Specialization of PackSingle for Payload */
/* @brief PackSingle 对 Payload 的特化 */
template <>
struct PackSingle<Payload>
{
    static int op(Payload& p, const Payload& t)
    {
        if (p.bitCount != 0 || t.bitCount != 0)
        {
            return 1;
        }
        p.raw.reserve(p.raw.size() + t.raw.size());
        p.raw.insert(p.raw.end(), t.raw.begin(), t.raw.end());
        return 0;
    }
};

} // namespace details

} // namespace message

} // namespace ipmi
