
/**
 * @file utility.hpp
 * @brief ipmid 内部小工具。
 */
/*  @file utility.hpp
 *  @brief ipmid 内部小工具。 */
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
#include <boost/asio/spawn.hpp>
#include <boost/callable_traits.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

namespace ipmi
{

// forward declare Context and Request for NonIpmiArgsCount
// 为 NonIpmiArgsCount 前向声明 Context 和 Request
struct Context;
namespace message
{
struct Request;
}

namespace utility
{

/**
 * @brief a utility template to extract the args after N from a tuple
 *
 * Given a tuple of type <T1, ...TN, TN+1, ...>, provide type = tuple<TN+1,...>
 */
/* @brief 从 tuple 中提取第 N 个之后参数的工具模板
 * 给定类型 <T1, ...TN, TN+1, ...> 的 tuple，提供 type = tuple<TN+1,...> */
template <std::size_t N, typename FirstArg, typename... Rest>
struct StripFirstArgs;

template <std::size_t N, typename FirstArg, typename... Rest>
struct StripFirstArgs<N, std::tuple<FirstArg, Rest...>> :
    StripFirstArgs<N - 1, std::tuple<Rest...>>
{};

template <typename FirstArg, typename... Rest>
struct StripFirstArgs<0, std::tuple<FirstArg, Rest...>>
{
    using type = std::tuple<FirstArg, Rest...>;
};
template <std::size_t N>
struct StripFirstArgs<N, std::tuple<>>
{
    using type = std::tuple<>;
};

/**
 * @brief a utility template to extract the remaining args from a tuple
 *
 * Given a tuple of type <T1, T2,...>, provide type = tuple<T2,...>
 */
/* @brief 从 tuple 中提取剩余参数的工具模板
 * 给定类型 <T1, T2,...> 的 tuple，提供 type = tuple<T2,...> */
template <typename T>
using StripFirstArg = StripFirstArgs<1, T>;

/**
 * @brief a utility template to find the number of non-special arguments
 *
 * Given a tuple, count the args after the first special args
 */
/* @brief 计算非特殊参数个数的工具模板
 * 给定一个 tuple，统计第一个特殊参数之后的参数个数 */
template <typename FirstArg, typename... Rest>
struct NonIpmiArgsCount;

template <>
struct NonIpmiArgsCount<std::tuple<>>
{
    constexpr static std::size_t size()
    {
        return 0;
    }
};
template <typename FirstArg, typename... OtherArgs>
struct NonIpmiArgsCount<std::tuple<FirstArg, OtherArgs...>>
{
    constexpr static std::size_t size()
    {
        if constexpr (std::is_same<
                          FirstArg,
                          std::shared_ptr<ipmi::message::Request>>::value ||
                      std::is_same<FirstArg,
                                   std::shared_ptr<ipmi::Context>>::value ||
                      std::is_same<FirstArg, boost::asio::yield_context>::value)
        {
            return 1 + NonIpmiArgsCount<std::tuple<OtherArgs...>>::size();
        }
        else
        {
            return NonIpmiArgsCount<std::tuple<OtherArgs...>>::size();
        }
    }
};

/**
 * @brief a utility template to find the type of the first arg
 *
 * Given a tuple, provide the type of the first element
 */
/* @brief 找出第一个参数类型的工具模板
 * 给定一个 tuple，提供第一个元素的类型 */
template <typename T>
struct GetFirstArg
{
    using type = void;
};

template <typename FirstArg, typename... Rest>
struct GetFirstArg<std::tuple<FirstArg, Rest...>>
{
    using type = FirstArg;
};

/**
 * @brief a utility template to remove const and reference from types
 *
 * Given a tuple, provide the type of the first element
 */
/* @brief 去除类型中 const 和引用的工具模板
 * 给定一个 tuple，提供第一个元素的类型 */
template <typename... Args>
struct DecayTuple;

template <typename... Args>
struct DecayTuple<std::tuple<Args...>>
{
    using type = std::tuple<typename std::decay<Args>::type...>;
};

/** @brief Convert T[N] to T* if is_same<Tbase,T>
 *
 *  @tparam Tbase - The base type expected.
 *  @tparam T - The type to convert.
 */
/* @brief 当 is_same<Tbase,T> 时把 T[N] 转换为 T*
 * @tparam Tbase - 期望的基类型。
 * @tparam T - 要转换的类型。 */
template <typename Tbase, typename T>
using ArrayToPtr_t = typename std::conditional_t<
    std::is_array<T>::value,
    std::conditional_t<std::is_same<Tbase, std::remove_extent_t<T>>::value,
                       std::add_pointer_t<std::remove_extent_t<T>>, T>,
    T>;

/** @brief Downcast type submembers.
 *
 * This allows std::tuple and std::pair members to be downcast to their
 * non-const, nonref versions of themselves to limit duplication in template
 * specializations
 *
 *  1. Remove references.
 *  2. Remove 'const' and 'volatile'.
 *  3. Convert 'char[N]' to 'char*'.
 */
/* @brief 对类型的子成员进行向下转换（downcast）。
 * 这允许把 std::tuple 和 std::pair 成员向下转换为
 * 其非 const、非引用版本，以减少
 * 模板特化中的重复。
 *  1. 去除引用。
 *  2. 去除 'const' 和 'volatile'。
 *  3. 把 'char[N]' 转换为 'char*'。 */
template <typename T>
struct DowncastMembers
{
    using type = T;
};
template <typename... Args>
struct DowncastMembers<std::pair<Args...>>
{
    using type = std::pair<utility::ArrayToPtr_t<
        char, std::remove_cv_t<std::remove_reference_t<Args>>>...>;
};

template <typename... Args>
struct DowncastMembers<std::tuple<Args...>>
{
    using type = std::tuple<utility::ArrayToPtr_t<
        char, std::remove_cv_t<std::remove_reference_t<Args>>>...>;
};

template <typename T>
using DowncastMembers_t = typename DowncastMembers<T>::type;

/** @brief Convert some C++ types to others for 'TypeId' conversion purposes.
 *
 *  Similar C++ types have the same dbus type-id, so 'downcast' those to limit
 *  duplication in TypeId template specializations.
 *
 *  1. Remove references.
 *  2. Remove 'const' and 'volatile'.
 *  3. Convert 'char[N]' to 'char*'.
 */
/* @brief 为 'TypeId' 转换目的把某些 C++ 类型转换为其他类型。
 * 相似的 C++ 类型具有相同的 dbus type-id，因此对它们进行 "downcast"
 * 以减少 TypeId 模板特化中的重复。
 *  1. 去除引用。
 *  2. 去除 'const' 和 'volatile'。
 *  3. 把 'char[N]' 转换为 'char*'。 */
template <typename T>
struct TypeIdDowncast
{
    using type = utility::ArrayToPtr_t<
        char, DowncastMembers_t<std::remove_cv_t<std::remove_reference_t<T>>>>;
};

template <typename T>
using TypeIdDowncast_t = typename TypeIdDowncast<T>::type;

/** @brief Detect if a type is a tuple
 *
 */
/* @brief 检测某个类型是否为 tuple */
template <typename>
struct is_tuple : std::false_type
{};

template <typename... T>
struct is_tuple<std::tuple<T...>> : std::true_type
{};

/** @brief used for static_assert in a constexpr-if else statement
 */
/* @brief 用于 constexpr-if else 语句中的 static_assert */
template <typename T>
struct dependent_false : std::false_type
{};

} // namespace utility

} // namespace ipmi
