
/**
 * @file sensorutils.cpp
 * @brief 传感器工具函数（读取阈值/单位等）。
 */
/*  @file sensorutils.cpp
 *  @brief 传感器工具函数（读取阈值/单位等）。 */
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

#include "dbus-sdr/sensorutils.hpp"

#include <phosphor-logging/lg2.hpp>

#include <algorithm>
#include <cmath>

namespace ipmi
{

// Helper function to avoid repeated complicated expression
// 辅助函数，避免重复的复杂表达式
static bool baseInRange(double base)
{
    auto min10 = static_cast<double>(minInt10);
    auto max10 = static_cast<double>(maxInt10);

    return ((base >= min10) && (base <= max10));
}

// Helper function for internal use by getSensorAttributes()
// Ensures floating-point "base" is within bounds,
// and adjusts integer exponent "expShift" accordingly.
// To minimize data loss when later truncating to integer,
// the floating-point "base" will be as large as possible,
// but still within the bounds (minInt10,maxInt10).
// The bounds of "expShift" are (minInt4,maxInt4).
// Consider this equation: n = base * (10.0 ** expShift)
// This function will try to maximize "base",
// adjusting "expShift" to keep the value "n" unchanged,
// while keeping base and expShift within bounds.
// Returns true if successful, modifies values in-place
// 供 getSensorAttributes() 内部使用的辅助函数。
// 确保浮点 "base" 在边界范围内，
// 并相应地调整整数指数 "expShift"。
// 为了在后续截断为整数时尽量减少数据损失，
// 浮点 "base" 会尽可能大，
// 但仍保持在边界 (minInt10, maxInt10) 内。
// "expShift" 的边界是 (minInt4, maxInt4)。
// 考虑这个等式：n = base * (10.0 ** expShift)
// 本函数会尝试最大化 "base"，
// 调整 "expShift" 以保持 "n" 的值不变，
// 同时让 base 和 expShift 都保持在边界内。
// 成功时返回 true，并就地修改这些值
static bool scaleFloatExp(double& base, int8_t& expShift)
{
    // Comparing with zero should be OK, zero is special in floating-point
    // If base is exactly zero, no adjustment of the exponent is necessary
    // 与零比较应该没问题，零在浮点数中是特殊的。
    // 如果 base 恰好为零，则无需调整指数
    if (base == 0.0)
    {
        return true;
    }

    // As long as base value is within allowed range, expand precision
    // This will help to avoid loss when later rounding to integer
    // 只要 base 值在允许范围内，就扩展精度。
    // 这有助于避免后续舍入为整数时的损失
    while (baseInRange(base))
    {
        if (expShift <= minInt4)
        {
            // Already at the minimum expShift, can not decrement it more
            // 已处于最小 expShift，无法再减小它
            break;
        }

        // Multiply by 10, but shift decimal point to the left, no net change
        // 乘以 10，但将小数点左移，净值不变
        base *= 10.0;
        --expShift;
    }

    // As long as base value is *not* within range, shrink precision
    // This will pull base value closer to zero, thus within range
    // 只要 base 值不在范围内，就缩小精度。
    // 这会使 base 值更接近零，从而进入范围
    while (!(baseInRange(base)))
    {
        if (expShift >= maxInt4)
        {
            // Already at the maximum expShift, can not increment it more
            // 已处于最大 expShift，无法再增大它
            break;
        }

        // Divide by 10, but shift decimal point to the right, no net change
        // 除以 10，但将小数点右移，净值不变
        base /= 10.0;
        ++expShift;
    }

    // If the above loop was not able to pull it back within range,
    // the base value is beyond what expShift can represent, return false.
    // 如果上述循环无法把 base 拉回范围内，
    // 说明 base 值超出了 expShift 可表示的范围，返回 false。
    return baseInRange(base);
}

// Helper function for internal use by getSensorAttributes()
// Ensures integer "ibase" is no larger than necessary,
// by normalizing it so that the decimal point shift is in the exponent,
// whenever possible.
// This provides more consistent results,
// as many equivalent solutions are collapsed into one consistent solution.
// If integer "ibase" is a clean multiple of 10,
// divide it by 10 (this is lossless), so it is closer to zero.
// Also modify floating-point "dbase" at the same time,
// as both integer and floating-point base share the same expShift.
// Example: (ibase=300, expShift=2) becomes (ibase=3, expShift=4)
// because the underlying value is the same: 200*(10**2) == 2*(10**4)
// Always successful, modifies values in-place
// 供 getSensorAttributes() 内部使用的辅助函数。
// 确保整数 "ibase" 不会不必要地过大，
// 方法是归一化它，使小数点位移体现在指数中，
// 只要有可能。
// 这样可得到更一致的结果，
// 因为许多等价解被归约为一个一致的解。
// 如果整数 "ibase" 是 10 的整数倍，
// 就除以 10（这是无损的），使其更接近零。
// 同时也要修改浮点 "dbase"，
// 因为整数和浮点基数共享同一个 expShift。
// 示例：(ibase=300, expShift=2) 变为 (ibase=3, expShift=4)
// 因为底层数值相同：200*(10**2) == 2*(10**4)
// 总是成功，并就地修改这些值
static void normalizeIntExp(int16_t& ibase, int8_t& expShift, double& dbase)
{
    for (;;)
    {
        // If zero, already normalized, ensure exponent also zero
        // 如果为零，则已经归一化，同时确保指数也为零
        if (ibase == 0)
        {
            expShift = 0;
            break;
        }

        // If not cleanly divisible by 10, already normalized
        // 如果不能被 10 整除，则已经归一化
        if ((ibase % 10) != 0)
        {
            break;
        }

        // If exponent already at max, already normalized
        // 如果指数已达最大值，则已经归一化
        if (expShift >= maxInt4)
        {
            break;
        }

        // Bring values closer to zero, correspondingly shift exponent,
        // without changing the underlying number that this all represents,
        // similar to what is done by scaleFloatExp().
        // The floating-point base must be kept in sync with the integer base,
        // as both floating-point and integer share the same exponent.
        // 使数值更接近零，并相应地移动指数，
        // 而不改变这一切所代表的实际数值，
        // 类似于 scaleFloatExp() 所做的。
        // 浮点基数必须与整数基数保持同步，
        // 因为浮点和整数共享同一个指数。
        ibase /= 10;
        dbase /= 10.0;
        ++expShift;
    }
}

// The IPMI equation:
// y = (Mx + (B * 10^(bExp))) * 10^(rExp)
// Section 36.3 of this document:
// https://www.intel.com/content/dam/www/public/us/en/documents/product-briefs/ipmi-second-gen-interface-spec-v2-rev1-1.pdf
//
// The goal is to exactly match the math done by the ipmitool command,
// at the other side of the interface:
// https://github.com/ipmitool/ipmitool/blob/42a023ff0726c80e8cc7d30315b987fe568a981d/lib/ipmi_sdr.c#L360
//
// To use with Wolfram Alpha, make all variables single letters
// bExp becomes E, rExp becomes R
// https://www.wolframalpha.com/input/?i=y%3D%28%28M*x%29%2B%28B*%2810%5EE%29%29%29*%2810%5ER%29
// IPMI 等式：
//  y = (Mx + (B * 10^(bExp))) * 10^(rExp)
// 本文档 36.3 节：
//  https://www.intel.com/content/dam/www/public/us/en/documents/product-briefs/ipmi-second-gen-interface-spec-v2-rev1-1.pdf
//  目标是与接口另一端的 ipmitool 命令所做的数学运算
//  完全一致：
//  https://github.com/ipmitool/ipmitool/blob/42a023ff0726c80e8cc7d30315b987fe568a981d/lib/ipmi_sdr.c#L360
//  要在 Wolfram Alpha 中使用，请把所有变量改为单字母
//  bExp 变为 E，rExp 变为 R
//  https://www.wolframalpha.com/input/?i=y%3D%28%28M*x%29%2B%28B*%2810%5EE%29%29%29*%2810%5ER%29
bool getSensorAttributes(const double max, const double min, int16_t& mValue,
                         int8_t& rExp, int16_t& bValue, int8_t& bExp,
                         bool& bSigned)
{
    if (!(std::isfinite(min)))
    {
        lg2::error("getSensorAttributes: Min value is unusable");
        return false;
    }
    if (!(std::isfinite(max)))
    {
        lg2::error("getSensorAttributes: Max value is unusable");
        return false;
    }

    // Because NAN has already been tested for, this comparison works
    // 由于已经检查过 NAN，这个比较是有效的
    if (max <= min)
    {
        lg2::error("getSensorAttributes: Max must be greater than min");
        return false;
    }

    // Given min and max, we must solve for M, B, bExp, rExp
    // y comes in from D-Bus (the actual sensor reading)
    // x is calculated from y by scaleIPMIValueFromDouble() below
    // If y is min, x should equal = 0 (or -128 if signed)
    // If y is max, x should equal 255 (or 127 if signed)
    // 给定最小值和最大值，我们必须求解 M、B、bExp、rExp
    // y 来自 D-Bus（实际的传感器读数）
    // x 由下面的 scaleIPMIValueFromDouble() 根据 y 计算得出
    // 如果 y 为最小值，x 应等于 0（有符号时为 -128）
    // 如果 y 为最大值，x 应等于 255（有符号时为 127）
    double fullRange = max - min;
    double lowestX;

    rExp = 0;
    bExp = 0;

    // TODO(): The IPMI document is ambiguous, as to whether
    // the resulting byte should be signed or unsigned,
    // essentially leaving it up to the caller.
    // The document just refers to it as "raw reading",
    // or "byte of reading", without giving further details.
    // Previous code set it signed if min was less than zero,
    // so I'm sticking with that, until I learn otherwise.
    // TODO()：IPMI 文档对于结果字节是否
    // 应为有符号还是无符号表述模糊，
    // 实际上交由调用方决定。
    // 文档只是称其为 "raw reading"
    // 或 "byte of reading"，没有给出更多细节。
    // 如果 min 小于零，之前的代码将其设为有符号，
    // 在了解到其他情况之前，我沿用这一做法。
    if (min < 0.0)
    {
        // TODO(): It would be worth experimenting with the range (-127,127),
        // instead of the range (-128,127), because this
        // would give good symmetry around zero, and make results look better.
        // Divide by 254 instead of 255, and change -128 to -127 elsewhere.
        // TODO()：值得试验使用范围 (-127,127)，
        // 而不是范围 (-128,127)，因为这会在零附近
        // 提供良好的对称性，让结果看起来更好。
        // 除以 254 而不是 255，并把其他地方的 -128 改为 -127。
        bSigned = true;
        lowestX = -128.0;
    }
    else
    {
        bSigned = false;
        lowestX = 0.0;
    }

    // Step 1: Set y to (max - min), set x to 255, set B to 0, solve for M
    // This works, regardless of signed or unsigned,
    // because total range is the same.
    // 第 1 步：令 y = (max - min)，x = 255，B = 0，求解 M
    // 无论有符号还是无符号都有效，
    // 因为总范围相同。
    double dM = fullRange / 255.0;

    // Step 2: Constrain M, and set rExp accordingly
    // 第 2 步：约束 M，并相应地设置 rExp
    if (!(scaleFloatExp(dM, rExp)))
    {
        lg2::error(
            "getSensorAttributes: Multiplier range exceeds scale (M={DM}, "
            "rExp={REXP})",
            "DM", dM, "REXP", rExp);
        return false;
    }

    mValue = static_cast<int16_t>(std::round(dM));

    normalizeIntExp(mValue, rExp, dM);

    // The multiplier can not be zero, for obvious reasons
    // 乘数显然不能为零
    if (mValue == 0)
    {
        lg2::error("getSensorAttributes: Multiplier range below scale");
        return false;
    }

    // Step 3: set y to min, set x to min, keep M and rExp, solve for B
    // If negative, x will be -128 (the most negative possible byte), not 0
    // 第 3 步：令 y 为 min，x 为 min，保持 M 和 rExp，求解 B
    // 如果为负，x 将是 -128（最负的可能字节），而不是 0

    // Solve the IPMI equation for B, instead of y
    // https://www.wolframalpha.com/input/?i=solve+y%3D%28%28M*x%29%2B%28B*%2810%5EE%29%29%29*%2810%5ER%29+for+B
    // B = 10^(-rExp - bExp) (y - M 10^rExp x)
    // TODO(): Compare with this alternative solution from SageMathCell
    // https://sagecell.sagemath.org/?z=eJyrtC1LLNJQr1TX5KqAMCuATF8I0xfIdIIwnYDMIteKAggPxAIKJMEFkiACxfk5Zaka0ZUKtrYKGhq-CloKFZoK2goaTkCWhqGBgpaWAkilpqYmQgBklmasjoKTJgDAECTH&lang=sage&interacts=eJyLjgUAARUAuQ==
    // 针对 B 而不是 y 求解 IPMI 方程
    //  https://www.wolframalpha.com/input/?i=solve+y%3D%28%28M*x%29%2B%28B*%2810%5EE%29%29%29*%2810%5ER%29+for+B
    //  B = 10^(-rExp - bExp) (y - M 10^rExp x)
    //  TODO()：与 SageMathCell 的这种替代解进行比较
    //  https://sagecell.sagemath.org/?z=eJyrtC1LLNJQr1TX5KqAMCuATF8I0xfIdIIwnYDMIteKAggPxAIKJMEFkiACxfk5Zaka0ZUKtrYKGhq-CloKFZoK2goaTkCWhqGBgpaWAkilpqYmQgBklmasjoKTJgDAECTH&lang=sage&interacts=eJyLjgUAARUAuQ==
    double dB = std::pow(10.0, ((-rExp) - bExp)) *
                (min - ((dM * std::pow(10.0, rExp) * lowestX)));

    // Step 4: Constrain B, and set bExp accordingly
    // 第 4 步：约束 B，并相应地设置 bExp
    if (!(scaleFloatExp(dB, bExp)))
    {
        lg2::error(
            "getSensorAttributes: Offset range exceeds scale (B={DB}, "
            "bExp={BEXP}) exceeds multiplier scale (M={DM}, rExp={REXP})",
            "DB", dB, "BEXP", bExp, "DM", dM, "REXP", rExp);
        return false;
    }

    bValue = static_cast<int16_t>(std::round(dB));

    normalizeIntExp(bValue, bExp, dB);

    // Unlike the multiplier, it is perfectly OK for bValue to be zero
    // 与乘数不同，bValue 为零完全没有问题
    return true;
}

uint8_t scaleIPMIValueFromDouble(const double value, const int16_t mValue,
                                 const int8_t rExp, const int16_t bValue,
                                 const int8_t bExp, const bool bSigned)
{
    // Avoid division by zero below
    // 避免下面的除以零
    if (mValue == 0)
    {
        throw std::out_of_range("Scaling multiplier is uninitialized");
    }

    auto dM = static_cast<double>(mValue);
    auto dB = static_cast<double>(bValue);

    // Solve the IPMI equation for x, instead of y
    // https://www.wolframalpha.com/input/?i=solve+y%3D%28%28M*x%29%2B%28B*%2810%5EE%29%29%29*%2810%5ER%29+for+x
    // x = (10^(-rExp) (y - B 10^(rExp + bExp)))/M and M 10^rExp!=0
    // TODO(): Compare with this alternative solution from SageMathCell
    // https://sagecell.sagemath.org/?z=eJyrtC1LLNJQr1TX5KqAMCuATF8I0xfIdIIwnYDMIteKAggPxAIKJMEFkiACxfk5Zaka0ZUKtrYKGhq-CloKFZoK2goaTkCWhqGBgpaWAkilpqYmQgBklmasDlAlAMB8JP0=&lang=sage&interacts=eJyLjgUAARUAuQ==
    // 针对 x 而不是 y 求解 IPMI 方程
    //  https://www.wolframalpha.com/input/?i=solve+y%3D%28%28M*x%29%2B%28B*%2810%5EE%29%29%29*%2810%5ER%29+for+x
    //  x = (10^(-rExp) (y - B 10^(rExp + bExp)))/M 且 M 10^rExp!=0
    //  TODO()：与 SageMathCell 的这种替代解进行比较
    //  https://sagecell.sagemath.org/?z=eJyrtC1LLNJQr1TX5KqAMCuATF8I0xfIdIIwnYDMIteKAggPxAIKJMEFkiACxfk5Zaka0ZUKtrYKGhq-CloKFZoK2goaTkCWhqGBgpaWAkilpqYmQgBklmasDlAlAMB8JP0=&lang=sage&interacts=eJyLjgUAARUAuQ==
    double dX =
        (std::pow(10.0, -rExp) * (value - (dB * std::pow(10.0, rExp + bExp)))) /
        dM;

    auto scaledValue = static_cast<int32_t>(std::round(dX));

    int32_t minClamp;
    int32_t maxClamp;

    // Because of rounding and integer truncation of scaling factors,
    // sometimes the resulting byte is slightly out of range.
    // Still allow this, but clamp the values to range.
    // 由于缩放因子的舍入和整数截断，
    // 有时得到的字节会略微超出范围。
    // 仍然允许这种情况，但要把值钳制到范围内。
    if (bSigned)
    {
        minClamp = std::numeric_limits<int8_t>::lowest();
        maxClamp = std::numeric_limits<int8_t>::max();
    }
    else
    {
        minClamp = std::numeric_limits<uint8_t>::lowest();
        maxClamp = std::numeric_limits<uint8_t>::max();
    }

    auto clampedValue = std::clamp(scaledValue, minClamp, maxClamp);

    // This works for both signed and unsigned,
    // because it is the same underlying byte storage.
    // 这对有符号和无符号都有效，
    // 因为底层字节存储是相同的。
    return static_cast<uint8_t>(clampedValue);
}

uint8_t getScaledIPMIValue(const double value, const double max,
                           const double min)
{
    int16_t mValue = 0;
    int8_t rExp = 0;
    int16_t bValue = 0;
    int8_t bExp = 0;
    bool bSigned = false;

    bool result =
        getSensorAttributes(max, min, mValue, rExp, bValue, bExp, bSigned);
    if (!result)
    {
        throw std::runtime_error("Illegal sensor attributes");
    }

    return scaleIPMIValueFromDouble(value, mValue, rExp, bValue, bExp, bSigned);
}

} // namespace ipmi
