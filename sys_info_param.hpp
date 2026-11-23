#pragma once

/**
 * @file sys_info_param.hpp
 * @brief System Info 命令的声明。
 */
/*  @file sys_info_param.hpp
 *  @brief System Info 命令的声明。 */

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <tuple>

/**
 * Key-value store for string-type system info parameters.
 */
/*  用于字符串类型系统信息参数的键值存储（Key-value store）。 */
class SysInfoParamStoreIntf
{
  public:
    virtual ~SysInfoParamStoreIntf() {}

    /**
     * Returns true if parameter is found. If and only if s is non-null,
     * invokes the parameter's callback and writes the value.
     *
     * @param[in] paramSelector - the key to lookup.
     * @return tuple of bool and string, true if parameter is found and
     * string set accordingly.
     */
    /*  如果找到参数则返回 true。仅当 s 非空时，
     *  才会调用该参数的回调并写入值。
     *  @param[in] paramSelector - 要查找的键。
     *  @return 由 bool 和 string 组成的 tuple，找到参数时为 true，
     *  并相应地设置字符串。 */
    virtual std::tuple<bool, std::string> lookup(
        uint8_t paramSelector) const = 0;

    /**
     * Update a parameter by its code with a string value.
     *
     * @param[in] paramSelector - the key to update.
     * @param[in] s - the value to set.
     */
    /*  通过参数代码用字符串值更新参数。
     *  @param[in] paramSelector - 要更新的键。
     *  @param[in] s - 要设置的值。 */
    virtual void update(uint8_t paramSelector, const std::string& s) = 0;

    /**
     * Update a parameter by its code with a callback that is called to retrieve
     * its value whenever called. Callback must be idempotent, as it may be
     * called multiple times by the host to retrieve the parameter by chunks.
     *
     * @param[in] paramSelector - the key to update.
     * @param[in] callback - the callback to use for parameter retrieval.
     */
    /*  通过参数代码用一个回调更新参数，每次调用该回调时都会
     *  检索其值。回调必须是幂等的，因为主机可能会多次调用它，
     *  以分块方式获取该参数。
     *  @param[in] paramSelector - 要更新的键。
     *  @param[in] callback - 用于检索参数的回调。 */
    virtual void update(uint8_t paramSelector,
                        const std::function<std::string()>& callback) = 0;

    /**
     * Store the encoding type for a parameter.
     *
     * @param[in] paramSelector - the key to update.
     * @param[in] encoding - the encoding value to store.
     */
    /*  存储某个参数的编码类型。
     *  @param[in] paramSelector - 要更新的键。
     *  @param[in] encoding - 要存储的编码值。 */
    virtual void updateEncoding(uint8_t paramSelector, uint8_t encoding) = 0;

    /**
     * Retrieve the encoding type for a parameter.
     * Returns 0 (ASCII) if no encoding has been stored for this parameter.
     *
     * @param[in] paramSelector - the key to lookup.
     * @return the encoding value, or 0 if not set.
     */
    /*  检索某个参数的编码类型。
     *  如果该参数未存储任何编码，则返回 0（ASCII）。
     *  @param[in] paramSelector - 要查找的键。
     *  @return 编码值，未设置时为 0。 */
    virtual uint8_t lookupEncoding(uint8_t paramSelector) const = 0;

    // TODO: Store "read-only" flag for each parameter.
    // TODO: Function to erase a parameter?
    // TODO：为每个参数存储 "read-only"（只读）标志。
    // TODO：提供擦除参数的函数？
};

/**
 * Implement the system info parameters store as a map of callbacks.
 */
/*  将系统信息参数存储实现为回调的映射（map）。 */
class SysInfoParamStore : public SysInfoParamStoreIntf
{
  public:
    std::tuple<bool, std::string> lookup(uint8_t paramSelector) const override;
    void update(uint8_t paramSelector, const std::string& s) override;
    void update(uint8_t paramSelector,
                const std::function<std::string()>& callback) override;
    void updateEncoding(uint8_t paramSelector, uint8_t encoding) override;
    uint8_t lookupEncoding(uint8_t paramSelector) const override;

  private:
    std::map<uint8_t, std::function<std::string()>> params;
    std::map<uint8_t, uint8_t> encodings;
};
