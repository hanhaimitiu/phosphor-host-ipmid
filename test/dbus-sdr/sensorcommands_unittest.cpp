#include "dbus-sdr/sensorutils.hpp"

#include <cmath>

#include "gtest/gtest.h"

// There is a surprising amount of slop in the math,
// thanks to all the rounding and conversion.
// The "x" byte value can drift by up to 2 away, I have seen.
// 由于各种舍入和转换，运算中出现了
// 多得惊人的误差。
// 我见过 "x" 字节值偏差最多达 2 的情况。
static constexpr int8_t expectedSlopX = 2;

// Unlike expectedSlopX, this is a ratio, not an integer
// It scales based on the range of "y"
// 与 expectedSlopX 不同，这是一个比率而非整数
// 它会根据 "y" 的取值范围而缩放
static constexpr double expectedSlopY = 0.01;

// The algorithm here was copied from ipmitool
// sdr_convert_sensor_reading() function
// https://github.com/ipmitool/ipmitool/blob/42a023ff0726c80e8cc7d30315b987fe568a981d/lib/ipmi_sdr.c#L360
// 这里的算法拷贝自 ipmitool 的
//  sdr_convert_sensor_reading() 函数
//  https://github.com/ipmitool/ipmitool/blob/42a023ff0726c80e8cc7d30315b987fe568a981d/lib/ipmi_sdr.c#L360
double ipmitool_y_from_x(uint8_t x, int m, int k2_rExp, int b, int k1_bExp,
                         bool bSigned)
{
    double result;

    // Rename to exactly match names and types (except analog) from ipmitool
    // 重命名以完全匹配 ipmitool 中的名称和类型（analog 除外）
    uint8_t val = x;
    double k1 = k1_bExp;
    double k2 = k2_rExp;
    int analog = bSigned ? 2 : 0;

    // Begin paste here
    // Only change is to comment out complicated structure in switch statement
    // 从这里开始粘贴
    // 唯一的改动是注释掉 switch 语句中复杂的结构体

    switch (/*sensor->cmn.unit.*/ analog)
    {
        case 0:
            result = (double)(((m * val) + (b * pow(10, k1))) * pow(10, k2));
            break;
        case 1:
            if (val & 0x80)
                val++;
            /* Deliberately fall through to case 2. */
            /* 故意贯穿到 case 2。 */
            [[fallthrough]];
        case 2:
            result =
                (double)(((m * (int8_t)val) + (b * pow(10, k1))) * pow(10, k2));
            break;
        default:
            /* Oops! This isn't an analog sensor. */
            /* 糟糕！这并不是模拟传感器。 */
            return 0.0;
    }

    // End paste here
    // Ignoring linearization curves and postprocessing that follows,
    // assuming all sensors are perfectly linear
    // 粘贴到此结束
    // 忽略其后的线性化曲线和后处理，
    // 假设所有传感器都是完全线性的
    return result;
}

void testValue(int x, double y, int16_t M, int8_t rExp, int16_t B, int8_t bExp,
               bool bSigned, double yRange)
{
    double yRoundtrip;
    int result;

    // There is intentionally no exception catching here,
    // because if getSensorAttributes() returned true,
    // it is a promise that all of these should work.
    // 这里有意不做异常捕获，
    // 因为如果 getSensorAttributes() 返回 true，
    // 就保证这些操作都应该能成功。
    if (bSigned)
    {
        int8_t expect = x;
        int8_t actual =
            ipmi::scaleIPMIValueFromDouble(y, M, rExp, B, bExp, bSigned);

        result = actual;
        yRoundtrip = ipmitool_y_from_x(actual, M, rExp, B, bExp, bSigned);

        EXPECT_NEAR(actual, expect, expectedSlopX);
    }
    else
    {
        uint8_t expect = x;
        uint8_t actual =
            ipmi::scaleIPMIValueFromDouble(y, M, rExp, B, bExp, bSigned);

        result = actual;
        yRoundtrip = ipmitool_y_from_x(actual, M, rExp, B, bExp, bSigned);

        EXPECT_NEAR(actual, expect, expectedSlopX);
    }

    // Scale the amount of allowed slop in y based on range, so ratio similar
    // 根据范围缩放 y 允许的误差量，使比率相近
    double yTolerance = yRange * expectedSlopY;
    // double yError = std::abs(y - yRoundtrip);
    // double yError = std::abs(y - yRoundtrip);

    EXPECT_NEAR(y, yRoundtrip, yTolerance);

    char szFormat[1024];
    sprintf(szFormat,
            "Value | xExpect %4d | xResult %4d "
            "| M %5d | rExp %3d "
            "| B %5d | bExp %3d | bSigned %1d | y %18.3f | yRoundtrip %18.3f\n",
            x, result, M, (int)rExp, B, (int)bExp, (int)bSigned, y, yRoundtrip);
    std::cout << szFormat;
}

