# IPMI机制讲解：一条 IPMI 命令进来，ipmid 内部到底怎么转

> 业务背景（这条命令是干嘛的、为什么要转 D-Bus）请看同目录《IPMI业务详解.md》。本篇只回答"内部怎么转"。
>
> 面向有 openUBMC 经历的 Java 程序员：**先把一条真命令从头走到尾，再回头拆各个机制**。所有 `路径:行号` 相对于 `phosphor-host-ipmid/` 仓库根目录（这个仓库本身就是 ipmid 包，见 `meson.build:1-2`），基于 git HEAD `c23efd8`。

---

## 0. 先讲一个真实例子：Get Sensor Reading 的一个字节，怎么变成 D-Bus 调用再变回响应

我们先不讲架构，就把一条命令**从头到尾走一遍**。选 **Get Sensor Reading**（`netFn=0x04`, `cmd=0x2D`，定义见 `include/ipmid/api-types.hpp:349`、`include/ipmid/api-types.hpp:210`）。它特别适合讲机制，因为请求里只有一个字节 `sensorNum`。

### 0.1 完整链路代码走读

```text
① 帧进来：D-Bus 方法 execute
   ipmid-new.cpp:900-904
    server.add_interface("/xyz/openbmc_project/Ipmi",
                         "xyz.openbmc_project.Ipmi.Server");
    iface->register_method("execute", ipmi::executionEntry);
   → 外部（KCS/BT 驱动或 ipmitool 桥）调用 execute(netFn, lun, cmd, data, options)

② 建上下文：谁发的、什么权限
   ipmid-new.cpp:492        executionEntry(yield, m, netFn, lun, cmd, data, options)
   ipmid-new.cpp:502-510    sender = m.get_sender(); channel = channelFromMessage(m)
   ipmid-new.cpp:521-548    按 channel 是否有会话，取 privilege / userId / sessionId
   ipmid-new.cpp:581-585    Context + message::Request(ctx, data)  ← data 就是原始字节

③ 分流：按 netFn 查路由表
   ipmid-new.cpp:586        executeIpmiCommand(request)
   ipmid-new.cpp:332-344    netFn 是 2Ch/2Eh 走特殊表，否则走 handlerMap
   ipmid-new.cpp:241-251    key = makeCmdKey(netFn, cmd) = (netFn << 8) | cmd
                            （makeCmdKey 在 ipmid-new.cpp:103-106）

④ 鉴权 + 调用
   ipmid-new.cpp:251-266    handlers.find(key) → 检查 priv → handler->call(request)
   ipmid-new.cpp:262-265    request->ctx->priv < 要求的 Privilege → ccInsufficientPrivilege

⑤ 解包：字节 → 函数参数（框架自动做）
   include/ipmid/handler.hpp:184-203
    IpmiHandler::executeCallback:
      using CallbackSig = boost::callable_traits::args_t<Handler>;   (:189)
      request->unpack(unpackArgs);                                   (:198)
    → sensorNum 这个 uint8_t 从 payload 里被解出来
   include/ipmid/handler.hpp:226-266
    把 Context::ptr 这类"特殊首参"拼上，std::apply(handler_, *inputArgs) (:266)
    → 调用 ipmiSensorGetSensorReading(ctx, sensorNum)

⑥ handler 干活：转成 D-Bus
   sensorhandler.cpp:568-642
    ipmiSensorGetSensorReading(ctx, sensorNum)
      sensorNum==0xFF → 拒绝                       (:571-574)
      ipmi::sensor::sensors.find(sensorNum)        (:576-580)  ← 构建时生成的传感器表
      检查 Mutability::Read                        (:581-585)
      iter->second.getFunc(iter->second)           (:641-642)  ← std::function，绑到 D-Bus 读属性
   表来源：scripts/writesensor.cpp.mako:35 生成 IdInfoMap sensors

⑦ sdbusplus 出马
   libipmid/utils.cpp:203-225  getService() → ObjectMapper.GetObject 找服务名
   libipmid/utils.cpp:89-105   getDbusProperty() → Properties.Get → reply.read

⑧ 打包回响应
   chassishandler 等 handler 返回 ipmi::RspType<...>
   include/ipmid/handler.hpp:298-305
    response->cc = cc; response->pack(*payload)  ← 字段按位打包回字节
   ipmid-new.cpp:588        dbusResponse(response->cc, response->payload.raw)
    → 返回 tuple(retNetFn, lun, cmd, cc, data)，外层驱动送回主机
```

