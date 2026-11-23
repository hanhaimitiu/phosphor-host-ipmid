
/**
 * @file types.hpp
 * @brief payload 打包/解包支持的类型与逐字节类型。
 */
/*  @file types.hpp
 *  @brief payload 打包/解包支持的类型与逐字节类型。 */
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

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/version.hpp>
#include <ipmid/utility.hpp>

#include <bitset>
#include <tuple>

#if BOOST_VERSION < 107900
using bitcount_t = unsigned;
#else
using bitcount_t = std::size_t;
#endif

// unsigned fixed-bit sizes
// 无符号固定位宽类型
template <bitcount_t N>
using fixed_uint_t =
    boost::multiprecision::number<boost::multiprecision::cpp_int_backend<
        N, N, boost::multiprecision::unsigned_magnitude,
        boost::multiprecision::unchecked, void>>;
// signed fixed-bit sizes
// 有符号固定位宽类型
template <bitcount_t N>
using fixed_int_t =
    boost::multiprecision::number<boost::multiprecision::cpp_int_backend<
        N, N, boost::multiprecision::signed_magnitude,
        boost::multiprecision::unchecked, void>>;

using uint1_t = fixed_uint_t<1>;
using uint2_t = fixed_uint_t<2>;
using uint3_t = fixed_uint_t<3>;
using uint4_t = fixed_uint_t<4>;
using uint5_t = fixed_uint_t<5>;
using uint6_t = fixed_uint_t<6>;
using uint7_t = fixed_uint_t<7>;
// native uint8_t
// 原生 uint8_t
using uint9_t = fixed_uint_t<9>;
using uint10_t = fixed_uint_t<10>;
using uint11_t = fixed_uint_t<11>;
using uint12_t = fixed_uint_t<12>;
using uint13_t = fixed_uint_t<13>;
using uint14_t = fixed_uint_t<14>;
using uint15_t = fixed_uint_t<15>;
// native uint16_t
// 原生 uint16_t
using uint24_t = fixed_uint_t<24>;

// signed fixed-bit sizes
// 有符号固定位宽类型
using int2_t = fixed_int_t<2>;
using int3_t = fixed_int_t<3>;
using int4_t = fixed_int_t<4>;
using int5_t = fixed_int_t<5>;
using int6_t = fixed_int_t<6>;
using int7_t = fixed_int_t<7>;
// native int8_t
// 原生 int8_t
using int9_t = fixed_int_t<9>;
using int10_t = fixed_int_t<10>;
using int11_t = fixed_int_t<11>;
using int12_t = fixed_int_t<12>;
using int13_t = fixed_int_t<13>;
using int14_t = fixed_int_t<14>;
using int15_t = fixed_int_t<15>;
// native int16_t
// 原生 int16_t
using int24_t = fixed_int_t<24>;

// bool is more efficient than a uint1_t
// bool 比 uint1_t 更高效
using bit = bool;

// Mechanism for going from uint7_t, int7_t, or std::bitset<7> to 7 bits
// use nrFixedBits<uint7_t> or nrFixedBits<decltype(u7)>
// 将 uint7_t、int7_t 或 std::bitset<7> 转换为 7 位的机制
// 使用 nrFixedBits<uint7_t> 或 nrFixedBits<decltype(u7)>
namespace types
{
namespace details
{

template <bitcount_t N>
struct Size
{
    static constexpr bitcount_t value = N;
};

template <bitcount_t Bits>
constexpr auto getNrBits(const fixed_int_t<Bits>&) -> Size<Bits>;
template <bitcount_t Bits>
constexpr auto getNrBits(const fixed_uint_t<Bits>&) -> Size<Bits>;
template <bitcount_t Bits>
constexpr auto getNrBits(const std::bitset<Bits>&) -> Size<Bits>;

template <typename U>
using underlying_t =
    typename std::conditional_t<std::is_enum_v<U>, std::underlying_type<U>,
                                std::enable_if<true, U>>::type;

} // namespace details

/**
 * @brief mechanism to get N from a type like fixed_int_t<N>
 *
 * helper template to extract N from a fixed_(u)int_t variable
 *
 * @tparam T - a type of fixed_int_t<N> or fixed_unint_t<N>
 *
 * @return size_t - evaluates to a constexpr size_t of N
 */
/* @brief 从 fixed_int_t<N> 这类类型中获取 N 的机制
 * 从 fixed_(u)int_t 变量中提取 N 的辅助模板
 * @tparam T - fixed_int_t<N> 或 fixed_unint_t<N> 类型
 * @return size_t - 求值为 N 的 constexpr size_t */
template <typename T>
constexpr auto nrFixedBits =
    decltype(details::getNrBits(std::declval<T>()))::value;

/**
 * @brief Converts a number or enum class to another
 * @tparam R - The output type
 * @tparam T - The input type
 * @param t - An enum or integer value to cast
 * @return The value in R form
 */
/* @brief 将数字或 enum class 转换为另一种类型
 * @tparam R - 输出类型
 * @tparam T - 输入类型
 * @param t - 要转换的枚举或整数值
 * @return R 形式的值 */
template <typename R, typename T>
inline R enum_cast(T t)
{
    auto tu = static_cast<details::underlying_t<T>>(t);
    auto ru = static_cast<details::underlying_t<R>>(tu);
    return static_cast<R>(ru);
}

} // namespace types
