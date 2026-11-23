
/**
 * @file cmd.hpp
 * @brief host 命令注册/回调的声明。
 */
/*  @file cmd.hpp
 *  @brief host 命令注册/回调的声明。 */
#include <ipmid-host/cmd-utils.hpp>
#include <sdbusplus/asio/connection.hpp>

#include <memory>

// Global Host Bound Command manager
// 全局 Host Bound Command 管理器
extern void ipmid_send_cmd_to_host(phosphor::host::command::CommandHandler&&);
extern std::unique_ptr<sdbusplus::asio::connection>&
    ipmid_get_sdbus_plus_handler();