### 0.2 用 Java 的话把这条链子再说一遍

```text
Spring Boot 收到 HTTP 请求
  → DispatcherServlet              ≈ executionEntry（ipmid-new.cpp:492）
  → HandlerMapping 找 Controller    ≈ handlerMap 查表（ipmid-new.cpp:241-266）
  → Interceptor / 权限校验           ≈ filterIpmiCommand + priv 检查（ipmid-new.cpp:224-239, 262-265）
  → @RequestBody 反序列化           ≈ payload.unpack（include/ipmid/handler.hpp:198）
  → Controller 方法                 ≈ ipmiSensorGetSensorReading（sensorhandler.cpp:568）
  → Feign 调下游服务                ≈ ipmi::getDbusProperty（libipmid/utils.cpp:89-105）
  → @ResponseBody 序列化            ≈ response->pack（include/ipmid/handler.hpp:298-304）
```

**四个最该记住的对应关系**：

| C++ 里的东西 | Java 类比 | 溯源 |
| --- | --- | --- |
| `handlerMap`（unordered_map） | Spring 的 `HandlerMapping` 注册表 | `ipmid-new.cpp:112-115` |
| `ipmi::registerHandler(...)` | `@GetMapping` + 注解扫描注册 | `include/ipmid/handler.hpp:574-580`、`chassishandler.cpp:2539-2601` |
| `Payload::pack/unpack` | Jackson 编解码器 | `include/ipmid/message.hpp:112-131`、`include/ipmid/message.hpp:242-267`、`include/ipmid/message.hpp:416-446` |
| `std::apply(handler_, ...)` | 反射调用 Controller 方法 | `include/ipmid/handler.hpp:266` |

---

## 1. 先看构建关系：meson.build 里的进程 / 库 / 子目录

在钻代码之前，先看清楚这个工程最终产出什么。下面直接从 `meson.build` 读出来：

### 1.1 产出的目标一览

| 类型 | 名字 | 源码 / 说明 | 溯源 |
| --- | --- | --- | --- |
| 可执行程序 | `ipmid` | `ipmid-new.cpp` + `host-cmd-manager.cpp` + `settings.cpp` | `meson.build:331-343` |
| 共享库 | `libipmid.so` | `libipmid/sdbus-asio.cpp`、`signals.cpp`、`systemintf-sdbus.cpp`、`utils.cpp` | `libipmid/meson.build:19-31` |
| 静态库 | `libapp.a` | `app/channel.cpp`、`watchdog.cpp`、`watchdog_service.cpp` | `app/meson.build:5-12` |
| 共享库（provider） | `libipmi20.so` | 所有 command handler（见下表） | `meson.build:314-323` |
| 共享库（provider） | `libsysintfcmds.so` | `systemintfcmds.cpp`、`host-interface.cpp` | `meson.build:236-246` |
| 共享库（provider） | `libdynamiccmds.so` | 动态传感器栈，条件编译 | `meson.build:345-359` |
| 共享库（可选） | `libwhitelist.so` | IPMI 命令白名单过滤 | `meson.build:186-220` |

### 1.2 `libipmi20.so` 里塞了哪些 handler（这是干货）

```cpp
// meson.build:285-312
libipmi20_src = [
    'app/channel.cpp',
    'app/watchdog.cpp',
    'app/watchdog_service.cpp',
    'apphandler.cpp',
    'sys_info_param.cpp',
    'sensorhandler.cpp',
    'storagehandler.cpp',
    'chassishandler.cpp',
    'dcmihandler.cpp',
    'ipmisensor.cpp',
    'transporthandler.cpp',
    'globalhandler.cpp',
    'groupext.cpp',
    'selutility.cpp',
    'ipmi_fru_info_area.cpp',
    'read_fru_data.cpp',
    'sensordatahandler.cpp',
    'user_channel/channelcommands.cpp',
    'user_channel/user_layer.cpp',
    ...
]
```

