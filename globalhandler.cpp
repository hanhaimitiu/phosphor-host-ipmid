
/**
 * @file globalhandler.cpp
 * @brief Global IPMI 命令（如 Cold Reset）的实现。
 */
/* @file globalhandler.cpp
 * @brief Global IPMI 命令（如 Cold Reset）的实现。 */
#include <ipmid/api.hpp>
#include <ipmid/utils.hpp>
#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/State/BMC/server.hpp>

#include <string>

using BMCState = sdbusplus::server::xyz::openbmc_project::state::BMC;

void registerNetFnGlobalFunctions() __attribute__((constructor));

/** @brief implements cold and warm reset commands
 *  @param - None
 *  @returns IPMI completion code.
 */
/* @brief 实现冷复位和热复位命令
 *  @param - 无
 *  @returns IPMI 完成码。 */
ipmi::RspType<> ipmiGlobalReset(ipmi::Context::ptr ctx)
{
    ipmi::DbusObjectInfo bmcStateObj;
    boost::system::error_code ec = ipmi::getDbusObject(
        ctx, BMCState::interface, BMCState::namespace_path::value,
        BMCState::namespace_path::bmc, bmcStateObj);
    if (!ec)
    {
        std::string service;
        ec = ipmi::getService(ctx, BMCState::interface, bmcStateObj.first,
                              service);
        if (!ec)
        {
            ec = ipmi::setDbusProperty(
                ctx, service, bmcStateObj.first, BMCState::interface,
                BMCState::property_names::requested_bmc_transition,
                convertForMessage(BMCState::Transition::Reboot));
        }
    }
    if (ec)
    {
        lg2::error("Exception in Global Reset: {ERROR}", "ERROR", ec.message());
        return ipmi::responseUnspecifiedError();
    }

    // Status code.
    // 状态码。
    return ipmi::responseSuccess();
}

void registerNetFnGlobalFunctions()
{
    // Cold Reset
    // 冷复位（Cold Reset）
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnApp,
                          ipmi::app::cmdColdReset, ipmi::Privilege::Admin,
                          ipmiGlobalReset);
    return;
}
