
/**
 * @file sbmrhandler.cpp
 * @brief SBMR OEM 命令的处理。
 */
/* @file sbmrhandler.cpp
 * @brief SBMR OEM 命令的处理。 */
#include <ipmid/api.hpp>
#include <ipmid/filter.hpp>
#include <ipmid/utils.hpp>
#include <phosphor-logging/lg2.hpp>

constexpr auto sbmrBootStateIntf = "xyz.openbmc_project.State.Boot.Raw";
constexpr auto sbmrHostStateIntf = "xyz.openbmc_project.State.Boot.Progress";
constexpr auto sbmrBootProgressCodeSize = 9;

constexpr auto bootProgressOem = "OEM";
constexpr auto bootProgressOsRuning = "OSRunning";
constexpr auto bootProgressOsStart = "OSStart";
constexpr auto bootProgressPciInit = "PCIInit";
constexpr auto bootProgressSystemInitComplete = "SystemInitComplete";
constexpr auto bootProgressSystemSetup = "SystemSetup";

// EFI_STATUS_CODE_TYPE
// EFI_STATUS_CODE_TYPE
constexpr auto efiProgressCode = 0x01;
constexpr auto efiCodeSeverityNone = 0;

// EFI_STATUS_CODE_CLASS
// EFI_STATUS_CODE_CLASS
constexpr auto efiIoBus = 0x02;
constexpr auto efiSoftware = 0x03;

// EFI_STATUS_CODE_SUBCLASS
// EFI_STATUS_CODE_SUBCLASS
constexpr auto efiIoBusPci = 0x01;
constexpr auto efiSoftwareDxeCore = 0x04;
constexpr auto efiSoftwareDxeBsDriver = 0x05;
constexpr auto efiSoftwareEfiBootService = 0x10;

// EFI_STATUS_CODE_OPERATION
// EFI_STATUS_CODE_OPERATION
constexpr auto efiIoBusPciResAlloc = 0x0110;
constexpr auto efiSwDxeCorePcHandoffToNext = 0x0110;
constexpr auto efiSwPcUserSetup = 0x0700;
constexpr auto efiSwOsLoaderStart = 0x0180;
constexpr auto efiSwBsPcExitBootServices = 0x1910;

void registerNetfnSBMRFunctions() __attribute__((constructor));

