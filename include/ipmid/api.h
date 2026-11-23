
/**
 * @file api.h
 * @brief C 风格插件 API：第三方 handler 以此注册命令回调。
 */
/*  @file api.h
 *  @brief C 风格插件 API：第三方 handler 以此注册命令回调。 */
#ifndef __HOST_IPMID_IPMI_COMMON_H__
#define __HOST_IPMID_IPMI_COMMON_H__

#include <systemd/sd-bus.h>

#include <cstddef>

/*
 * 声明执行一条命令所需的最低特权级：
 * 命令可在给定特权级或更高级别上执行。
 * 只能通过系统接口（System Interface）执行的命令应使用
 * SYSTEM_INTERFACE。
 */
/*  声明执行一条命令所需的最低特权级：
 *  命令可在给定特权级或更高级别上执行。
 *  只能通过系统接口（System Interface）执行的命令应使用
 *  SYSTEM_INTERFACE。 */
enum CommandPrivilege
{
    PRIVILEGE_CALLBACK = 0x01, // Callback 特权
    PRIVILEGE_USER,            // User
    PRIVILEGE_OPERATOR,        // Operator
    PRIVILEGE_ADMIN,           // Administrator
    PRIVILEGE_OEM,             // OEM 专有
    SYSTEM_INTERFACE = 0xFF,   // 仅限系统接口
};

// IPMI v2.0 规范定义的 Net Function（网络功能号）。
// 示例：
// NETFUN_APP      =   (0x06 << 2),
// IPMI v2.0 规范定义的 Net Function（网络功能号）。
//  示例：
//  NETFUN_APP      =   (0x06 << 2),
typedef unsigned char ipmi_netfn_t;

// 某个 Net Function 下的 IPMI 命令号。
typedef unsigned char ipmi_cmd_t;

// 请求缓冲：发送方随 [netfn, cmd] 一起发来的数据。
typedef void* ipmi_request_t;

// 应答缓冲：[netfn,cmd] 的提供者把应答写回给调用者。
// Provider 在 handler 内部分配内存，然后 memcpy 到该缓冲，
// 并把 data_len 设置为缓冲大小。
// 示例：
// unsigned char str[] = {0x00, 0x01, 0xFE, 0xFF, 0x0A, 0x01};
// *data_len = 6;
// memcpy(response, &str, *data_len);
// 应答缓冲：[netfn,cmd] 的提供者把应答写回给调用者。
//  Provider 在 handler 内部分配内存，然后 memcpy 到该缓冲，
//  并把 data_len 设置为缓冲大小。
//  示例：
//  unsigned char str[] = {0x00, 0x01, 0xFE, 0xFF, 0x0A, 0x01};
//  *data_len = 6;
//  memcpy(response, &str, *data_len);
typedef void* ipmi_response_t;

// *插件私有*的上下文数据，IPMI 命令路由器对其不透明。
// 注册插件 handler 时，插件可自行分配内存并填充后续命令处理
// 需要的内容；IPMID 调用 handler 时会原样传回 netfn、cmd 和这块数据。
typedef void* ipmi_context_t;

// 请求/应答缓冲的长度（取决于该数据是请求还是应答）。
typedef std::size_t* ipmi_data_len_t;

// 插件函数返回的完成码
typedef unsigned char ipmi_ret_t;

typedef enum CommandPrivilege ipmi_cmd_privilege_t;

// 插件向 IPMID 注册的回调 handler。IPMI 命令路由器收到命令后，
// 会带着 netfn、cmd、请求、应答、长度与上下文调用它。
typedef ipmi_ret_t (*ipmid_callback_t)(ipmi_netfn_t, ipmi_cmd_t, ipmi_request_t,
                                       ipmi_response_t, ipmi_data_len_t,
                                       ipmi_context_t);

// 各插件 handler 的注册入口函数。
// ipmid 建立回调时传入 netfn、cmd、handler 指针与上下文数据。
void ipmi_register_callback(ipmi_netfn_t, ipmi_cmd_t, ipmi_context_t,
                            ipmid_callback_t, ipmi_cmd_privilege_t);