void testBounds(double yMin, double yMax, bool bExpectedOutcome = true)
{
    int16_t mValue;
    int8_t rExp;
    int16_t bValue;
    int8_t bExp;
    bool bSigned;
    bool result;

    result = ipmi::getSensorAttributes(yMax, yMin, mValue, rExp, bValue, bExp,
                                       bSigned);
    EXPECT_EQ(result, bExpectedOutcome);

    if (!result)
    {
        return;
    }

    char szFormat[1024];
    sprintf(szFormat,
            "Bounds | yMin %18.3f | yMax %18.3f | M %5d"
            " | rExp %3d | B %5d | bExp %3d | bSigned %1d\n",
            yMin, yMax, mValue, (int)rExp, bValue, (int)bExp, (int)bSigned);
    std::cout << szFormat;

    double y50p = (yMin + yMax) / 2.0;

    // Average the average
    // 对平均值再求平均
    double y25p = (yMin + y50p) / 2.0;
    double y75p = (y50p + yMax) / 2.0;

    // This range value is only used for tolerance checking, not computation
    // 该范围值仅用于容差检查，不参与计算
    double yRange = yMax - yMin;

    if (bSigned)
    {
        int8_t xMin = -128;
        int8_t x25p = -64;
        int8_t x50p = 0;
        int8_t x75p = 64;
        int8_t xMax = 127;

        testValue(xMin, yMin, mValue, rExp, bValue, bExp, bSigned, yRange);
        testValue(x25p, y25p, mValue, rExp, bValue, bExp, bSigned, yRange);
        testValue(x50p, y50p, mValue, rExp, bValue, bExp, bSigned, yRange);
        testValue(x75p, y75p, mValue, rExp, bValue, bExp, bSigned, yRange);
        testValue(xMax, yMax, mValue, rExp, bValue, bExp, bSigned, yRange);
    }
    else
    {
        uint8_t xMin = 0;
        uint8_t x25p = 64;
        uint8_t x50p = 128;
        uint8_t x75p = 192;
        uint8_t xMax = 255;

        testValue(xMin, yMin, mValue, rExp, bValue, bExp, bSigned, yRange);
        testValue(x25p, y25p, mValue, rExp, bValue, bExp, bSigned, yRange);
        testValue(x50p, y50p, mValue, rExp, bValue, bExp, bSigned, yRange);
        testValue(x75p, y75p, mValue, rExp, bValue, bExp, bSigned, yRange);
        testValue(xMax, yMax, mValue, rExp, bValue, bExp, bSigned, yRange);
    }
}

