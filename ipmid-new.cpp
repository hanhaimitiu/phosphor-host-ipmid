
/**
 * @file ipmid-new.cpp
 * @brief ipmid 主程序：D-Bus 与 host 之间的 IPMI 命令分发核心。
 */
/* @file ipmid-new.cpp
 * @brief ipmid 主程序：D-Bus 与 host 之间的 IPMI 命令分发核心。 */
/**
 * Copyright © 2018 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/* Copyright © 2018 Intel Corporation
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License. */
#include "config.h"

#include "settings.hpp"

#include <dlfcn.h>

#include <boost/asio/detached.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/spawn.hpp>
#include <host-cmd-manager.hpp>
#include <ipmid-host/cmd.hpp>
#include <ipmid/api.hpp>
#include <ipmid/handler.hpp>
#include <ipmid/message.hpp>
#include <ipmid/oemrouter.hpp>
#include <ipmid/types.hpp>
#include <ipmid/utils.hpp>
#include <phosphor-logging/lg2.hpp>
#include <sdbusplus/asio/connection.hpp>
#include <sdbusplus/asio/object_server.hpp>
#include <sdbusplus/asio/sd_event.hpp>
#include <sdbusplus/bus.hpp>
#include <sdbusplus/bus/match.hpp>
#include <sdbusplus/timer.hpp>

#include <algorithm>
#include <any>
#include <exception>
#include <filesystem>
#include <forward_list>
#include <map>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

using namespace phosphor::logging;

// IPMI 规范要求的全局共享 SEL Reservation ID。
static unsigned short selReservationID = 0xFFFF;
static bool selReservationValid = false;

unsigned short reserveSel(void)
{
    // 按 IPMI 规范，Reservation ID 每执行一次 Reserve SEL 命令
    // 就自增一次。
    if (++selReservationID == 0)
    {
        selReservationID = 1;
    }
    selReservationValid = true;
    return selReservationID;
}

bool checkSELReservation(unsigned short id)
{
    return (selReservationValid && selReservationID == id);
}

void cancelSELReservation(void)
{
    selReservationValid = false;
}

EInterfaceIndex getInterfaceIndex(void)
{
    return interfaceKCS;
}

sd_bus* bus = nullptr;
sd_event* events = nullptr;
sd_event* ipmid_get_sd_event_connection(void)
{
    return events;
}
sd_bus* ipmid_get_sd_bus_connection(void)
{
    return bus;
}