namespace ipmi
{

std::string getSbmrBootProgressStage(uint8_t codeType, uint8_t codeSeverity,
                                     uint8_t codeClass, uint8_t codeSubClass,
                                     uint16_t codeOperation)
{
    // Return OEM if code type or severity are unexpected
    // 若代码类型或严重级别意外，则返回 OEM
    if (codeType != efiProgressCode || codeSeverity != efiCodeSeverityNone)
    {
        return bootProgressOem;
    }

    // Code Class Software
    // 代码类别：Software（软件）
    if (codeClass == efiSoftware)
    {
        if (codeSubClass == efiSoftwareDxeCore &&
            codeOperation == efiSwDxeCorePcHandoffToNext)
        {
            return bootProgressSystemInitComplete;
        }
        else if (codeSubClass == efiSoftwareDxeBsDriver &&
                 codeOperation == efiSwPcUserSetup)
        {
            return bootProgressSystemSetup;
        }
        else if (codeSubClass == efiSoftwareDxeBsDriver &&
                 codeOperation == efiSwOsLoaderStart)
        {
            return bootProgressOsStart;
        }
        else if (codeSubClass == efiSoftwareEfiBootService &&
                 codeOperation == efiSwBsPcExitBootServices)
        {
            return bootProgressOsRuning;
        }
    }
    // Code Class IO Bus
    // 代码类别：IO Bus（IO 总线）
    else if (codeClass == efiIoBus)
    {
        if (codeSubClass == efiIoBusPci && codeOperation == efiIoBusPciResAlloc)
        {
            return bootProgressPciInit;
        }
    }

    // Fallback to OEM if no conditions met
    // 若不满足任何条件则回退到 OEM
    return bootProgressOem;
}

bool updateBootProgressProperty(ipmi::Context::ptr& ctx,
                                const std::string& value)
{
    std::string bootProgress =
        "xyz.openbmc_project.State.Boot.Progress.ProgressStages." + value;
    ipmi::DbusObjectInfo sbmrHostStateObject{};

    /* Get Host State Object */
    /* 获取 Host State 对象 */
    boost::system::error_code ec =
        ipmi::getDbusObject(ctx, sbmrHostStateIntf, sbmrHostStateObject);
    if (ec.value())
    {
        lg2::error("Failed to get Host State object, Error={ERROR}", "ERROR",
                   ec.message());
        return false;
    }

    /* Set Host State property */
    /* 设置 Host State 属性 */
    ec = ipmi::setDbusProperty(ctx, sbmrHostStateObject.second,
                               sbmrHostStateObject.first, sbmrHostStateIntf,
                               "BootProgress", bootProgress);
    if (ec.value())
    {
        lg2::error(
            "updateBootProgressProperty, can't set progerty - Error={ERROR}",
            "ERROR", ec.message());
        return false;
    }

    return true;
}

bool updateBootProgressLastUpdateProperty(ipmi::Context::ptr& ctx,
                                          uint64_t timeStamp)
{
    ipmi::DbusObjectInfo sbmrHostStateObject{};

    /* Get Host State Object */
    /* 获取 Host State 对象 */
    boost::system::error_code ec =
        ipmi::getDbusObject(ctx, sbmrHostStateIntf, sbmrHostStateObject);
    if (ec.value())
    {
        lg2::error("Failed to get Host State object, Error={ERROR}", "ERROR",
                   ec.message());
        return false;
    }

    /* Set Host State property */
    /* 设置 Host State 属性 */
    ec = ipmi::setDbusProperty(ctx, sbmrHostStateObject.second,
                               sbmrHostStateObject.first, sbmrHostStateIntf,
                               "BootProgressLastUpdate", timeStamp);
    if (ec.value())
    {
        lg2::error(
            "updateBootProgressLastUpdateProperty, can't set property - Error={ERROR}",
            "ERROR", ec.message());
        return false;
    }

    return true;
}

ipmi::RspType<> sendBootProgressCode(
    ipmi::Context::ptr ctx, uint8_t codeType, uint8_t codeReserved1,
    uint8_t codeReserved2, uint8_t codeSeverity, uint8_t codeOperation1,
    uint8_t codeOperation2, uint8_t codeSubClass, uint8_t codeClass,
    uint8_t instance)
{
    /* Update boot progress code to Dbus property */
    /* 将启动进度码更新到 Dbus 属性 */
    ipmi::DbusObjectInfo sbmrBootStateObject{};

    /* Get Boot State Object */
    /* 获取 Boot State 对象 */
    boost::system::error_code ec =
        ipmi::getDbusObject(ctx, sbmrBootStateIntf, sbmrBootStateObject);
    if (ec.value())
    {
        lg2::error("Failed to get Boot State object, Error={ERROR}", "ERROR",
                   ec.message());
        return ipmi::responseUnspecifiedError();
    }

    /* Set Boot State property */
    /* 设置 Boot State 属性 */
    BootProgressCode bpCode(
        {codeType, codeReserved1, codeReserved2, codeSeverity, codeOperation1,
         codeOperation2, codeSubClass, codeClass, instance},
        {});
    ec = ipmi::setDbusProperty(ctx, sbmrBootStateObject.second,
                               sbmrBootStateObject.first, sbmrBootStateIntf,
                               "Value", bpCode);
    if (ec.value())
    {
        lg2::error("Failed to set boot progress code, Error={ERROR}", "ERROR",
                   ec.message());
        return ipmi::responseUnspecifiedError();
    }

    /* Update Redfish BootProgress object */
    /* 更新 Redfish BootProgress 对象 */
    auto timeStamp = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    if (!updateBootProgressLastUpdateProperty(ctx, timeStamp))
    {
        return ipmi::responseUnspecifiedError();
    }

    /* Check for BootProgressTypes */
    /* 检查 BootProgressTypes */
    uint16_t codeOperation =
        static_cast<uint16_t>(codeOperation1) << 8 | codeOperation2;

    std::string stage = getSbmrBootProgressStage(
        codeType, codeSeverity, codeClass, codeSubClass, codeOperation);

    if (!updateBootProgressProperty(ctx, stage))
    {
        return ipmi::responseUnspecifiedError();
    }

    return ipmi::responseSuccess();
}

ipmi::RspType<uint8_t, // STATUS_CODE_TYPE
              uint8_t, // STATUS_CODE_RESERVED1
              uint8_t, // STATUS_CODE_RESERVED2
              uint8_t, // STATUS_CODE_SEVERITY
              uint8_t, // STATUS_CODE_OPERATION1
              uint8_t, // STATUS_CODE_OPERATION2
              uint8_t, // STATUS_CODE_SUBCLASS
              uint8_t, // STATUS_CODE_CLASS
              uint8_t> // Instance
    getBootProgressCode(ipmi::Context::ptr ctx)
{
    ipmi::DbusObjectInfo sbmrBootStateObject{};

    /* Get Boot State Object */
    /* 获取 Boot State 对象 */
    boost::system::error_code ec =
        ipmi::getDbusObject(ctx, sbmrBootStateIntf, sbmrBootStateObject);
    if (ec.value())
    {
        lg2::error("Failed to get Boot State object, Error={ERROR}", "ERROR",
                   ec.message());
        return ipmi::responseUnspecifiedError();
    }

    /* Get Boot State property */
    /* 获取 Boot State 属性 */
    BootProgressCode value;
    ec = ipmi::getDbusProperty(ctx, sbmrBootStateObject.second,
                               sbmrBootStateObject.first, sbmrBootStateIntf,
                               "Value", value);
    if (ec.value())
    {
        lg2::error("Can't get property Value, Error={ERROR}", "ERROR",
                   ec.message());
        return ipmi::responseUnspecifiedError();
    }

    auto respBootProgressCode = std::get<0>(std::move(value));
    if (respBootProgressCode.size() != sbmrBootProgressCodeSize)
    {
        return ipmi::responseUnspecifiedError();
    }

    return ipmi::responseSuccess(
        respBootProgressCode[0], respBootProgressCode[1],
        respBootProgressCode[2], respBootProgressCode[3],
        respBootProgressCode[4], respBootProgressCode[5],
        respBootProgressCode[6], respBootProgressCode[7],
        respBootProgressCode[8]);
}

bool checkAllowedMediumType(uint8_t mediumType)
{
    if (mediumType ==
            static_cast<uint8_t>(ipmi::EChannelMediumType::smbusV20) ||
        mediumType ==
            static_cast<uint8_t>(ipmi::EChannelMediumType::systemInterface) ||
        mediumType == static_cast<uint8_t>(ipmi::EChannelMediumType::oem))
    {
        return true;
    }

    return false;
}

ipmi::Cc sbmrFilterCommands(ipmi::message::Request::ptr request)
{
    if (request->ctx->netFn != ipmi::netFnGroup ||
        request->ctx->group != ipmi::groupSBMR)
    {
        // Skip if not group SBMR
        // 若不是 SBMR 组则跳过
        return ipmi::ccSuccess;
    }

    ipmi::ChannelInfo chInfo;
    if (ipmi::getChannelInfo(request->ctx->channel, chInfo) != ipmi::ccSuccess)
    {
        lg2::error("Failed to get Channel Info, channel={CHANNEL}", "CHANNEL",
                   request->ctx->channel);
        return ipmi::ccUnspecifiedError;
    }

    if (request->ctx->cmd == ipmi::sbmr::cmdSendBootProgressCode &&
        !checkAllowedMediumType(chInfo.mediumType))
    {
        lg2::error("Error - Medium interface not supported, medium={TYPE}",
                   "TYPE", chInfo.mediumType);
        return ipmi::ccCommandNotAvailable;
    }

    return ipmi::ccSuccess;
}

} // namespace ipmi

void registerNetfnSBMRFunctions()
{
    registerGroupHandler(ipmi::prioOpenBmcBase, ipmi::groupSBMR,
                         ipmi::sbmr::cmdSendBootProgressCode,
                         ipmi::Privilege::Admin, ipmi::sendBootProgressCode);

    registerGroupHandler(ipmi::prioOpenBmcBase, ipmi::groupSBMR,
                         ipmi::sbmr::cmdGetBootProgressCode,
                         ipmi::Privilege::User, ipmi::getBootProgressCode);

    ipmi::registerFilter(ipmi::prioOemBase,
                         [](ipmi::message::Request::ptr request) {
                             return ipmi::sbmrFilterCommands(request);
                         });
}