按职责归类：

- **chassis / app / sensor / storage / DCMI / transport / global / group**：标准 IPMI 命令 handler。
- **selutility / ipmi_fru_info_area / read_fru_data / sensordatahandler**：SEL 与 FRU、传感器数据的工具层。
- **user_channel 下**：用户和通道管理命令。
- provider 都装到 `libdir/ipmid-providers`（`meson.build:320`、`meson.build:245`），路径由 `HOST_IPMI_LIB_PATH` 宏给出（`meson.build:48`），默认值 `/usr/lib/ipmid-providers/`（`meson.options:99`）。**主进程启动时就是去这个目录 dlopen 这些 .so**（`ipmid-new.cpp:868-869`）。

### 1.3 子目录一览

| 子目录 | 作用 | 溯源 |
| --- | --- | --- |
| `libipmid/` | 通用工具库：D-Bus 封装、事件、日志、entity map | `libipmid/meson.build:1-37` |
| `include/` | 对外头文件：`ipmid/api-types.hpp`、`handler.hpp`、`message.hpp` 等 | `meson.build:177`、`include/ipmid/` 目录 |
| `app/` | channel、watchdog 相关命令 | `app/meson.build:1-14` |
| `user_channel/` | 用户/通道/密码/加密套件管理 | `meson.build:178` |
| `dbus-sdr/` | 动态传感器栈（sensorcommands / storagecommands / sdrutils） | `meson.build:346`、`dbus-sdr/meson.build` |
| `transport/serialbridge/` | 串口桥（仅 `transport-implementation=serial` 时编） | `transport/meson.build:1-3` |
| `oem/` | OEM provider | `meson.build:376-377` |
| `scripts/` | 构建期代码生成（传感器表等） | `meson.build:179` |

> **一句话**：`ipmid` 主进程很小（就三个 .cpp），绝大多数命令逻辑在 `libipmi20.so` 等 provider 里，靠 dlopen 在运行时加载。这就是它的插件架构。

---

## 2. transport：KCS / BT / LAN 在这里到底是什么角色

这是最容易混淆的一节，先把名词拆开：

### 2.1 硬件层 KCS/BT —— 不在本仓库

- KCS（Keyboard Controller Style）和 BT（Block Transfer）是**主机和 BMC 之间的物理/链路接口**，字节级驱动在独立仓库里。
- 本仓库 `transport/` 目录只有 `serialbridge/`（`transport/meson.build:1-3`），是串口桥，不是 KCS/BT。
- 这些驱动把解出来的 IPMI 帧，通过 D-Bus 送进 ipmid：新路径调 `execute` 方法（`ipmid-new.cpp:900-904`）；老路径发 `org.openbmc.HostIpmi.ReceivedMessage` signal（`ipmid-new.cpp:871-875` 的 match，`ipmid-new.cpp:767-808` 的 `handleLegacyIpmiCommand`）。

### 2.2 协议层"Transport NetFn" —— 在本仓库

`transporthandler.cpp` 处理的是 IPMI 规范里 NetFn=0x0C 的**传输类命令**，常量定义在 `include/ipmid/api-types.hpp:353`：

```cpp
// transporthandler.cpp:1830-1846
void registerNetFnTransportFunctions()
{
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnTransport,
                          ipmi::transport::cmdSetLanConfigParameters,
                          ipmi::Privilege::Admin, ipmi::transport::setLan);
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnTransport,
                          ipmi::transport::cmdGetLanConfigParameters,
                          ipmi::Privilege::Operator, ipmi::transport::getLan);
    ...
}
```

也就是 **LAN / SOL 配置命令**（Set/Get LAN Configuration、Set/Get SOL Configuration），跟"字节从 KCS 搬上来"完全不是一回事。代码注释也直接写了 "LAN Configuration Parameters"（`transporthandler.cpp:722`）。

> 记忆法：**KCS/BT 是"线"，transport NetFn 是"线上跑的那种命令"**。前者在别的仓库，后者在 `transporthandler.cpp`。

### 2.3 通道（channel）模型

ipmid 用 channel 区分请求来源。常量在 `include/ipmid/api-types.hpp:52-61`（如 `channelSystemIface = 0x0F`、`channelPrimaryIpmb = 0x00`）。运行时，**D-Bus 发送者名会被映射到 channel 号**：

