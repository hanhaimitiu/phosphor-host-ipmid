
/**
 * @file systemintf-sdbus.cpp
 * @brief SystemIntf（系统调用抽象）的 sdbus 实现，便于测试替换。
 */
/*  @file systemintf-sdbus.cpp
 *  @brief SystemIntf（系统调用抽象）的 sdbus 实现，便于测试替换。 */
#include <ipmid/api.hpp>
#include <sdbusplus/asio/connection.hpp>

#include <memory>

namespace
{

std::unique_ptr<sdbusplus::asio::connection> sdbusp;

} // namespace

/**
 * @brief ipmid_get_sdbus_plus_handler is used by some ipmi providers
 *
 * @return: a reference to a unique pointer of the systemd connection
 *          managed by the systemintfcmds code
 */
/*  @brief 某些 ipmi provider 会使用 ipmid_get_sdbus_plus_handler
 *  @return: 由 systemintfcmds 代码管理的 systemd 连接
 *           unique_ptr 的引用 */
std::unique_ptr<sdbusplus::asio::connection>& ipmid_get_sdbus_plus_handler()
{
    if (!sdbusp)
    {
        // Create a new sdbus connection so it can have a well-known name
        // 创建一个新的 sdbus 连接，以便它拥有一个 well-known name
        sd_bus* bus = nullptr;
        sd_bus_open_system(&bus);
        if (bus)
        {
            sdbusp = std::make_unique<sdbusplus::asio::connection>(
                *getIoContext(), bus);
        }
    }
    return sdbusp;
}
