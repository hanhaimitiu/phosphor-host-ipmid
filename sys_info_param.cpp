
/**
 * @file sys_info_param.cpp
 * @brief System Info（系统信息参数）IPMI 命令的实现。
 */
/*  @file sys_info_param.cpp
 *  @brief System Info（系统信息参数）IPMI 命令的实现。 */
#include "sys_info_param.hpp"

std::tuple<bool, std::string> SysInfoParamStore::lookup(
    uint8_t paramSelector) const
{
    const auto iterator = params.find(paramSelector);
    if (iterator == params.end())
    {
        return std::make_tuple(false, "");
    }

    auto s = iterator->second();
    return std::make_tuple(true, s);
}

void SysInfoParamStore::update(uint8_t paramSelector, const std::string& s)
{
    // Add a callback that captures a copy of the string passed and returns it
    // when invoked.
    // 添加一个回调，捕获传入字符串的副本，并在
    // 调用时返回该副本。

    // clang-format off
    update(paramSelector, [s]() {
        return s;
    });
    // clang-format on
}

void SysInfoParamStore::update(uint8_t paramSelector,
                               const std::function<std::string()>& callback)
{
    params[paramSelector] = callback;
}

void SysInfoParamStore::updateEncoding(uint8_t paramSelector, uint8_t encoding)
{
    encodings[paramSelector] = encoding;
}

uint8_t SysInfoParamStore::lookupEncoding(uint8_t paramSelector) const
{
    const auto it = encodings.find(paramSelector);
    if (it == encodings.end())
    {
        return 0;
    }
    return it->second;
}