- 前缀 `xyz.openbmc_project.Ipmi.Channel`（`ipmid-new.cpp:366-367`）。
- 启动时 `doListNames()` 列所有 D-Bus 名（`ipmid-new.cpp:400-427`），匹配前缀的调 `updateOwners()`（`ipmid-new.cpp:368-398`），用 `GetNameOwner` 拿到唯一名，存进 `uniqueNameToChannelNumber`（`ipmid-new.cpp:362`、`ipmid-new.cpp:385`）。
- 运行期 `nameChangeHandler`（`ipmid-new.cpp:429-466`）监听 NameOwnerChanged 动态更新。
- 收到请求时 `channelFromMessage()`（`ipmid-new.cpp:470-489`）按 sender 查表；查不到就退回 `INTRABMC`（`ipmid-new.cpp:470`、`ipmid-new.cpp:480-484`）。

> Java 类比：这就是一个"发送方身份 → 逻辑通道号"的转换器，类似网关根据请求的来源 IP/证书映射成内部的 tenantId。

---

## 3. command handler 的注册与分发

### 3.1 注册：靠 `__attribute__((constructor))` 隐式完成

没有 `main()` 里逐个调用注册函数。每个 handler 文件用 GCC 的 constructor 属性，在 `.so` 被 dlopen 时自动执行注册函数：

```cpp
// chassishandler.cpp:87
void registerNetFnChassisFunctions() __attribute__((constructor));
// chassishandler.cpp:2539-2601 是函数体，里面一堆 ipmi::registerHandler(...)
```

同样的模式在：

- `sensorhandler.cpp:53` → 函数体 `sensorhandler.cpp:1601-1667`
- `storagehandler.cpp:31` → 函数体 `storagehandler.cpp:861-...`
- `transporthandler.cpp:1828` → 函数体 `transporthandler.cpp:1830-1846`

注册 API 本身在 `include/ipmid/handler.hpp:574-580`：

```cpp
template <typename Handler>
bool registerHandler(int prio, NetFn netFn, Cmd cmd, Privilege priv,
                     Handler&& handler)
{
    auto h = ipmi::makeHandler(std::forward<Handler>(handler));  // :578
    return impl::registerHandler(prio, netFn, cmd, priv, h);     // :579
}
```

- `makeHandler`（`include/ipmid/handler.hpp:537-543`）把普通函数/lambda 包成 `IpmiHandler` 对象（智能指针）。
- `impl::registerHandler` 的**实现**在 `ipmid-new.cpp:137-159`：

```cpp
    unsigned int netFnCmd = makeCmdKey(netFn, cmd);          // :148
    HandlerTuple item(prio, priv, handler);                  // :149
    auto& mapCmd = handlerMap[netFnCmd];                     // :152
    if (!std::get<HandlerBase::ptr>(mapCmd) ||
        std::get<int>(mapCmd) <= prio)                       // :153 优先级比较
    {
        mapCmd = item;
        return true;
    }
```

**优先级机制**：数字小的优先（`prioOpenBmcBase=10`，`prioOemBase=20`，`prioOdmBase=30`，`prioCustomBase=40`，见 `include/ipmid/api-types.hpp:46-50`）。高优先级（数字小）的命令可以覆盖低优先级的——这就是 OEM 能覆盖基础实现的办法。注释直接写了 "Set the priority as the lowest number that is necessary so it is possible that others can override it if desired."（`include/ipmid/api-types.hpp:40-45`）。

> **Java 类比**：`__attribute__((constructor))` ≈ Spring 组件扫描时执行 `@Bean` 注册方法；`prio` ≈ `@Order`，数字小的排在前面；`handlerMap` ≈ `RequestMappingHandlerMapping` 内部的 `Map<RequestMappingInfo, HandlerMethod>`。

### 3.2 三张路由表

`ipmid-new.cpp:112-132` 定义了核心数据结构：