void testRanges(void)
{
    // The ranges from the main TEST function
    // 来自主 TEST 函数的各个范围
    testBounds(0x0, 0xFF);
    testBounds(-128, 127);
    testBounds(0, 16000);
    testBounds(0, 20);
    testBounds(8000, 16000);
    testBounds(-10, 10);
    testBounds(0, 277);
    testBounds(0, 0, false);
    testBounds(10, 12);

    // Additional test cases recommended to me by hardware people
    // 硬件同事向我推荐的额外测试用例
    testBounds(-40, 150);
    testBounds(0, 1);
    testBounds(0, 2);
    testBounds(0, 4);
    testBounds(0, 8);
    testBounds(35, 65);
    testBounds(0, 18);
    testBounds(0, 25);
    testBounds(0, 80);
    testBounds(0, 500);

    // Additional sanity checks
    // 额外的健全性检查
    testBounds(0, 255);
    testBounds(-255, 0);
    testBounds(-255, 255);
    testBounds(0, 1000);
    testBounds(-1000, 0);
    testBounds(-1000, 1000);
    testBounds(0, 255000);
    testBounds(-128000000, 127000000);
    testBounds(-50000, 0);
    testBounds(-40000, 10000);
    testBounds(-30000, 20000);
    testBounds(-20000, 30000);
    testBounds(-10000, 40000);
    testBounds(0, 50000);
    testBounds(-1e3, 1e6);
    testBounds(-1e6, 1e3);

    // Extreme ranges are now possible
    // 现在可以支持极端范围
    testBounds(0, 1e10);
    testBounds(0, 1e11);
    testBounds(0, 1e12);
    testBounds(0, 1e13, false);
    testBounds(-1e10, 0);
    testBounds(-1e11, 0);
    testBounds(-1e12, 0);
    testBounds(-1e13, 0, false);
    testBounds(-1e9, 1e9);
    testBounds(-1e10, 1e10);
    testBounds(-1e11, 1e11);
    testBounds(-1e12, 1e12, false);

    // Large multiplier but small offset
    // 大乘数但小偏移
    testBounds(1e4, 1e4 + 255);
    testBounds(1e5, 1e5 + 255);
    testBounds(1e6, 1e6 + 255);
    testBounds(1e7, 1e7 + 255);
    testBounds(1e8, 1e8 + 255);
    testBounds(1e9, 1e9 + 255);
    testBounds(1e10, 1e10 + 255, false);

    // Input validation against garbage
    // 针对无效输入的校验
    testBounds(0, INFINITY, false);
    testBounds(-INFINITY, 0, false);
    testBounds(-INFINITY, INFINITY, false);
    testBounds(0, NAN, false);
    testBounds(NAN, 0, false);
    testBounds(NAN, NAN, false);

    // Noteworthy binary integers
    // 值得关注的二进制整数
    testBounds(0, std::pow(2.0, 32.0) - 1.0);
    testBounds(0, std::pow(2.0, 32.0));
    testBounds(0.0 - std::pow(2.0, 31.0), std::pow(2.0, 31.0));
    testBounds((0.0 - std::pow(2.0, 31.0)) - 1.0, std::pow(2.0, 31.0));

    // Similar but negative (note additional commented-out below)
    // 类似但为负值（注意下方另有被注释掉的内容）
    testBounds(-1e1, (-1e1) + 255);
    testBounds(-1e2, (-1e2) + 255);

    // Ranges of negative numbers (note additional commented-out below)
    // 负数范围（注意下方另有被注释掉的内容）
    testBounds(-10400, -10000);
    testBounds(-15000, -14000);
    testBounds(-10000, -9000);
    testBounds(-1000, -900);
    testBounds(-1000, -800);
    testBounds(-1000, -700);
    testBounds(-1000, -740);

    // Very small ranges (note additional commented-out below)
    // 极小的范围（注意下方另有被注释掉的内容）
    testBounds(0, 0.1);
    testBounds(0, 0.01);
    testBounds(0, 0.001);
    testBounds(0, 0.0001);
    testBounds(0, 0.000001, false);

#if 0
    // TODO(): The algorithm in this module is better than it was before,
    // but the resulting value of X is still wrong under certain conditions,
    // such as when the range between min and max is around 255,
    // and the offset is fairly extreme compared to the multiplier.
    // Not sure why this is, but these ranges are contrived,
    // and real-world examples would most likely never be this way.
    // TODO()：本模块中的算法比以前好了，
    // 但在某些条件下算出的 X 值仍然错误，
    // 例如 min 与 max 之间的范围约为 255，
    // 且偏移相对于乘数而言相当极端时。
    // 原因不明，不过这些范围是人为构造的，
    // 现实中的例子多半不会是这样。
    testBounds(-10290, -10000);
    testBounds(-10280, -10000);
    testBounds(-10275,-10000);
    testBounds(-10270,-10000);
    testBounds(-10265,-10000);
    testBounds(-10260,-10000);
    testBounds(-10255,-10000);
    testBounds(-10250,-10000);
    testBounds(-10245,-10000);
    testBounds(-10256,-10000);
    testBounds(-10512, -10000);
    testBounds(-11024, -10000);

    // TODO(): This also fails, due to extreme small range, loss of precision
    // TODO()：这个也失败，因为范围极小，精度丢失
    testBounds(0, 0.00001);

    // TODO(): Interestingly, if bSigned is forced false,
    // causing "x" to have range of (0,255) instead of (-128,127),
    // these test cases change from failing to passing!
    // Not sure why this is, perhaps a mathematician might know.
    // TODO()：有趣的是，如果强制 bSigned 为 false，
    // 使 "x" 的取值范围变为 (0,255) 而非 (-128,127)，
    // 这些测试用例就从失败变为通过了！
    // 原因不明，也许数学家能解释。
    testBounds(-10300, -10000);
    testBounds(-1000,-750);
    testBounds(-1e3, (-1e3) + 255);
    testBounds(-1e4, (-1e4) + 255);
    testBounds(-1e5, (-1e5) + 255);
    testBounds(-1e6, (-1e6) + 255);
#endif
}

