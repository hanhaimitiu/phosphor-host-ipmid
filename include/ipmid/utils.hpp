#pragma once

/**
 * @file utils.hpp
 * @brief 公共 IPMI 工具：读 D-Bus 属性、FRU/传感器辅助等。
 */
/*  @file utils.hpp
 *  @brief 公共 IPMI 工具：读 D-Bus 属性、FRU/传感器辅助等。 */

#include <boost/system/error_code.hpp>
#include <ipmid/api-types.hpp>
#include <ipmid/message.hpp>
#include <ipmid/types.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/server.hpp>

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace ipmi
{

using namespace std::literals::chrono_literals;

constexpr auto ROOT = "/";
constexpr auto HOST_MATCH = "host0";

constexpr auto PROP_INTF = "org.freedesktop.DBus.Properties";
constexpr auto DELETE_INTERFACE = "xyz.openbmc_project.Object.Delete";

constexpr auto METHOD_GET = "Get";
constexpr auto METHOD_GET_ALL = "GetAll";
constexpr auto METHOD_SET = "Set";

/* Use a value of 5s which aligns with BT/KCS bridged timeouts, rather
 * than the default 25s D-Bus timeout. */
/* 使用 5 秒的值，与 BT/KCS 桥接超时保持一致，
 * 而不是默认的 25 秒 D-Bus 超时。 */
constexpr std::chrono::microseconds IPMI_DBUS_TIMEOUT = 5s;

/** @class ServiceCache
 *  @brief Caches lookups of service names from the object mapper.
 *  @details Most ipmi commands need to talk to other dbus daemons to perform
 *           their intended actions on the BMC. This usually means they will
 *           first look up the service name providing the interface they
 *           require. This class reduces the number of such calls by caching
 *           the lookup for a specific service.
 */
/* @class ServiceCache
 * @brief 缓存从 object mapper 查询服务名称的结果。
 * @details 大多数 ipmi 命令需要与 BMC 上的其他 dbus 守护进程通信
 *           以执行其预期操作。这通常意味着它们会先查找
 *           提供所需接口的服务名称。该类通过
 *           缓存特定服务的查找结果来减少
 *           此类调用的次数。 */
class ServiceCache
{
  public:
    /** @brief Creates a new service cache for the given interface
     *         and path.
     *
     *  @param[in] intf - The interface used for each lookup
     *  @param[in] path - The path used for each lookup
     */
    /* @brief 为给定的接口和路径
     * 创建新的服务缓存。
     * @param[in] intf - 每次查找使用的接口
     * @param[in] path - 每次查找使用的路径 */
    ServiceCache(const std::string& intf, const std::string& path);
    ServiceCache(std::string&& intf, std::string&& path);

    /** @brief Gets the service name from the cache or does in a
     *         lookup when invalid.
     *
     *  @param[in] bus - The bus associated with and used for looking
     *                   up the service.
     */
    /* @brief 从缓存中获取服务名称，若缓存无效
     * 则执行一次查找。
     * @param[in] bus - 与服务查找关联并用于查找
     *                   的总线。 */
    const std::string& getService(sdbusplus::bus_t& bus);

    /** @brief Invalidates the current service name */
    /* @brief 使当前服务名称失效 */
    void invalidate();

    /** @brief A wrapper around sdbusplus bus.new_method_call
     *
     *  @param[in] bus - The bus used for calling the method
     *  @param[in] intf - The interface containing the method
     *  @param[in] method - The method name
     *  @return The message containing the method call.
     */
    /* @brief 对 sdbusplus bus.new_method_call 的封装
     * @param[in] bus - 用于调用方法的总线
     * @param[in] intf - 包含该方法的接口
     * @param[in] method - 方法名
     * @return 包含该方法调用的 message。 */
    sdbusplus::message_t newMethodCall(sdbusplus::bus_t& bus, const char* intf,
                                       const char* method);

    /** @brief Check to see if the current cache is valid
     *
     * @param[in] bus - The bus used for the service lookup
     * @return True if the cache is valid false otherwise.
     */
    /* @brief 检查当前缓存是否有效
     * @param[in] bus - 用于服务查找的总线
     * @return 缓存有效时返回 true，否则返回 false。 */
    bool isValid(sdbusplus::bus_t& bus) const;

  private:
    /** @brief DBUS interface provided by the service */
    /* @brief 服务提供的 DBUS 接口 */
    const std::string intf;
    /** @brief DBUS path provided by the service */
    /* @brief 服务提供的 DBUS 路径 */
    const std::string path;
    /** @brief The name of the service if valid */
    /* @brief 服务名称（有效时） */
    std::optional<std::string> cachedService;
    /** @brief The name of the bus used in the service lookup */
    /* @brief 服务查找中使用的总线名称 */
    std::optional<std::string> cachedBusName;
};

/**
 * @brief Get the DBUS Service name for the input dbus path
 *
 * @param[in] bus - DBUS Bus Object
 * @param[in] intf - DBUS Interface
 * @param[in] path - DBUS Object Path
 *
 */
/* @brief 获取输入 dbus 路径对应的 DBUS 服务名称
 * @param[in] bus - DBUS Bus Object
 * @param[in] intf - DBUS Interface
 * @param[in] path - DBUS Object Path */
std::string getService(sdbusplus::bus_t& bus, const std::string& intf,
                       const std::string& path);

/** @brief Gets the dbus sub tree implementing the given interface.
 *  @param[in] bus - DBUS Bus Object.
 *  @param[in] interfaces - Dbus interface.
 *  @param[in] subtreePath - subtree from where the search should start.
 *  @param[in] depth - Search depth
 *  @return map of object path and service info.
 */
/* @brief 获取实现给定接口的 dbus 子树。
 * @param[in] bus - DBUS Bus Object。
 * @param[in] interfaces - Dbus interface。
 * @param[in] subtreePath - 搜索起始的子树。
 * @param[in] depth - 搜索深度
 * @return 对象路径与服务信息的 map。 */
ObjectTree getSubTree(sdbusplus::bus_t& bus, const InterfaceList& interface,
                      const std::string& subtreePath = ROOT, int32_t depth = 0);

/** @brief Gets the dbus object info implementing the given interface
 *         from the given subtree.
 *  @param[in] bus - DBUS Bus Object.
 *  @param[in] interface - Dbus interface.
 *  @param[in] subtreePath - subtree from where the search should start.
 *  @param[in] match - identifier for object.
 *  @return On success returns the object having objectpath and servicename.
 */
/* @brief 从给定子树中获取实现给定接口的
 *  dbus 对象信息。
 *  @param[in] bus - DBUS Bus Object。
 *  @param[in] interface - Dbus interface。
 *  @param[in] subtreePath - 搜索起始的子树。
 *  @param[in] match - 对象的标识符。
 *  @return 成功时返回含对象路径和服务名的对象。 */
DbusObjectInfo getDbusObject(
    sdbusplus::bus_t& bus, const std::string& interface,
    const std::string& subtreePath = ROOT, const std::string& match = {});

/** @brief Gets the value associated with the given object
 *         and the interface.
 *  @param[in] bus - DBUS Bus Object.
 *  @param[in] service - Dbus service name.
 *  @param[in] objPath - Dbus object path.
 *  @param[in] interface - Dbus interface.
 *  @param[in] property - name of the property.
 *  @return On success returns the value of the property.
 */
/* @brief 获取与给定对象和接口关联的
 *  值。
 *  @param[in] bus - DBUS Bus Object。
 *  @param[in] service - Dbus service name。
 *  @param[in] objPath - Dbus object path。
 *  @param[in] interface - Dbus interface。
 *  @param[in] property - 属性的名称。
 *  @return 成功时返回属性的值。 */
Value getDbusProperty(sdbusplus::bus_t& bus, const std::string& service,
                      const std::string& objPath, const std::string& interface,
                      const std::string& property,
                      std::chrono::microseconds timeout = IPMI_DBUS_TIMEOUT);

/** @brief Gets all the properties associated with the given object
 *         and the interface.
 *  @param[in] bus - DBUS Bus Object.
 *  @param[in] service - Dbus service name.
 *  @param[in] objPath - Dbus object path.
 *  @param[in] interface - Dbus interface.
 *  @return On success returns the map of name value pair.
 */
/* @brief 获取与给定对象和接口关联的
 *  所有属性。
 *  @param[in] bus - DBUS Bus Object。
 *  @param[in] service - Dbus service name。
 *  @param[in] objPath - Dbus object path。
 *  @param[in] interface - Dbus interface。
 *  @return 成功时返回名称-值对的 map。 */
PropertyMap getAllDbusProperties(
    sdbusplus::bus_t& bus, const std::string& service,
    const std::string& objPath, const std::string& interface,
    std::chrono::microseconds timeout = IPMI_DBUS_TIMEOUT);

/** @brief Gets all managed objects associated with the given object
 *         path and service.
 *  @param[in] bus - D-Bus Bus Object.
 *  @param[in] service - D-Bus service name.
 *  @param[in] objPath - D-Bus object path.
 *  @return On success returns the map of name value pair.
 */
/* @brief 获取与给定对象路径和服务关联的
 *  所有 managed objects。
 *  @param[in] bus - D-Bus Bus Object。
 *  @param[in] service - D-Bus service name。
 *  @param[in] objPath - D-Bus object path。
 *  @return 成功时返回名称-值对的 map。 */
ObjectValueTree getManagedObjects(sdbusplus::bus_t& bus,
                                  const std::string& service,
                                  const std::string& objPath);

/** @brief Sets the property value of the given object.
 *  @param[in] bus - DBUS Bus Object.
 *  @param[in] service - Dbus service name.
 *  @param[in] objPath - Dbus object path.
 *  @param[in] interface - Dbus interface.
 *  @param[in] property - name of the property.
 *  @param[in] value - value which needs to be set.
 */
/* @brief 设置给定对象的属性值。
 *  @param[in] bus - DBUS Bus Object。
 *  @param[in] service - Dbus service name。
 *  @param[in] objPath - Dbus object path。
 *  @param[in] interface - Dbus interface。
 *  @param[in] property - 属性的名称。
 *  @param[in] value - 要设置的值。 */
void setDbusProperty(sdbusplus::bus_t& bus, const std::string& service,
                     const std::string& objPath, const std::string& interface,
                     const std::string& property, const Value& value,
                     std::chrono::microseconds timeout = IPMI_DBUS_TIMEOUT);

/** @brief  Gets all the dbus objects from the given service root
 *          which matches the object identifier.
 *  @param[in] bus - DBUS Bus Object.
 *  @param[in] serviceRoot - Service root path.
 *  @param[in] interface - Dbus interface.
 *  @param[in] match - Identifier for a path.
 *  @returns map of object path and service info.
 */
/* @brief 从给定服务根中获取所有匹配对象
 *  标识符的 dbus 对象。
 *  @param[in] bus - DBUS Bus Object。
 *  @param[in] serviceRoot - Service root path。
 *  @param[in] interface - Dbus interface。
 *  @param[in] match - 路径的标识符。
 *  @returns 对象路径与服务信息的 map。 */
ObjectTree getAllDbusObjects(
    sdbusplus::bus_t& bus, const std::string& serviceRoot,
    const std::string& interface, const std::string& match = {});

/********* Begin co-routine yielding alternatives ***************/
/* 开始 co-routine，产生备选方案 */

/** @brief Get the D-Bus Service name for the input D-Bus path
 *
 *  @param[in] ctx - ipmi::Context::ptr
 *  @param[in] intf - D-Bus Interface
 *  @param[in] path - D-Bus Object Path
 *  @param[out] service - requested service name
 *  @return boost error code
 *
 */
/* @brief 获取输入 D-Bus 路径对应的 D-Bus 服务名称
 * @param[in] ctx - ipmi::Context::ptr
 * @param[in] intf - D-Bus Interface
 * @param[in] path - D-Bus Object Path
 * @param[out] service - 请求的服务名称
 * @return boost error code */
boost::system::error_code getService(Context::ptr ctx, const std::string& intf,
                                     const std::string& path,
                                     std::string& service);

/** @brief Gets the dbus sub tree implementing the given interface.
 *  @param[in] ctx - ipmi::Context::ptr
 *  @param[in] bus - DBUS Bus Object.
 *  @param[in] interfaces - Dbus interface.
 *  @param[in] subtreePath - subtree from where the search should start.
 *  @param[in] depth - Search depth
 *  @param[out] objectTree - map of object path and service info.
 *  @return map of object path and service info.
 */
/* @brief 获取实现给定接口的 dbus 子树。
 *  @param[in] ctx - ipmi::Context::ptr
 *  @param[in] bus - DBUS Bus Object。
 *  @param[in] interfaces - Dbus interface。
 *  @param[in] subtreePath - 搜索起始的子树。
 *  @param[in] depth - 搜索深度
 *  @param[out] objectTree - 对象路径与服务信息的 map。
 *  @return 对象路径与服务信息的 map。 */
boost::system::error_code getSubTree(
    Context::ptr ctx, const InterfaceList& interface,
    const std::string& subtreePath, int32_t depth, ObjectTree& objectTree);

/** @brief Gets the D-Bus object info implementing the given interface
 *         from the given subtree.
 *  @param[in] ctx - ipmi::Context::ptr
 *  @param[in] interface - D-Bus interface.
 *  @param[in][optional] subtreePath - subtree from where the search starts.
 *  @param[in][optional] match - identifier for object.
 *  @param[out] D-Bus object with path and service name
 *  @return - boost error code object
 */
/* @brief 从给定子树中获取实现给定接口的
 *  D-Bus 对象信息。
 *  @param[in] ctx - ipmi::Context::ptr
 *  @param[in] interface - D-Bus interface。
 *  @param[in][optional] subtreePath - 搜索起始的子树。
 *  @param[in][optional] match - 对象的标识符。
 *  @param[out] 含路径和服务名的 D-Bus 对象
 *  @return - boost error code object */
boost::system::error_code getDbusObject(
    Context::ptr ctx, const std::string& interface,
    const std::string& subtreePath, const std::string& match,
    DbusObjectInfo& dbusObject);

// default for ROOT for subtreePath and std::string{} for match
// subtreePath 默认为 ROOT，match 默认为 std::string{}
static inline boost::system::error_code getDbusObject(
    Context::ptr ctx, const std::string& interface, DbusObjectInfo& dbusObject)
{
    return getDbusObject(ctx, interface, ROOT, {}, dbusObject);
}

// default std::string{} for match
// match 默认为 std::string{}
static inline boost::system::error_code getDbusObject(
    Context::ptr ctx, const std::string& interface,
    const std::string& subtreePath, DbusObjectInfo& dbusObject)
{
    return getDbusObject(ctx, interface, subtreePath, {}, dbusObject);
}

/** @brief Gets the value associated with the given object
 *         and the interface.
 *  @param[in] ctx - ipmi::Context::ptr
 *  @param[in] service - D-Bus service name.
 *  @param[in] objPath - D-Bus object path.
 *  @param[in] interface - D-Bus interface.
 *  @param[in] property - name of the property.
 *  @param[out] propertyValue - value of the D-Bus property.
 *  @return - boost error code object
 */
/* @brief 获取与给定对象和接口关联的
 *  值。
 *  @param[in] ctx - ipmi::Context::ptr
 *  @param[in] service - D-Bus service name。
 *  @param[in] objPath - D-Bus object path。
 *  @param[in] interface - D-Bus interface。
 *  @param[in] property - 属性的名称。
 *  @param[out] propertyValue - D-Bus 属性的值。
 *  @return - boost error code object */
template <typename Type>
boost::system::error_code getDbusProperty(
    Context::ptr ctx, const std::string& service, const std::string& objPath,
    const std::string& interface, const std::string& property,
    Type& propertyValue)
{
    boost::system::error_code ec;
    auto variant = ctx->bus->yield_method_call<std::variant<Type>>(
        ctx->yield, ec, service.c_str(), objPath.c_str(), PROP_INTF, METHOD_GET,
        interface, property);
    if (!ec)
    {
        Type* tmp = std::get_if<Type>(&variant);
        if (tmp)
        {
            propertyValue = *tmp;
            return ec;
        }
        // user requested incorrect type; make an error code for them
        // 用户请求了错误的类型；为其生成一个错误码
        ec = boost::system::errc::make_error_code(
            boost::system::errc::invalid_argument);
    }
    return ec;
}

/** @brief Gets all the properties associated with the given object
 *         and the interface.
 *  @param[in] ctx - ipmi::Context::ptr
 *  @param[in] service - D-Bus service name.
 *  @param[in] objPath - D-Bus object path.
 *  @param[in] interface - D-Bus interface.
 *  @param[out] properties - map of name value pair.
 *  @return - boost error code object
 */
/* @brief 获取与给定对象和接口关联的
 *  所有属性。
 *  @param[in] ctx - ipmi::Context::ptr
 *  @param[in] service - D-Bus service name。
 *  @param[in] objPath - D-Bus object path。
 *  @param[in] interface - D-Bus interface。
 *  @param[out] properties - 名称-值对的 map。
 *  @return - boost error code object */
boost::system::error_code getAllDbusProperties(
    Context::ptr ctx, const std::string& service, const std::string& objPath,
    const std::string& interface, PropertyMap& properties);

/** @brief Sets the property value of the given object.
 *  @param[in] ctx - ipmi::Context::ptr
 *  @param[in] service - D-Bus service name.
 *  @param[in] objPath - D-Bus object path.
 *  @param[in] interface - D-Bus interface.
 *  @param[in] property - name of the property.
 *  @param[in] value - value which needs to be set.
 *  @return - boost error code object
 */
/* @brief 设置给定对象的属性值。
 *  @param[in] ctx - ipmi::Context::ptr
 *  @param[in] service - D-Bus service name。
 *  @param[in] objPath - D-Bus object path。
 *  @param[in] interface - D-Bus interface。
 *  @param[in] property - 属性的名称。
 *  @param[in] value - 要设置的值。
 *  @return - boost error code object */
boost::system::error_code setDbusProperty(
    Context::ptr ctx, const std::string& service, const std::string& objPath,
    const std::string& interface, const std::string& property,
    const Value& value);

/** @brief  Gets all the D-Bus objects from the given service root
 *          which matches the object identifier.
 *  @param[in] ctx - ipmi::Context::ptr
 *  @param[in] serviceRoot - Service root path.
 *  @param[in] interface - D-Bus interface.
 *  @param[in][optional] match - Identifier for a path.
 *  @param[out] objectree - map of object path and service info.
 *  @return - boost error code object
 */
/* @brief 从给定服务根中获取所有匹配对象
 *  标识符的 D-Bus 对象。
 *  @param[in] ctx - ipmi::Context::ptr
 *  @param[in] serviceRoot - Service root path。
 *  @param[in] interface - D-Bus interface。
 *  @param[in][optional] match - 路径的标识符。
 *  @param[out] objectree - 对象路径与服务信息的 map。
 *  @return - boost error code object */
boost::system::error_code getAllDbusObjects(
    Context::ptr ctx, const std::string& serviceRoot,
    const std::string& interface, const std::string& match,
    ObjectTree& objectTree);

// default std::string{} for match
// match 默认为 std::string{}
static inline boost::system::error_code getAllDbusObjects(
    Context::ptr ctx, const std::string& serviceRoot,
    const std::string& interface, ObjectTree& objectTree)
{
    return getAllDbusObjects(ctx, serviceRoot, interface, {}, objectTree);
}

/** @brief Gets all managed objects associated with the given object
 *         path and service.
 *  @param[in] ctx - ipmi::Context::ptr
 *  @param[in] service - D-Bus service name.
 *  @param[in] objPath - D-Bus object path.
 *  @param[out] objects - map of name value pair.
 *  @return - boost error code object
 */
/* @brief 获取与给定对象路径和服务关联的
 *  所有 managed objects。
 *  @param[in] ctx - ipmi::Context::ptr
 *  @param[in] service - D-Bus service name。
 *  @param[in] objPath - D-Bus object path。
 *  @param[out] objects - 名称-值对的 map。
 *  @return - boost error code object */
boost::system::error_code getManagedObjects(
    Context::ptr ctx, const std::string& service, const std::string& objPath,
    ObjectValueTree& objects);

/** @brief Gets the value associated with the given object
 *         and the interface.
 *  @param[in] ctx - ipmi::Context::ptr
 *  @param[in] service - D-Bus service name.
 *  @param[in] objPath - D-Bus object path.
 *  @param[in] interface - D-Bus interface.
 *  @param[in] method - name of the method.
 *  @return - boost error code object
 */
/* @brief 获取与给定对象和接口关联的
 *  值。
 *  @param[in] ctx - ipmi::Context::ptr
 *  @param[in] service - D-Bus service name。
 *  @param[in] objPath - D-Bus object path。
 *  @param[in] interface - D-Bus interface。
 *  @param[in] method - 方法的名称。
 *  @return - boost error code object */

boost::system::error_code callDbusMethod(
    Context::ptr ctx, const std::string& service, const std::string& objPath,
    const std::string& interface, const std::string& method);

/********* End co-routine yielding alternatives ***************/
/* 结束 co-routine，产生备选方案 */

/** @brief Retrieve the value from map of variants,
 *         returning a default if the key does not exist or the
 *         type of the value does not match the expected type
 *
 *  @tparam T - type of expected value to return
 *  @param[in] props - D-Bus property map (Map of variants)
 *  @param[in] name - key name of property to fetch
 *  @param[in] defaultValue - default value to return on error
 *  @return - value from property map at name, or defaultValue
 */
/* @brief 从 variant 的 map 中检索值，
 * 当键不存在或值的类型与预期类型不匹配时
 * 返回默认值。
 * @tparam T - 要返回的预期值的类型
 * @param[in] props - D-Bus 属性 map（variant 的 Map）
 * @param[in] name - 要获取属性的键名
 * @param[in] defaultValue - 出错时返回的默认值
 * @return - name 处属性值，或 defaultValue */
template <typename T>
T mappedVariant(const ipmi::PropertyMap& props, const std::string& name,
                const T& defaultValue)
{
    auto item = props.find(name);
    if (item == props.end())
    {
        return defaultValue;
    }
    const T* prop = std::get_if<T>(&item->second);
    if (!prop)
    {
        return defaultValue;
    }
    return *prop;
}

/** @struct VariantToDoubleVisitor
 *  @brief Visitor to convert variants to doubles
 *  @details Performs a static cast on the underlying type
 */
/* @struct VariantToDoubleVisitor
 * @brief 将 variant 转换为 double 的 Visitor
 * @details 对底层类型执行 static cast */
struct VariantToDoubleVisitor
{
    template <typename T>
    std::enable_if_t<std::is_arithmetic<T>::value, double> operator()(
        const T& t) const
    {
        return static_cast<double>(t);
    }

    template <typename T>
    std::enable_if_t<!std::is_arithmetic<T>::value, double> operator()(
        const T&) const
    {
        throw std::invalid_argument("Cannot translate type to double");
    }
};

namespace method_no_args
{

/** @brief Calls the Dbus method which waits for response.
 *  @param[in] bus - DBUS Bus Object.
 *  @param[in] service - Dbus service name.
 *  @param[in] objPath - Dbus object path.
 *  @param[in] interface - Dbus interface.
 *  @param[in] method - Dbus method.
 */
/* @brief 调用等待响应的 Dbus 方法。
 *  @param[in] bus - DBUS Bus Object。
 *  @param[in] service - Dbus service name。
 *  @param[in] objPath - Dbus object path。
 *  @param[in] interface - Dbus interface。
 *  @param[in] method - Dbus method。 */
void callDbusMethod(sdbusplus::bus_t& bus, const std::string& service,
                    const std::string& objPath, const std::string& interface,
                    const std::string& method);

} // namespace method_no_args

template <typename... InputArgs>
boost::system::error_code callDbusMethod(
    ipmi::Context::ptr ctx, const std::string& service,
    const std::string& objPath, const std::string& interface,
    const std::string& method, const InputArgs&... args)
{
    boost::system::error_code ec;
    ctx->bus->yield_method_call(ctx->yield, ec, service, objPath, interface,
                                method, args...);

    return ec;
}

template <typename RetType, typename... InputArgs>
RetType callDbusMethod(ipmi::Context::ptr ctx, boost::system::error_code& ec,
                       const std::string& service, const std::string& objPath,
                       const std::string& interface, const std::string& method,
                       const InputArgs&... args)
{
    auto rc = ctx->bus->yield_method_call<RetType>(
        ctx->yield, ec, service, objPath, interface, method, args...);

    return rc;
}

/** @brief Perform the low-level i2c bus write-read.
 *  @param[in] i2cBus - i2c bus device node name, such as /dev/i2c-2.
 *  @param[in] targetAddr - i2c device target address.
 *  @param[in] writeData - The data written to i2c device.
 *  @param[out] readBuf - Data read from the i2c device.
 */
/* @brief 执行底层 i2c 总线读写。
 *  @param[in] i2cBus - i2c 总线设备节点名，如 /dev/i2c-2。
 *  @param[in] targetAddr - i2c 设备目标地址。
 *  @param[in] writeData - 写入 i2c 设备的数据。
 *  @param[out] readBuf - 从 i2c 设备读取的数据。 */
ipmi::Cc i2cWriteRead(std::string i2cBus, const uint8_t targetAddr,
                      std::vector<uint8_t> writeData,
                      std::vector<uint8_t>& readBuf);

/** @brief Split a string into a vector of strings
 *  @param[in] srcStr - The string to split
 *  @param[in] delim - The delimiter to split the string on
 *  @return A vector of strings
 */
/* @brief 将字符串拆分为字符串 vector
 *  @param[in] srcStr - 要拆分的字符串
 *  @param[in] delim - 拆分用的分隔符
 *  @return 字符串组成的 vector */
std::vector<std::string> split(const std::string& srcStr, char delim);

/** @brief Parse an integral value from a string without throwing exceptions.
 *  @param[in] s - Input string_view to parse.
 *  @param[in,out] out - Output parameter that receives the parsed value on
 *                       success.
 *  @param[in] base - Number base (10 or 16). If 16, 0x/0X prefix is optional.
 *  @return True if parsing succeeds and the entire string is consumed;
 *          otherwise false.
 */
/* @brief 解析字符串中的整数值，且不抛出异常。
 * @param[in] s - 要解析的输入 string_view。
 * @param[in,out] out - 成功时接收解析值的
 *                        输出参数。
 * @param[in] base - 数制（10 或 16）。为 16 时 0x/0X 前缀可选。
 * @return 解析成功且消费完整个字符串时返回 true；
 *          否则返回 false。 */
template <typename T>
inline bool tryParse(std::string_view s, T& out, int base = 10)
{
    static_assert(std::is_integral_v<T>,
                  "tryParse only supports integral types");

    if (s.empty())
    {
        return false;
    }

    if (base == 16 && s.size() >= 2 && s[0] == '0' &&
        (s[1] == 'x' || s[1] == 'X'))
    {
        s.remove_prefix(2);
    }

    auto result = std::from_chars(s.data(), s.data() + s.size(), out, base);

    return result.ec == std::errc{} && result.ptr == s.data() + s.size();
}
} // namespace ipmi