| 表 | 键 | 用途 | 溯源 |
| --- | --- | --- | --- |
| `handlerMap` | `(netFn << 8) \| cmd` | 标准命令 | `ipmid-new.cpp:112-115` |
| `groupHandlerMap` | `(group << 8) \| cmd` | NetFn 2Ch 的 group 命令 | `ipmid-new.cpp:117-120` |
| `oemHandlerMap` | `(iana << 8) \| cmd` | NetFn 2Eh 的 OEM 命令 | `ipmid-new.cpp:122-125` |
| `filterList` | 按 prio 排序的链表 | 固件防火墙 / 白名单过滤 | `ipmid-new.cpp:127-132` |

`makeCmdKey(cluster, cmd) = (cluster << 8) | cmd`（`ipmid-new.cpp:103-106`）——把一个 16 位的键同时装下"簇号"和"命令号"。

### 3.3 分发：一次查找 + 两级回退

`executeIpmiCommandCommon`（`ipmid-new.cpp:241-291`）是分发核心：

1. 先过过滤器 `filterIpmiCommand(request)`（`ipmid-new.cpp:247`，过滤器实现 `ipmid-new.cpp:224-239`）。
2. 精确查找 `handlers.find(makeCmdKey(keyCommon, cmd))`（`ipmid-new.cpp:249-252`）。
3. **找不到就查通配命令** `cmdWildcard = 0xFF`（`ipmid-new.cpp:270-288`，常量在 `include/ipmid/api-types.hpp:369`）。
4. 检查权限 `request->ctx->priv < std::get<Privilege>(chosen)`（`ipmid-new.cpp:262-265`）。
5. 调用 `std::get<HandlerBase::ptr>(chosen)->call(request)`（`ipmid-new.cpp:266`）。

对于 Group/OEM 命令，还有"先拆首字节"的步骤：

- `executeIpmiGroupCommand`（`ipmid-new.cpp:293-310`）：从 payload 解出 1 字节 group，再查 `groupHandlerMap`。
- `executeIpmiOemCommand`（`ipmid-new.cpp:312-330`）：解出 3 字节 IANA（`uint24_t`，`ipmid-new.cpp:315-320`），再查 `oemHandlerMap`，响应前还要把这 3 字节加回去（`ipmid-new.cpp:326-328`）。

总入口 `executeIpmiCommand`（`ipmid-new.cpp:332-344`）按 netFn 决定走哪张表。

---

## 4. ipmid 主循环：进程起来之后在干什么

`main()` 在 `ipmid-new.cpp:830-917`，按顺序做这些事：

```text
830  main
833-834  建 boost::asio::io_context（事件循环核心）
835-852  连 D-Bus：带 -session 参数连 user bus，否则连 system bus
853-854  把 sd_bus 包成 sdbusplus::asio::connection
858-859  起一个空转定时器（注释说这是 hack，为了保持 sd_event 队列活着）
863      sdbusplus::asio::sd_event_wrapper 把 sd_event 接到 asio
865      创建 host command Manager（BMC → 主机方向）
868-869  loadProviders(HOST_IPMI_LIB_PATH)  ← dlopen 所有 provider，触发 constructor 注册
871-876  兼容老路径：监听 org.openbmc.HostIpmi 的 ReceivedMessage signal
878-884  监听 NameOwnerChanged，维护 channel ↔ D-Bus 名映射
886-896  注册 SIGINT/SIGTERM 处理，信号来了 io->stop()
898      申请总线名 xyz.openbmc_project.Ipmi.Host
900-904  注册 /xyz/openbmc_project/Ipmi 上的 execute 方法
906      io->run()  ← 主循环，之后所有请求都在回调里处理
908-914  退出时清空各 handler map 并 dlclose providers
```

几个值得注意的点：

- **provider 加载是注册的关键时机**：`loadProviders`（`ipmid-new.cpp:653-687`）遍历 `/usr/lib/ipmid-providers/`（`meson.options:99`），挑出带 `.so` 的文件（`ipmid-new.cpp:650`、`ipmid-new.cpp:665-674`），逐个构造 `IpmiProvider`（`ipmid-new.cpp:611-634`，内部 `dlopen(..., RTLD_NOW)` 在 `ipmid-new.cpp:616`）。**dlopen 的瞬间，各 .so 里的 constructor 函数执行，handler 被塞进 map。**
- **RAII 管理 dlclose**：`IpmiProvider` 析构函数调 `dlclose`（`ipmid-new.cpp:636-642`），`providers` 是 `std::forward_list`（`ipmid-new.cpp:678`、`ipmid-new.cpp:686`）。
- **老路径的响应方式不一样**：`handleLegacyIpmiCommand`（`ipmid-new.cpp:767-808`）最后用 `callDbusMethod(..., "sendMessage", ...)`（`ipmid-new.cpp:795-797`）主动回调发送方；新路径则是方法调用的返回值（`ipmid-new.cpp:588`）。