namespace ipmi
{

static inline unsigned int makeCmdKey(unsigned int cluster, unsigned int cmd)
{
    return (cluster << 8) | cmd;
}

using HandlerTuple = std::tuple<int,                        /* 优先级 */
                                Privilege, HandlerBase::ptr /* handler */
                                >;

/* 标准 IPMI 命令的注册表 */
static std::unordered_map<unsigned int, /* key 是 NetFn/Cmd */
                          HandlerTuple>
    handlerMap;

/* Group 扩展命令（NetFn 2Ch）的专用注册表 */
static std::unordered_map<unsigned int, /* key 是 Group/Cmd（NetFn 为 2Ch） */
                          HandlerTuple>
    groupHandlerMap;

/* OEM 命令（NetFn 2Eh）的专用注册表 */
static std::unordered_map<unsigned int, /* key 是 Iana/Cmd（NetFn 为 2Eh） */
                          HandlerTuple>
    oemHandlerMap;

using FilterTuple = std::tuple<int,            /* 优先级 */
                               FilterBase::ptr /* filter */
                               >;

/* 保存所有已注册的 IPMI 命令过滤器 */
static std::forward_list<FilterTuple> filterList;

namespace impl
{
/* 注册所有标准 IPMI handler 的公共函数 */
bool registerHandler(int prio, NetFn netFn, Cmd cmd, Privilege priv,
                     HandlerBase::ptr handler)
{
    // 校验 NetFn 合法性：偶数；范围 00-0Ch、30-3Eh
    if (netFn & 1 || (netFn > netFnTransport && netFn < netFnGroup) ||
        netFn > netFnOemEight)
    {
        return false;
    }

    // 生成该 handler 的 key 与 value
    unsigned int netFnCmd = makeCmdKey(netFn, cmd);
    HandlerTuple item(prio, priv, handler);

    // 查表确认是否可以注册
    auto& mapCmd = handlerMap[netFnCmd];
    if (!std::get<HandlerBase::ptr>(mapCmd) || std::get<int>(mapCmd) <= prio)
    {
        mapCmd = item;
        return true;
    }
    return false;
}

/* 注册所有 Group IPMI handler 的公共函数 */
bool registerGroupHandler(int prio, Group group, Cmd cmd, Privilege priv,
                          HandlerBase::ptr handler)
{
    // 生成该 handler 的 key 与 value
    unsigned int netFnCmd = makeCmdKey(group, cmd);
    HandlerTuple item(prio, priv, handler);

    // 查表确认是否可以注册
    auto& mapCmd = groupHandlerMap[netFnCmd];
    if (!std::get<HandlerBase::ptr>(mapCmd) || std::get<int>(mapCmd) <= prio)
    {
        mapCmd = item;
        return true;
    }
    return false;
}

/* 注册所有 OEM IPMI handler 的公共函数 */
bool registerOemHandler(int prio, Iana iana, Cmd cmd, Privilege priv,
                        HandlerBase::ptr handler)
{
    // 生成该 handler 的 key 与 value
    unsigned int netFnCmd = makeCmdKey(iana, cmd);
    HandlerTuple item(prio, priv, handler);

    // 查表确认是否可以注册
    auto& mapCmd = oemHandlerMap[netFnCmd];
    if (!std::get<HandlerBase::ptr>(mapCmd) || std::get<int>(mapCmd) <= prio)
    {
        mapCmd = item;
        lg2::debug("registered OEM Handler: NetFn/Cmd={NETFNCMD}", "IANA",
                   lg2::hex, iana, "CMD", lg2::hex, cmd, "NETFNCMD", lg2::hex,
                   netFnCmd);
        return true;
    }

    lg2::warning("could not register OEM Handler: NetFn/Cmd={NETFNCMD}", "IANA",
                 lg2::hex, iana, "CMD", lg2::hex, cmd, "NETFNCMD", lg2::hex,
                 netFnCmd);
    return false;
}

/* 注册所有 IPMI 过滤器的公共函数 */
void registerFilter(int prio, FilterBase::ptr filter)
{
    // 先看是否应放在表头
    if (filterList.empty() || std::get<int>(filterList.front()) < prio)
    {
        filterList.emplace_front(std::make_tuple(prio, filter));
        return;
    }
    // 遍历链表，插到合适的位置
    auto j = filterList.begin();
    for (auto i = j; i != filterList.end() && std::get<int>(*i) > prio; i++)
    {
        j = i;
    }
    filterList.emplace_after(j, std::make_tuple(prio, filter));
}

} // namespace impl

message::Response::ptr filterIpmiCommand(message::Request::ptr request)
{
    // 让命令依次通过过滤器链：可以是固件防火墙，
    // 也可以是按运行模式过滤白名单等 OEM 机制
    for (auto& item : filterList)
    {
        FilterBase::ptr filter = std::get<FilterBase::ptr>(item);
        ipmi::Cc cc = filter->call(request);
        if (ipmi::ccSuccess != cc)
        {
            return errorResponse(request, cc);
        }
    }
    return message::Response::ptr();
}

message::Response::ptr executeIpmiCommandCommon(
    std::unordered_map<unsigned int, HandlerTuple>& handlers,
    unsigned int keyCommon, message::Request::ptr request)
{
    // 先过过滤器；返回非空的 message::Response::ptr
    // 表示消息因某种原因已被拒绝
    message::Response::ptr filterResponse = filterIpmiCommand(request);

    Cmd cmd = request->ctx->cmd;
    unsigned int key = makeCmdKey(keyCommon, cmd);
    auto cmdIter = handlers.find(key);
    if (cmdIter != handlers.end())
    {
        // 只有命中命令时才采用过滤器的拒绝结果
        if (filterResponse)
        {
            lg2::debug("request for NetFn/Cmd {NETFN}/{CMD} has been filtered",
                       "NETFN", lg2::hex, keyCommon, "CMD", lg2::hex, cmd);
            return filterResponse;
        }
        HandlerTuple& chosen = cmdIter->second;
        if (request->ctx->priv < std::get<Privilege>(chosen))
        {
            return errorResponse(request, ccInsufficientPrivilege);
        }
        return std::get<HandlerBase::ptr>(chosen)->call(request);
    }
    else
    {
        unsigned int wildcard = makeCmdKey(keyCommon, cmdWildcard);
        cmdIter = handlers.find(wildcard);
        if (cmdIter != handlers.end())
        {
            // only return the filter response if the command is found
            // 仅在找到该命令时才返回过滤响应
            if (filterResponse)
            {
                lg2::debug(
                    "request for NetFn/Cmd {NETFN}/{CMD} has been filtered",
                    "NETFN", lg2::hex, keyCommon, "CMD", lg2::hex, cmd);
                return filterResponse;
            }
            HandlerTuple& chosen = cmdIter->second;
            if (request->ctx->priv < std::get<Privilege>(chosen))
            {
                return errorResponse(request, ccInsufficientPrivilege);
            }
            return std::get<HandlerBase::ptr>(chosen)->call(request);
        }
    }
    return errorResponse(request, ccInvalidCommand);
}

message::Response::ptr executeIpmiGroupCommand(message::Request::ptr request)
{
    // look up the group for this request
    // 查找该请求对应的 group
    uint8_t bytes;
    if (0 != request->payload.unpack(bytes))
    {
        return errorResponse(request, ccReqDataLenInvalid);
    }
    auto group = static_cast<Group>(bytes);
    // Set defining body code
    // 设置 defining body code
    request->ctx->group = group;
    message::Response::ptr response =
        executeIpmiCommandCommon(groupHandlerMap, group, request);
    ipmi::message::Payload prefix;
    prefix.pack(bytes);
    response->prepend(prefix);
    return response;
}

message::Response::ptr executeIpmiOemCommand(message::Request::ptr request)
{
    // look up the iana for this request
    // 查找该请求对应的 iana
    uint24_t bytes;
    if (0 != request->payload.unpack(bytes))
    {
        return errorResponse(request, ccReqDataLenInvalid);
    }
    auto iana = static_cast<Iana>(bytes);

    lg2::debug("unpack IANA {IANA}", "IANA", lg2::hex, iana);

    message::Response::ptr response =
        executeIpmiCommandCommon(oemHandlerMap, iana, request);
    ipmi::message::Payload prefix;
    prefix.pack(bytes);
    response->prepend(prefix);
    return response;
}

message::Response::ptr executeIpmiCommand(message::Request::ptr request)
{
    NetFn netFn = request->ctx->netFn;
    if (netFnGroup == netFn)
    {
        return executeIpmiGroupCommand(request);
    }
    else if (netFnOem == netFn)
    {
        return executeIpmiOemCommand(request);
    }
    return executeIpmiCommandCommon(handlerMap, netFn, request);
}

namespace utils
{
template <typename AssocContainer, typename UnaryPredicate>
void assoc_erase_if(AssocContainer& c, UnaryPredicate p)
{
    typename AssocContainer::iterator next = c.begin();
    typename AssocContainer::iterator last = c.end();
    while ((next = std::find_if(next, last, p)) != last)
    {
        c.erase(next++);
    }
}
} // namespace utils

namespace
{
std::unordered_map<std::string, uint8_t> uniqueNameToChannelNumber;

// sdbusplus::match_rules::arg0namespace() wants the prefix
// to match without any trailing '.'
// sdbusplus::match_rules::arg0namespace() 要求待匹配的
// 前缀不带结尾的 '.'
constexpr const char ipmiDbusChannelMatch[] =
    "xyz.openbmc_project.Ipmi.Channel";
void updateOwners(sdbusplus::asio::connection& conn, const std::string& name)
{
    conn.async_method_call(
        [name](const boost::system::error_code ec,
               const std::string& nameOwner) {
            if (ec)
            {
                lg2::error("Error getting dbus owner for {INTERFACE}",
                           "INTERFACE", name);
                return;
            }
            // start after ipmiDbusChannelPrefix (after the '.')
            // 从 ipmiDbusChannelPrefix 之后开始（'.' 之后）
            std::string chName =
                name.substr(std::strlen(ipmiDbusChannelMatch) + 1);
            try
            {
                uint8_t channel = getChannelByName(chName);
                uniqueNameToChannelNumber[nameOwner] = channel;
                lg2::info(
                    "New interface mapping: {INTERFACE} -> channel {CHANNEL}",
                    "INTERFACE", name, "CHANNEL", channel);
            }
            catch (const std::exception& e)
            {
                lg2::info("Failed interface mapping, no such name: {INTERFACE}",
                          "INTERFACE", name);
            }
        },
        "org.freedesktop.DBus", "/", "org.freedesktop.DBus", "GetNameOwner",
        name);
}

void doListNames(sdbusplus::asio::connection& conn)
{
    conn.async_method_call(
        [&conn](const boost::system::error_code ec,
                std::vector<std::string> busNames) {
            if (ec)
            {
                lg2::error("Error getting dbus names: {ERROR}", "ERROR",
                           ec.message());
                std::exit(EXIT_FAILURE);
                return;
            }
            // Try to make startup consistent
            // 尽量使启动过程保持一致
            std::sort(busNames.begin(), busNames.end());

            const std::string channelPrefix =
                std::string(ipmiDbusChannelMatch) + ".";
            for (const std::string& busName : busNames)
            {
                if (busName.find(channelPrefix) == 0)
                {
                    updateOwners(conn, busName);
                }
            }
        },
        "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "ListNames");
}

void nameChangeHandler(sdbusplus::message_t& message)
{
    std::string name;
    std::string oldOwner;
    std::string newOwner;

    message.read(name, oldOwner, newOwner);

    if (!oldOwner.empty())
    {
        if (oldOwner.starts_with(":"))
        {
            // 连接断开
            auto it = uniqueNameToChannelNumber.find(oldOwner);
            if (it != uniqueNameToChannelNumber.end())
            {
                uniqueNameToChannelNumber.erase(it);
            }
        }
    }
    if (!newOwner.empty())
    {
        // 取 ipmiDbusChannelMatch 之后（'.' 之后）的通道名
        std::string chName = name.substr(std::strlen(ipmiDbusChannelMatch) + 1);
        try
        {
            uint8_t channel = getChannelByName(chName);
            uniqueNameToChannelNumber[newOwner] = channel;
            lg2::info("New interface mapping: {INTERFACE} -> channel {CHANNEL}",
                      "INTERFACE", name, "CHANNEL", channel);
        }
        catch (const std::exception& e)
        {
            lg2::info("Failed interface mapping, no such name: {INTERFACE}",
                      "INTERFACE", name);
        }
    }
};

} // anonymous namespace

static constexpr const char intraBmcName[] = "INTRABMC";
uint8_t channelFromMessage(sdbusplus::message_t& msg)
{
    // 返回 ipmitool 可解析的通道名
    std::string sender = msg.get_sender();
    auto chIter = uniqueNameToChannelNumber.find(sender);
    if (chIter != uniqueNameToChannelNumber.end())
    {
        return chIter->second;
    }
    // FIXME: 当前 BMC 内部连接是临时的，很难准确归类
    try
    {
        return getChannelByName(intraBmcName);
    }
    catch (const std::exception& e)
    {
        return invalidChannel;
    }
} // namespace ipmi

/* 由 sdbus 异步服务端上下文调用：IPMI 请求的执行入口 */
auto executionEntry(boost::asio::yield_context yield, sdbusplus::message_t& m,
                    NetFn netFn, uint8_t lun, Cmd cmd, ipmi::SecureBuffer& data,
                    std::map<std::string, ipmi::Value>& options)
{
    const auto dbusResponse =
        [netFn, lun, cmd](Cc cc, const ipmi::SecureBuffer& data = {}) {
            constexpr uint8_t netFnResponse = 0x01;
            uint8_t retNetFn = netFn | netFnResponse;
            return std::make_tuple(retNetFn, lun, cmd, cc, data);
        };
    std::string sender = m.get_sender();
    Privilege privilege = Privilege::None;
    int rqSA = 0;
    int hostIdx = 0;
    uint8_t userId = 0; // 未定义的用户
    uint32_t sessionId = 0;

    // 判断请求来自哪个通道
    uint8_t channel = channelFromMessage(m);
    if (channel == invalidChannel)
    {
        // 发送方通道未知：拒绝服务该请求
        lg2::error("ERROR determining source IPMI channel from "
                   "{SENDER} NetFn/Cmd {NETFN}/{CMD}",
                   "SENDER", sender, "NETFN", lg2::hex, netFn, "CMD", lg2::hex,
                   cmd);
        return dbusResponse(ipmi::ccDestinationUnavailable);
    }

    // 基于会话的通道必须提供 userId、privilege 与 sessionId
    if (getChannelSessionSupport(channel) != EChannelSessSupported::none)
    {
        try
        {
            Value requestPriv = options.at("privilege");
            Value requestUserId = options.at("userId");
            Value requestSessionId = options.at("currentSessionId");
            privilege = static_cast<Privilege>(std::get<int>(requestPriv));
            userId = static_cast<uint8_t>(std::get<int>(requestUserId));
            sessionId =
                static_cast<uint32_t>(std::get<uint32_t>(requestSessionId));
        }
        catch (const std::exception& e)
        {
            lg2::error("ERROR determining IPMI session credentials on "
                       "channel {CHANNEL} for userid {USERID}",
                       "CHANNEL", channel, "USERID", userId, "NETFN", lg2::hex,
                       netFn, "CMD", lg2::hex, cmd);
            return dbusResponse(ipmi::ccUnspecifiedError);
        }
    }
    else
    {
        // 无会话通道取最高特权：目前无法配置，默认 Admin
        privilege = Privilege::Admin;

        // IPMB 通道应提供 rqSA（请求方源地址）
        ChannelInfo chInfo;
        getChannelInfo(channel, chInfo);
        if (static_cast<EChannelMediumType>(chInfo.mediumType) ==
            EChannelMediumType::ipmb)
        {
            const auto iter = options.find("rqSA");
            if (iter != options.end())
            {
                if (std::holds_alternative<int>(iter->second))
                {
                    rqSA = std::get<int>(iter->second);
                }
            }
            const auto iteration = options.find("hostId");
            if (iteration != options.end())
            {
                if (std::holds_alternative<int>(iteration->second))
                {
                    hostIdx = std::get<int>(iteration->second);
                }
            }
        }
    }
    // 校验请求的特权/用户是否有效
    lg2::debug("Set up ipmi context: Ch:NetFn/Cmd={CHANNEL}:{NETFN}/{CMD}",
               "SENDER", sender, "NETFN", lg2::hex, netFn, "LUN", lg2::hex, lun,
               "CMD", lg2::hex, cmd, "CHANNEL", channel, "USERID", userId,
               "SESSIONID", lg2::hex, sessionId, "PRIVILEGE",
               static_cast<uint8_t>(privilege), "RQSA", lg2::hex, rqSA);

    auto ctx = std::make_shared<ipmi::Context>(
        getSdBus(), netFn, lun, cmd, channel, userId, sessionId, privilege,
        rqSA, hostIdx, yield);
    auto request = std::make_shared<ipmi::message::Request>(
        ctx, std::forward<ipmi::SecureBuffer>(data));
    message::Response::ptr response = executeIpmiCommand(request);

    return dbusResponse(response->cc, response->payload.raw);
}

/** @struct IpmiProvider
 *
 *  dlopen 的 RAII 封装：进程退出时自动 dlclose
 */
/* @struct IpmiProvider
 *  dlopen 的 RAII 封装：进程退出时自动 dlclose */
struct IpmiProvider
{
  public:
    /** @brief 已打开库的地址 */
    void* addr;
    std::string name;