unsigned short reserveSel(void);
bool checkSELReservation(unsigned short id);
void cancelSELReservation(void);

// 命令的网络功能号集合。应答的网络功能号 = 命令的网络功能号 + 1，
// 因此要找出某条应答对应的命令功能号，减 1 即可。
// 注意：这些值还左移了 2 位，以便为 LUN 留出空间。
enum __attribute__((deprecated(
    "Deprecated enum: use netfn values from ipmi-api.hpp; this enum will be removed after 31-Dec-2025")))
ipmi_net_fns
{
    NETFUN_CHASSIS = 0x00,
    NETFUN_BRIDGE = 0x02,
    NETFUN_SENSOR = 0x04,
    NETFUN_APP = 0x06,
    NETFUN_FIRMWARE = 0x08,
    NETFUN_STORAGE = 0x0a,
    NETFUN_TRANSPORT = 0x0c,
    NETFUN_GRPEXT = 0x2c,
    NETFUN_OEM_GROUP = 0x2e,
    NETFUN_NONE = 0x30,
    NETFUN_OEM = 0x32,
    NETFUN_IBM_OEM = 0x3A
};

// IPMI 操作的返回（完成）码，遵循 IPMI v2.0 规范。
enum ipmi_return_codes
{
    IPMI_CC_OK = 0x00,
    IPMI_DCMI_CC_NO_ACTIVE_POWER_LIMIT = 0x80,
    IPMI_WDOG_CC_NOT_INIT = 0x80,
    IPMI_CC_SYSTEM_INFO_PARAMETER_NOT_SUPPORTED = 0x80,
    IPMI_CC_SYSTEM_INFO_PARAMETER_SET_READ_ONLY = 0x82,
    IPMI_CC_BUSY = 0xC0,
    IPMI_CC_INVALID = 0xC1,
    IPMI_CC_TIMEOUT = 0xC3,
    IPMI_CC_OUT_OF_SPACE = 0xC4,
    IPMI_CC_INVALID_RESERVATION_ID = 0xC5,
    IPMI_CC_REQ_DATA_TRUNCATED = 0xC6,
    IPMI_CC_REQ_DATA_LEN_INVALID = 0xC7,
    IPMI_CC_PARM_OUT_OF_RANGE = 0xC9,
    IPMI_CC_REQUESTED_TOO_MANY_BYTES = 0xCA,
    IPMI_CC_SENSOR_INVALID = 0xCB,
    IPMI_CC_INVALID_FIELD_REQUEST = 0xCC,
    IPMI_CC_ILLEGAL_COMMAND = 0xCD,
    IPMI_CC_RESPONSE_ERROR = 0xCE,
    IPMI_CC_INSUFFICIENT_PRIVILEGE = 0xD4,
    IPMI_CC_UNSPECIFIED_ERROR = 0xFF,
};

// 临时方案：判断请求来源通道。
// 目前没有直接获取确切通道的方法，只能硬编码：
// host-ipmid 记为 KCS，netipmid 记为 LAN1。
// 该逻辑无法区分 netipmid 的 LAN1 与 LAN2——当前设计不支持。
// 这样做是为了让所有用到"自身通道"的地方能先正确工作，
// 待新架构落地后再更新。
typedef enum
{
    interfaceKCS = 0,
    interfaceLAN1 = 1,
    interfaceUnknown = 0xFF
} EInterfaceIndex;

EInterfaceIndex getInterfaceIndex(void);

sd_bus* ipmid_get_sd_bus_connection(void);
sd_event* ipmid_get_sd_event_connection(void);

// move this from ipmid.hpp, which is now gone
// this should not be used. Use the channel API to get the channel size
// 把它从现已不存在的 ipmid.hpp 移过来
// 不应使用它。请使用 channel API 来获取通道大小
#define MAX_IPMI_BUFFER 64

#endif
