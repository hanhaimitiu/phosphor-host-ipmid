#pragma once

/**
 * @file settings.hpp
 * @brief phosphor-settingsd 封装的声明。
 */
/* @file settings.hpp
 * @brief phosphor-settingsd 封装的声明。 */

#include <sdbusplus/bus.hpp>

#include <string>
#include <tuple>

namespace settings
{

using Path = std::string;
using Service = std::string;
using Interface = std::string;

/** @class Objects
 *  @brief Fetch paths of settings d-bus objects of interest, upon construction
 */
/* @class Objects
 *  @brief 在构造时获取所需 settings d-bus 对象的路径 */
struct Objects
{
  public:
    /** @brief Constructor - fetch settings objects
     *
     * @param[in] bus - The Dbus bus object
     * @param[in] filter - A vector of settings interfaces the caller is
     *            interested in.
     */
    /* @brief 构造函数 - 获取 settings 对象
     * @param[in] bus - Dbus 总线对象
     * @param[in] filter - 调用方感兴趣的 settings 接口
     *            vector。 */
    Objects(sdbusplus::bus_t& bus, const std::vector<Interface>& filter);
    Objects(const Objects&) = default;
    Objects& operator=(const Objects&) = delete;
    Objects(Objects&&) = delete;
    Objects& operator=(Objects&&) = delete;
    ~Objects() = default;

    /** @brief Fetch d-bus service, given a path and an interface. The
     *         service can't be cached because mapper returns unique
     *         service names.
     *
     * @param[in] path - The Dbus object
     * @param[in] interface - The Dbus interface
     *
     * @return std::string - the dbus service
     */
    /* @brief 根据给定的路径和接口获取 d-bus 服务。
     *         该服务无法缓存，因为 mapper 返回的是
     *         唯一的服务名称。
     * @param[in] path - Dbus 对象
     * @param[in] interface - Dbus 接口
     * @return std::string - dbus 服务 */
    Service service(const Path& path, const Interface& interface) const;

    /** @brief map of settings objects */
    /* @brief settings 对象的 map */
    std::map<Interface, std::vector<Path>> map;

    /** @brief map of service objects */
    /* @brief service 对象的 map */
    std::map<Path, Service> serviceMap;

    /** @brief The Dbus bus object */
    /* @brief Dbus 总线对象 */
    sdbusplus::bus_t& bus;
};

} // namespace settings