    IpmiProvider() = delete;
    IpmiProvider(const IpmiProvider&) = delete;
    IpmiProvider& operator=(const IpmiProvider&) = delete;
    IpmiProvider(IpmiProvider&&) = delete;
    IpmiProvider& operator=(IpmiProvider&&) = delete;

    /** @brief 按路径 dlopen 一个动态库
     *  @param[in]  filename - 动态库路径
     */
    explicit IpmiProvider(const char* fname) : addr(nullptr), name(fname)
    {
        lg2::debug("Open IPMI provider library: {PROVIDER}", "PROVIDER", name);
        try
        {
            addr = dlopen(name.c_str(), RTLD_NOW);
        }
        catch (const std::exception& e)
        {
            lg2::error("ERROR opening IPMI provider {PROVIDER}: {ERROR}",
                       "PROVIDER", name, "ERROR", e);
        }
        catch (...)
        {
            const char* what = currentExceptionType();
            lg2::error("ERROR opening IPMI provider {PROVIDER}: {ERROR}",
                       "PROVIDER", name, "ERROR", what);
        }
        if (!isOpen())
        {
            lg2::error("ERROR opening IPMI provider {PROVIDER}: {ERROR}",
                       "PROVIDER", name, "ERROR", dlerror());
        }
    }

    ~IpmiProvider()
    {
        if (isOpen())
        {
            dlclose(addr);
        }
    }
    bool isOpen() const
    {
        return (nullptr != addr);
    }
};

// 插件库的文件名必须在末尾或中间包含 .so
constexpr const char ipmiPluginExtn[] = ".so";

/* 返回一组自动关闭的库句柄 */
std::forward_list<IpmiProvider> loadProviders(const fs::path& ipmiLibsPath)
{
    std::vector<fs::path> libs;
    for (const auto& libPath : fs::directory_iterator(ipmiLibsPath))
    {
        std::error_code ec;
        fs::path fname = libPath.path();
        if (fs::is_symlink(fname, ec) || ec)
        {
            // 是符号链接或其他错误：跳过
            continue;
        }
        while (fname.has_extension())
        {
            fs::path extn = fname.extension();
            if (extn == ipmiPluginExtn)
            {
                libs.push_back(libPath.path());
                break;
            }
            fname.replace_extension();
        }
    }
    std::sort(libs.begin(), libs.end());

    std::forward_list<IpmiProvider> handles;
    for (auto& lib : libs)
    {
#ifdef __IPMI_DEBUG__
        lg2::debug("Registering handler {HANDLER}", "HANDLER", lib);
#endif
        handles.emplace_front(lib.c_str());
    }
    return handles;
}

} // namespace ipmi

#ifdef ALLOW_DEPRECATED_API
/* legacy registration */
/* legacy（旧式）注册 */
void ipmi_register_callback(ipmi_netfn_t netFn, ipmi_cmd_t cmd,
                            ipmi_context_t context, ipmid_callback_t handler,
                            ipmi_cmd_privilege_t priv)
{
    auto h = ipmi::makeLegacyHandler(handler, context);
    // translate priv from deprecated enum to current
    // 将 priv 从已弃用的枚举转换为当前的枚举
    ipmi::Privilege realPriv;
    switch (priv)
    {
        case PRIVILEGE_CALLBACK:
            realPriv = ipmi::Privilege::Callback;
            break;
        case PRIVILEGE_USER:
            realPriv = ipmi::Privilege::User;
            break;
        case PRIVILEGE_OPERATOR:
            realPriv = ipmi::Privilege::Operator;
            break;
        case PRIVILEGE_ADMIN:
            realPriv = ipmi::Privilege::Admin;
            break;
        case PRIVILEGE_OEM:
            realPriv = ipmi::Privilege::Oem;
            break;
        case SYSTEM_INTERFACE:
            realPriv = ipmi::Privilege::Admin;
            break;
        default:
            realPriv = ipmi::Privilege::Admin;
            break;
    }
    // 旧版 ipmi_register_callback 也允许通过同一接口注册 Group OEM
    // handler。当年所有 handler 恰好都属于 DCMI 组，因此默认归入该组。
    if (netFn == ipmi::netFnGroup)
    {
        ipmi::impl::registerGroupHandler(ipmi::prioOpenBmcBase, ipmi::groupDCMI,
                                         cmd, realPriv, h);
    }
    else
    {
        ipmi::impl::registerHandler(ipmi::prioOpenBmcBase, netFn, cmd, realPriv,
                                    h);
    }
}

namespace oem
{

class LegacyRouter : public oem::Router
{
  public:
    virtual ~LegacyRouter() {}