> **Java 类比**：`main()` 像一个极简的 Spring Boot 启动类——建事件循环 ≈ Netty EventLoop，dlopen provider ≈ 扫描并加载插件 jar（SPI / ServiceLoader），`register_method("execute", ...)` ≈ 暴露一个 RPC 端点，`io->run()` ≈ 进入 serve 循环。

---

## 5. D-Bus 适配层：handler 怎么读写别的服务的状态

handler 不直接碰硬件，全靠 `libipmid/utils.cpp` 这一层。最常用的三个：

### 5.1 `getService()` —— 服务发现

```cpp
// libipmid/utils.cpp:203-225
std::string getService(sdbusplus::bus_t& bus, const std::string& intf,
                       const std::string& path)
{
    auto mapperCall = bus.new_method_call(
        ObjectMapper::default_service, ObjectMapper::instance_path,
        ObjectMapper::interface, ObjectMapper::method_names::get_object); // :206-208
    mapperCall.append(path);
    mapperCall.append(std::vector<std::string>({intf}));                  // :210-211
    auto mapperResponseMsg = bus.call(mapperCall);                        // :213
    ...
    return mapperResponse.begin()->first;                                 // :224
}
```

它问的是 **ObjectMapper**（`libipmid/utils.cpp:16`、`libipmid/utils.cpp:28`）："谁提供了 `path` 上的 `intf` 接口？" 返回服务名。这是 OpenBMC 里"不写死服务名"的标准做法。

> Java 类比：按 `(接口名, 路径)` 到注册中心查实例，等价于 Feign/Eureka 的服务发现。

### 5.2 `getDbusProperty()` / `getAllDbusProperties()` —— 读属性

```cpp
// libipmid/utils.cpp:89-105
Value getDbusProperty(sdbusplus::bus_t& bus, const std::string& service,
                      const std::string& objPath, const std::string& interface,
                      const std::string& property, ...)
{
    auto method = bus.new_method_call(service.c_str(), objPath.c_str(),
                                      PROP_INTF, METHOD_GET);  // :96-97
    method.append(interface, property);                         // :99
    auto reply = bus.call(method, timeout.count());             // :101
    reply.read(value);                                          // :102
    return value;
}
```

就是标准 D-Bus `org.freedesktop.DBus.Properties.Get`。`getAllDbusProperties`（`libipmid/utils.cpp:107-123`）对应 `GetAll`。再看 `getManagedObjects`（`libipmid/utils.cpp:125-138`）对应 ObjectManager 的 `GetManagedObjects`。

### 5.3 同步版 vs 协程版

上面三个是**同步**版（直接 `bus.call`）。`utils.cpp` 还有一套带 `Context::ptr` 的**异步**版，配合 `boost::asio::yield_context`：

- `getService(Context::ptr ctx, ...)`（`libipmid/utils.cpp:282-...`）：内部用 `ctx->yield`（`libipmid/utils.cpp:289-291`）。
- `getAllDbusProperties(Context::ptr ctx, ...)`（`libipmid/utils.cpp:374-...`）。
- `callDbusMethod(Context::ptr ctx, ...)`（`libipmid/utils.cpp:439-...`）。

`Context` 结构体把 `bus`、`netFn`、`cmd`、`channel`、`userId`、`sessionId`、`priv`、`yield` 全装在里面（`include/ipmid/message.hpp:36-71`）。handler 只要把 `Context::ptr` 作为第一个参数，框架就自动注入（`include/ipmid/handler.hpp:237-244`）。

