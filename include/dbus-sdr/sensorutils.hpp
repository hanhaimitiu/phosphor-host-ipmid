
/**
 * @file sensorutils.hpp
 * @brief 传感器工具函数的声明。
 */
/*  @file sensorutils.hpp
 *  @brief 传感器工具函数的声明。 */
/*
// Copyright (c) 2017 2018 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/
/* // 版权所有 (c) 2017 2018 Intel Corporation
 * //
 * // 根据 Apache 许可证 2.0 版（"License"）授权；
 * // 除非符合许可证的规定，否则不得使用本文件。
 * // 你可以从以下地址获取许可证副本：
 * //
 * //      http://www.apache.org/licenses/LICENSE-2.0
 * //
 * // 除非适用法律要求或书面同意，否则本软件
 * // 按"原样"分发，
 * // 不提供任何明示或暗示的保证或条件。
 * // 有关许可证下的具体权限和限制，请参阅许可证
 * // 本身。 */
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace ipmi
{
static constexpr int16_t maxInt10 = 0x1FF;
static constexpr int16_t minInt10 = -0x200;
static constexpr int8_t maxInt4 = 7;
static constexpr int8_t minInt4 = -8;

bool getSensorAttributes(const double max, const double min, int16_t& mValue,
                         int8_t& rExp, int16_t& bValue, int8_t& bExp,
                         bool& bSigned);

uint8_t scaleIPMIValueFromDouble(const double value, const int16_t mValue,
                                 const int8_t rExp, const int16_t bValue,
                                 const int8_t bExp, const bool bSigned);

uint8_t getScaledIPMIValue(const double value, const double max,
                           const double min);
} // namespace ipmi