    /// 启用消息路由。
    void activate() override {}

    void registerHandler(Number oen, ipmi_cmd_t cmd, Handler handler) override
    {
        auto h = ipmi::makeLegacyHandler(std::forward<Handler>(handler));
        ipmi::impl::registerOemHandler(ipmi::prioOpenBmcBase, oen, cmd,
                                       ipmi::Privilege::Admin, h);
    }
};
static LegacyRouter legacyRouter;

Router* mutableRouter()
{
    return &legacyRouter;
}

} // namespace oem

/* executionEntry 的旧版等价入口（废弃 API） */
void handleLegacyIpmiCommand(sdbusplus::message_t& m)
{
    // 先拷贝一份消息，避免后面两次 move 把栈上的数据搞乱
    sdbusplus::message_t b{m};
    boost::asio::spawn(
        *getIoContext(),
        [b = std::move(b)](boost::asio::yield_context yield) {
            sdbusplus::message_t m{std::move(b)};
            unsigned char seq = 0, netFn = 0, lun = 0, cmd = 0;
            ipmi::SecureBuffer data;

            m.read(seq, netFn, lun, cmd, data);
            std::shared_ptr<sdbusplus::asio::connection> bus = getSdBus();
            auto ctx = std::make_shared<ipmi::Context>(
                bus, netFn, lun, cmd, 0, 0, 0, ipmi::Privilege::Admin, 0, 0,
                yield);
            auto request = std::make_shared<ipmi::message::Request>(
                ctx, std::forward<ipmi::SecureBuffer>(data));
            ipmi::message::Response::ptr response =
                ipmi::executeIpmiCommand(request);

            // IPMI 应答要求 NetFn 的请求/应答位置位，因此补上最低位
            netFn |= 0x01;

            constexpr const char* DBUS_INTF = "org.openbmc.HostIpmi";

            std::string dest = m.get_sender();
            std::string path = m.get_path();
            boost::system::error_code ec = ipmi::callDbusMethod(
                ctx, dest, path, DBUS_INTF, "sendMessage", seq, netFn, lun, cmd,
                response->cc, response->payload.raw);

            if (ec)
            {
                lg2::error(
                    "Failed to send response to requester ({NETFN}/{CMD}): {ERROR}",
                    "ERROR", ec.message(), "SENDER", dest, "NETFN", lg2::hex,
                    netFn, "CMD", lg2::hex, cmd);
            }
        },
        boost::asio::detached);
}

#endif /* ALLOW_DEPRECATED_API */

// 调用 host 命令管理器执行面向 host 的命令
using CommandHandler = phosphor::host::command::CommandHandler;
std::unique_ptr<phosphor::host::command::Manager> cmdManager;
void ipmid_send_cmd_to_host(CommandHandler&& cmd)
{
    cmdManager->execute(std::forward<CommandHandler>(cmd));
}

std::unique_ptr<phosphor::host::command::Manager>& ipmid_get_host_cmd_manager()
{
    return cmdManager;
}

// 以下符号位于 libipmid 中，预期只在本文件（或单元测试）中使用，
// 因此在这里声明
extern void setIoContext(std::shared_ptr<boost::asio::io_context>& newIo);
extern void setSdBus(std::shared_ptr<sdbusplus::asio::connection>& newBus);

int main(int argc, char* argv[])
{
    // 连接系统总线
    auto io = std::make_shared<boost::asio::io_context>();
    setIoContext(io);
    if (argc > 1 && std::string(argv[1]) == "-session")
    {
        auto rc = sd_bus_default_user(&bus);
        if (rc < 0)
        {
            lg2::error("Failed to connect to user bus: {RC}", "RC", rc);
            return EXIT_FAILURE;
        }
    }
    else
    {
        auto rc = sd_bus_default_system(&bus);
        if (rc < 0)
        {
            lg2::error("Failed to connect to system bus: {RC}", "RC", rc);
            return EXIT_FAILURE;
        }
    }
    auto sdbusp = std::make_shared<sdbusplus::asio::connection>(*io, bus);
    setSdBus(sdbusp);

    // TODO: 保持 sdEvents 运行的权宜之计……不清楚为什么没有定时器
    //       反复重新武装时 sd_event 队列会停止运行
    sdbusplus::Timer t2([]() { ; });
    t2.start(std::chrono::microseconds(500000), true);

    // TODO: 彻底移除 phosphor-host-ipmid 中残留的 sd_event；
    //       在此之前先把 sd_event 包装器挂到 io 对象上
    sdbusplus::asio::sd_event_wrapper sdEvents(*io);

    cmdManager = std::make_unique<phosphor::host::command::Manager>(*sdbusp);

    // 注册所有命令提供者（插件库）与过滤器
    std::forward_list<ipmi::IpmiProvider> providers =
        ipmi::loadProviders(HOST_IPMI_LIB_PATH);

#ifdef ALLOW_DEPRECATED_API
    // 监听废弃的 KCS/BT 命令信号接口
    constexpr const char* FILTER = "type='signal',interface='org.openbmc."
                                   "HostIpmi',member='ReceivedMessage'";
    sdbusplus::match oldIpmiInterface(*sdbusp, FILTER, handleLegacyIpmiCommand);
#endif /* ALLOW_DEPRECATED_API */

    // 监视总线名变化，把 IPMI 通道服务名映射到通道号
    sdbusplus::match nameOwnerChanged(
        *sdbusp,
        sdbusplus::match_rules::nameOwnerChanged() +
            sdbusplus::match_rules::arg0namespace(ipmi::ipmiDbusChannelMatch),
        ipmi::nameChangeHandler);
    ipmi::doListNames(*sdbusp);

    int exitCode = 0;
    // 设置 boost::asio 的信号处理
    std::function<SignalResponse(int)> stopAsioRunLoop = [&io, &exitCode](
                                                             int signalNumber) {
        lg2::info("Received signal {SIGNAL}; quitting", "SIGNAL", signalNumber);
        io->stop();
        exitCode = signalNumber;
        return SignalResponse::breakExecution;
    };
    registerSignalHandler(ipmi::prioOpenBmcBase, SIGINT, stopAsioRunLoop);
    registerSignalHandler(ipmi::prioOpenBmcBase, SIGTERM, stopAsioRunLoop);

    sdbusp->request_name("xyz.openbmc_project.Ipmi.Host");
    // 为入站 IPMI 请求注册 D-Bus 方法绑定
    auto server = sdbusplus::asio::object_server(sdbusp);
    auto iface = server.add_interface("/xyz/openbmc_project/Ipmi",
                                      "xyz.openbmc_project.Ipmi.Server");
    iface->register_method("execute", ipmi::executionEntry);
    iface->initialize();

    io->run();

    // 先销毁所有 IPMI handler，插件库才能安全卸载
    ipmi::handlerMap.clear();
    ipmi::groupHandlerMap.clear();
    ipmi::oemHandlerMap.clear();
    ipmi::filterList.clear();
    // 卸载插件库
    providers.clear();

    std::exit(exitCode);
}