> **Java 类比**：`yield_context` 就是 C++ 版的"协程式 async/await"——写起来像同步代码，实际把线程让出去等 I/O。你可以把它想成 Kotlin 的 `suspend` 函数或者 Java 里 CompletableFuture 的同步写法。handler 签名里出现 `Context::ptr`，约等于 Spring 方法参数里出现 `HttpServletRequest`——框架帮你注入。

### 5.4 handler 里的真实用法对照

| 场景 | 调用 | 溯源 |
| --- | --- | --- |
| Get Chassis Status 读电源状态 | `getService` + `getDbusProperty(..., current_power_state)` | `chassishandler.cpp:1036-1041` |
| Get Chassis Status 读恢复策略 | `getDbusProperty(..., power_restore_policy)` | `chassishandler.cpp:1002-1005` |
| Chassis Control 写开机 | `setDbusProperty(..., requested_host_transition, ...)` | `chassishandler.cpp:891-893` |
| Get SEL Info 读日志缓存 | `readLoggingObjectPaths` + 本地缓存 | `storagehandler.cpp:192`、`storagehandler.cpp:186-208` |

---

## 6. 用户权限与会话

### 6.1 权限等级

```cpp
// include/ipmid/api-types.hpp:69-77
enum class Privilege : uint8_t
{
    None = 0x00,
    Callback,
    User,
    Operator,
    Admin,
    Oem,
};
```

命令注册时指定最低权限（如 Get Chassis Status 是 `User`，`chassishandler.cpp:2568`；Set LAN Config 是 `Admin`，`transporthandler.cpp:1834`）。运行时在 `executeIpmiCommandCommon` 里比对（`ipmid-new.cpp:262-265`），不够就返回 `ccInsufficientPrivilege`。

### 6.2 请求上下文里的凭据怎么来

`executionEntry`（`ipmid-new.cpp:492-589`）分两种情况：

1. **有会话的通道**（`getChannelSessionSupport(channel) != none`，`ipmid-new.cpp:523`）：从 `options` 里取 `privilege`、`userId`、`currentSessionId`（`ipmid-new.cpp:527-533`），转成 `Privilege` / `userId` / `sessionId`。
2. **无会话通道**：默认 `Privilege::Admin`（`ipmid-new.cpp:546-548`）。IPMB 还会从 options 里取 `rqSA`、`hostId`（`ipmid-new.cpp:550-572`）。

channel 的会话能力来自 `user_channel/channel_layer.cpp:73-75` 的 `getChannelSessionSupport()`，最终读 channel 配置（`user_channel/channel_mgmt.cpp:382-386`）。

### 6.3 用户/通道管理命令

`user_channel/` 目录下是另一批 IPMI 命令 handler，比如 `usercommands.cpp`、`channelcommands.cpp`、`cipher_mgmt.cpp`、`passwd_mgr.cpp`。它们被编进 `libipmi20.so`（`meson.build:303-304`），和 chassis/sensor 命令走同一套注册分发机制。

> Java 类比：`Privilege` 枚举 + channel 映射 ≈ Spring Security 的 `GrantedAuthority` + 租户/渠道上下文；`user_channel/` 下的命令就是"用户管理 REST API"的 IPMI 版本。

---

## 7. 特殊要点：几个容易踩的坑

### 7.1 编解码是模板魔法，不是手写 switch

`IpmiHandler::executeCallback`（`include/ipmid/handler.hpp:184-306`）用 `boost::callable_traits` 在**编译期**读出 handler 的函数签名（`include/ipmid/handler.hpp:189-194`），推断要解包哪些参数，然后 `std::apply` 调用（`include/ipmid/handler.hpp:266`）。所以你写一个 handler 时：

- 第一个参数可写 `boost::asio::yield_context`、`ipmi::Context::ptr`、`ipmi::message::Request::ptr` 之一，框架自动注入（`include/ipmid/handler.hpp:204-222`、`include/ipmid/handler.hpp:226-256`）。
- 其余参数写普通类型（如 `uint8_t sensorNum`），框架自动从字节流解包（`include/ipmid/handler.hpp:198`）。
- 返回值写 `ipmi::RspType<...>`，框架自动打包（`include/ipmid/handler.hpp:298-304`）。

写错签名会在编译期报模板错误，这是 C++ 模板的代价，换来了运行时零反射开销。

### 7.2 `ipmi::RspType` 与完成码