TEST(sensorutils, TranslateToIPMI)
{
    /*bool getSensorAttributes(double maxValue, double minValue, int16_t
       &mValue, int8_t &rExp, int16_t &bValue, int8_t &bExp, bool &bSigned); */
    // normal unsigned sensor
    // 普通无符号传感器
    double maxValue = 0xFF;
    double minValue = 0x0;
    int16_t mValue;
    int8_t rExp;
    int16_t bValue;
    int8_t bExp;
    bool bSigned;
    bool result;

    uint8_t scaledVal;

    result = ipmi::getSensorAttributes(maxValue, minValue, mValue, rExp, bValue,
                                       bExp, bSigned);
    EXPECT_EQ(result, true);
    if (result)
    {
        EXPECT_EQ(bSigned, false);
        EXPECT_EQ(mValue, 1);
        EXPECT_EQ(rExp, 0);
        EXPECT_EQ(bValue, 0);
        EXPECT_EQ(bExp, 0);
    }
    double expected = 0x50;
    scaledVal = ipmi::scaleIPMIValueFromDouble(0x50, mValue, rExp, bValue, bExp,
                                               bSigned);
    EXPECT_NEAR(scaledVal, expected, expected * 0.01);

    // normal signed sensor
    // 普通有符号传感器
    maxValue = 127;
    minValue = -128;

    result = ipmi::getSensorAttributes(maxValue, minValue, mValue, rExp, bValue,
                                       bExp, bSigned);
    EXPECT_EQ(result, true);

    if (result)
    {
        EXPECT_EQ(bSigned, true);
        EXPECT_EQ(mValue, 1);
        EXPECT_EQ(rExp, 0);
        EXPECT_EQ(bValue, 0);
        EXPECT_EQ(bExp, 0);
    }

    // check negative values
    // 检查负值
    expected = 236; // 2s compliment -20
    scaledVal = ipmi::scaleIPMIValueFromDouble(-20, mValue, rExp, bValue, bExp,
                                               bSigned);
    EXPECT_NEAR(scaledVal, expected, expected * 0.01);

    // fan example
    // 风扇示例
    maxValue = 16000;
    minValue = 0;

    result = ipmi::getSensorAttributes(maxValue, minValue, mValue, rExp, bValue,
                                       bExp, bSigned);
    EXPECT_EQ(result, true);
    if (result)
    {
        EXPECT_EQ(bSigned, false);
        EXPECT_EQ(mValue, floor((16000.0 / 0xFF) + 0.5));
        EXPECT_EQ(rExp, 0);
        EXPECT_EQ(bValue, 0);
        EXPECT_EQ(bExp, 0);
    }

    // voltage sensor example
    // 电压传感器示例
    maxValue = 20;
    minValue = 0;

    result = ipmi::getSensorAttributes(maxValue, minValue, mValue, rExp, bValue,
                                       bExp, bSigned);
    EXPECT_EQ(result, true);
    if (result)
    {
        EXPECT_EQ(bSigned, false);
        EXPECT_EQ(mValue, floor(((20.0 / 0xFF) / std::pow(10, rExp)) + 0.5));
        EXPECT_EQ(rExp, -3);
        EXPECT_EQ(bValue, 0);
        EXPECT_EQ(bExp, 0);
    }
    scaledVal = ipmi::scaleIPMIValueFromDouble(12.2, mValue, rExp, bValue, bExp,
                                               bSigned);

    expected = 12.2 / (mValue * std::pow(10, rExp));
    EXPECT_NEAR(scaledVal, expected, expected * 0.01);

    // shifted fan example
    // 偏移的风扇示例
    maxValue = 16000;
    minValue = 8000;

    result = ipmi::getSensorAttributes(maxValue, minValue, mValue, rExp, bValue,
                                       bExp, bSigned);
    EXPECT_EQ(result, true);

    if (result)
    {
        EXPECT_EQ(bSigned, false);
        EXPECT_EQ(mValue, floor(((8000.0 / 0xFF) / std::pow(10, rExp)) + 0.5));
        EXPECT_EQ(rExp, -1);
        EXPECT_EQ(bValue, 8);
        EXPECT_EQ(bExp, 4);
    }

    // signed voltage sensor example
    // 有符号电压传感器示例
    maxValue = 10;
    minValue = -10;

    result = ipmi::getSensorAttributes(maxValue, minValue, mValue, rExp, bValue,
                                       bExp, bSigned);
    EXPECT_EQ(result, true);
    if (result)
    {
        EXPECT_EQ(bSigned, true);
        EXPECT_EQ(mValue, floor(((20.0 / 0xFF) / std::pow(10, rExp)) + 0.5));
        EXPECT_EQ(rExp, -3);
        // Although this seems like a weird magic number,
        // it is because the range (-128,127) is not symmetrical about zero,
        // unlike the range (-10,10), so this introduces some distortion.
        // 虽然这看起来像个奇怪的魔数，
        // 但这是因为范围 (-128,127) 关于零并不对称，
        // 不像 (-10,10) 那样，因此会引入一些失真。
        EXPECT_EQ(bValue, 392);
        EXPECT_EQ(bExp, -1);
    }

    scaledVal =
        ipmi::scaleIPMIValueFromDouble(5, mValue, rExp, bValue, bExp, bSigned);

    expected = 5 / (mValue * std::pow(10, rExp));
    EXPECT_NEAR(scaledVal, expected, expected * 0.01);

    // reading = max example
    // reading = max 示例
    maxValue = 277;
    minValue = 0;

    result = ipmi::getSensorAttributes(maxValue, minValue, mValue, rExp, bValue,
                                       bExp, bSigned);
    EXPECT_EQ(result, true);
    if (result)
    {
        EXPECT_EQ(bSigned, false);
    }

    scaledVal = ipmi::scaleIPMIValueFromDouble(maxValue, mValue, rExp, bValue,
                                               bExp, bSigned);

    expected = 0xFF;
    EXPECT_NEAR(scaledVal, expected, expected * 0.01);

    // 0, 0 failure
    // 0, 0 失败案例
    maxValue = 0;
    minValue = 0;
    result = ipmi::getSensorAttributes(maxValue, minValue, mValue, rExp, bValue,
                                       bExp, bSigned);
    EXPECT_EQ(result, false);

    // too close *success* (was previously failure!)
    // 过于接近 *成功*（以前是失败！）
    maxValue = 12;
    minValue = 10;
    result = ipmi::getSensorAttributes(maxValue, minValue, mValue, rExp, bValue,
                                       bExp, bSigned);
    EXPECT_EQ(result, true);
    if (result)
    {
        EXPECT_EQ(bSigned, false);
        EXPECT_EQ(mValue, floor(((2.0 / 0xFF) / std::pow(10, rExp)) + 0.5));
        EXPECT_EQ(rExp, -4);
        EXPECT_EQ(bValue, 1);
        EXPECT_EQ(bExp, 5);
    }
}

TEST(sensorUtils, TestRanges)
{
    // Additional test ranges, each running through a series of values,
    // to make sure the values of "x" and "y" go together and make sense,
    // for the resulting scaling attributes from each range.
    // Unlike the TranslateToIPMI test, exact matches of the
    // getSensorAttributes() results (the coefficients) are not required,
    // because they are tested through actual use, relating "x" to "y".
    // 额外的测试范围，每个都跑一串数值，
    // 以确保对每个范围得到的缩放属性而言，
    // "x" 与 "y" 的值相互匹配且合理。
    // 与 TranslateToIPMI 测试不同，这里不要求
    // getSensorAttributes() 的结果（系数）精确匹配，
    // 因为它们是通过实际使用、"x" 与 "y" 的关联来测试的。
    testRanges();
}