handler 返回的是 `ipmi::RspType<字段...>`，第一个元素是完成码（cc），后面是响应字段。`responseSuccess(...)` 构造成功响应（如 `chassishandler.cpp:1316-1331`），出错用 `responseInvalidFieldRequest()` 之类（`sensorhandler.cpp:573`）。框架把 cc 和 payload 分开处理（`include/ipmid/handler.hpp:298-305`）。

### 7.3 两套 handler 实现并存，靠编译开关切换

- 静态传感器 handler 在 `sensorhandler.cpp`，受 `#ifndef FEATURE_DYNAMIC_SENSORS` 保护（`sensorhandler.cpp:1606`）。
- 动态传感器在 `dbus-sdr/`，由 `-Ddynamic-sensors` 开启（`meson.options:155-160`，默认 disabled）；`meson.build:266-271` 决定要不要把 `dbus-sdr/storagecommands.cpp` 编进 ipmi20。
- `sensors-cache` 也默认关闭（`meson.options:181-186`），所以 `sensorhandler.cpp:589-639` 的缓存分支默认不走，走 `sensorhandler.cpp:640-650` 的 `#else`。

**读代码前先确认编译开关，否则会同时对上两套实现。**

### 7.4 BMC → 主机的反向命令：host-cmd-manager

前面讲的都是主机 → BMC。反方向（BMC 要通知主机，如 SMS_ATN）由 `host-cmd-manager.cpp` 负责：

- `ipmid_send_cmd_to_host()`（`ipmid-new.cpp:815-818`）把 `CommandHandler` 交给 `cmdManager->execute(...)`（`host-cmd-manager.cpp:147`）。
- `Manager` 构造函数监听 `/xyz/openbmc_project/state/host0` 的 PropertiesChanged（`host-cmd-manager.cpp:33-42`），上电时清队列。
- `getNextCommand()`（`host-cmd-manager.cpp:44-80`）是 READ_MSG_DATA 命令取的下一项；队列空时返回心跳 `CMD_HEARTBEAT`（`host-cmd-manager.cpp:54-61`）。

> Java 类比：这是一个**出站消息队列**——BMC 想让主机做事时先入队，主机通过 READ_MSG_DATA 来"拉"，类似主机周期性 poll 的一个任务队列。

### 7.5 过滤器（firmware firewall）

`filterList`（`ipmid-new.cpp:127-132`）里的过滤器在真正分发前执行（`filterIpmiCommand`，`ipmid-new.cpp:224-239`）。注册用 `registerFilter`（`ipmid-new.cpp:204-220`）。白名单功能由 `ipmi-whitelist` 选项控制（`meson.build:186-220`），对应 `whitelist-filter.cpp`。被过滤的命令**只有在 map 里存在时才返回过滤响应**（`ipmid-new.cpp:254-260` 的注释和逻辑），否则还是返回无效命令。

---

## 8. 一页速查表

| 你想找的东西 | 直接去哪 |
| --- | --- |
| 主进程入口、事件循环 | `ipmid-new.cpp:830-917` |
| 路由表定义 | `ipmid-new.cpp:112-132` |
| 注册实现（优先级） | `ipmid-new.cpp:137-159` |
| 分发实现 | `ipmid-new.cpp:241-344` |
| 请求入口 executionEntry | `ipmid-new.cpp:492-589` |
| 老式 KCS/BT signal 处理 | `ipmid-new.cpp:767-808` |
| handler 编解码模板 | `include/ipmid/handler.hpp:184-306` |
| 命令号 / NetFn / 权限枚举 | `include/ipmid/api-types.hpp:40-83`、`:165-250`、`:347-369` |
| D-Bus 工具函数 | `libipmid/utils.cpp:89-225` |
| Get Chassis Status | `chassishandler.cpp:1250-1332` |
| Get Sensor Reading | `sensorhandler.cpp:568-650` |
| Get SEL Info | `storagehandler.cpp:242-283` |
| 传感器表生成模板 | `scripts/writesensor.cpp.mako:35` |
| 构建目标一览 | `meson.build:236-377` |

业务视角（命令是干嘛的、和各服务怎么分工）请回到《IPMI业务详解.md》。
