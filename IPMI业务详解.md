# IPMI业务详解：你在 ipmitool 里敲下 `power status`，主机和 BMC 之间到底发生了什么

> 写给有 openUBMC 经历的 Java 程序员。你以前在 openUBMC 里知道「IPMI 是管主机的」，这里把这份心智搬到 OpenBMC 的 `phosphor-host-ipmid` 上：**先带你走一遍真实命令，再回头点概念**。
>
> 本文所有 `路径:行号` 都相对于 `phosphor-host-ipmid/` 仓库根目录。注意：**这个仓库本身就是 ipmid 这个包**，没有单独的 `host-ipmid/` 子目录——工程名直接写在 `meson.build:1-2`（`project('phosphor-host-ipmid', 'cpp', ...)`）。行号基于当前 git HEAD `c23efd8`。

---

## 0. 一句话说清业务

**IPMI 就是主机和 BMC 之间的一条"管理命令通道"：主机通过 KCS/BT 这些系统接口把 IPMI 命令送过来，BMC 这一侧的 `phosphor-host-ipmid` 进程把命令接住、翻译成 D-Bus 调用打到 state-manager / entity-manager / phosphor-logging 等服务上，再把结果编回 IPMI 响应送回主机。**

用你熟悉的 openUBMC 心智对照：

| openUBMC 里的说法 | 这里的对应物 | 溯源 |
| --- | --- | --- |
| IPMI 通道 / 管理面 | `xyz.openbmc_project.Ipmi.Server` 这个 D-Bus 接口上的 `execute` 方法 | `ipmid-new.cpp:900-904` |
| 命令号（NetFn + Cmd） | `netFn` + `cmd` 两个 uint8_t，值定义在 `api-types.hpp` | `include/ipmid/api-types.hpp:79-83`、`include/ipmid/api-types.hpp:347-364` |
| 命令处理函数 | `ipmi::registerHandler(...)` 注册的 handler lambda/函数 | `chassishandler.cpp:2566-2568` |
| 转成内部服务调用 | handler 里调 `ipmi::getDbusProperty` / `getService` | `chassishandler.cpp:1039-1041`、`libipmid/utils.cpp:89-105` |

一句话：**它是"翻译官"，不是"状态所有者"**。状态在 state-manager，日志在 phosphor-logging，传感器数值在 D-Bus 上的传感器服务里，ipmid 只是把这些拼成 IPMI 帧。

---

## 1. 真实场景一：`ipmitool power status` 这条命令怎么走到 BMC

### 1.1 先看真实代码：这条命令落到哪个 handler

`ipmitool power status` 在 IPMI 层就是发 **Get Chassis Status**（`netFn = 0x00`, `cmd = 0x01`）。这两个数字在代码里是这么定义的：

```cpp
// include/ipmid/api-types.hpp:165-168
namespace chassis
{
constexpr Cmd cmdGetChassisCapabilities = 0x00;
constexpr Cmd cmdGetChassisStatus = 0x01;   // <-- 就是它
```

```cpp
// include/ipmid/api-types.hpp:347
constexpr NetFn netFnChassis = 0x00;
```

handler 注册在这里（`chassishandler.cpp:2565-2568`）：

```cpp
    // <Get Chassis Status>
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnChassis,
                          ipmi::chassis::cmdGetChassisStatus,
                          ipmi::Privilege::User, ipmiGetChassisStatus);
```

四个参数翻译成人话：

- `prioOpenBmcBase`（值 10，`include/ipmid/api-types.hpp:46`）：注册优先级，数字小的先占坑。
- `netFnChassis` + `cmdGetChassisStatus`：路由键，合起来就是 `(0x00, 0x01)`。
- `Privilege::User`（枚举见 `include/ipmid/api-types.hpp:69-77`）：调用这条命令至少要 User 权限。
- `ipmiGetChassisStatus`：真正干活的 C++ 函数。

> **Java 类比**：`registerHandler(netFn, cmd, priv, fn)` 就是 Spring MVC 里 `@GetMapping("/chassis/status")` 挂在一个 Controller 方法上——`(netFn, cmd)` 是 URL，`priv` 是 `@PreAuthorize`，`fn` 是 Controller 方法本体。区别是这里没有注解扫描，而是靠一个 `__attribute__((constructor))` 函数在 `.so` 被加载时把路由塞进 map（`chassishandler.cpp:87` 声明、`chassishandler.cpp:2539-2601` 定义）。

### 1.2 完整链路（每一步都带 file:line）

```text
[主机侧 ipmitool]
  敲 `ipmitool power status`
        │
        │ 组一帧 IPMI 请求：netFn=0x00, cmd=0x01（Get Chassis Status）
        ▼
[KCS/BT 硬件驱动 —— 在别的仓库，不在本仓库]
  把字节从 KCS/BT 系统接口搬出来
        │
        │ 通过 D-Bus 调用 execute 方法
        ▼
[ipmid 主进程 —— 本仓库]
  ipmid-new.cpp:900-904
    注册 xyz.openbmc_project.Ipmi.Server 接口的 execute 方法
        │
        ▼
  ipmid-new.cpp:492        executionEntry(...) 被 D-Bus 调用进来
  ipmid-new.cpp:502-510    从消息里拿 sender，反查 channel（channelFromMessage）
  ipmid-new.cpp:521-548    按 channel 决定 privilege；无会话通道默认 Admin
  ipmid-new.cpp:581-585    组装 ipmi::Context + message::Request
        │
        ▼
  ipmid-new.cpp:586        executeIpmiCommand(request)
  ipmid-new.cpp:332-344    按 netFn 分流：普通命令走 executeIpmiCommandCommon
  ipmid-new.cpp:241-266    查 handlerMap → 检查权限 → handler->call(request)
        │
        ▼
  include/ipmid/handler.hpp:184-198
    IpmiHandler::executeCallback：把 payload 字节解包成函数参数
  include/ipmid/handler.hpp:266
    std::apply(handler_, *inputArgs)  ← 真正调用 ipmiGetChassisStatus
        │
        ▼
[handler 内部 —— 转成 D-Bus 调用]
  chassishandler.cpp:1250-1255       ipmiGetChassisStatus 入口
  chassishandler.cpp:1254            power_policy::getPowerRestorePolicy()
  chassishandler.cpp:1255            power_policy::getPowerStatus()
        │
        ├─→ chassishandler.cpp:1028-1047  getPowerStatus
        │      ipmi::getService(*busp, ChassisState::interface,
        │                       "/xyz/openbmc_project/state/chassis0")   (:1036-1037)
        │      ipmi::getDbusProperty(..., current_power_state)           (:1039-1041)
        │      → 读 state-manager 的 D-Bus 属性，判断是否 ".On"
        │
        └─→ chassishandler.cpp:989-1020   getPowerRestorePolicy
               ipmi::getDbusProperty(..., power_restore_policy)          (:1002-1005)
               → 读控制电源恢复策略的服务
        │
        ▼
  libipmid/utils.cpp:89-105     getDbusProperty：new_method_call → bus.call → reply.read
  libipmid/utils.cpp:203-225    getService：问 ObjectMapper 的 GetObject 找服务名
        │
        ▼
  chassishandler.cpp:1316-1331  responseSuccess(...) 把各字段打包成 RspType
  include/ipmid/handler.hpp:298-305  把 cc 和 payload 塞回 Response
  ipmid-new.cpp:588            dbusResponse(cc, payload) 返回给调用方
        │
        ▼
[KCS/BT 硬件驱动] 把响应字节送回主机 → ipmitool 打印 "Chassis Power is on"
```

### 1.3 分段用什么 Java 概念去理解

| 这段在干什么 | C++ 里的东西 | Java 类比 |
| --- | --- | --- |
| 把 `(netFn, cmd)` 映射到函数 | `handlerMap`（`ipmid-new.cpp:112-115`）| Spring MVC 的 `HandlerMapping`（`@GetMapping` → Controller 方法） |
| 把请求字节变成函数实参 | `payload.unpack(unpackArgs)`（`include/ipmid/handler.hpp:196-203`）| Jackson 反序列化 / Spring 的 `@RequestBody` 参数解析器 |
| 把函数返回值变成响应字节 | `response->pack(*payload)`（`include/ipmid/handler.hpp:298-304`）| `@ResponseBody` + Jackson 序列化 |
| 找 D-Bus 服务名 | `getService()`（`libipmid/utils.cpp:203-225`）| 服务发现 / 注册中心按接口名查实例 |
| 读别的服务的状态 | `getDbusProperty()`（`libipmid/utils.cpp:89-105`）| 调 Feign/RPC 客户端拿远端属性 |
| 决定谁有权调用 | `priv < 要求的 Privilege`（`ipmid-new.cpp:262-265`）| Spring Security 方法级鉴权 |

### 1.4 一个必须澄清的边界：KCS/BT 字节驱动不在这个仓库

团队里常有人问"KCS、BT 的实现在哪"。就这个仓库的当前代码而言：

- `transport/` 目录下**只有一个 `serialbridge/` 子目录**（`transport/meson.build:1-3`：只有 `transport-implementation == 'serial'` 时才 `subdir('serialbridge')`）。
- 真正和 BMC 硬件打交道的 KCS/BT 驱动，是在**另外的仓库**里（phosphor-ipmi-kcs / phosphor-ipmi-bt 这类），它们把解出来的 IPMI 帧通过 D-Bus 丢给 ipmid 的 `execute` 方法（`ipmid-new.cpp:900-904`）。
- 本仓库里名字带 transport 的 `transporthandler.cpp`，处理的是 **IPMI 协议里 NetFn=0x0C 的"传输类命令"**（如 Set/Get LAN Configuration Parameters，注册见 `transporthandler.cpp:1830-1846`），不是 KCS/BT 的字节驱动。
- 老式的 KCS/BT 走法还留了一条兼容路径：监听 D-Bus signal `org.openbmc.HostIpmi.ReceivedMessage`（`ipmid-new.cpp:871-876`），由 `handleLegacyIpmiCommand` 处理（`ipmid-new.cpp:767-808`），最后再调 `sendMessage` 把响应送回去（`ipmid-new.cpp:791-797`）。

**结论：本仓库负责"收到帧之后怎么办"，不负责"字节怎么从硬件搬上来"。** 这条边界记住了，读代码就不会迷路。

### 1.5 顺手看清 execute 方法的"入参和出参"

新路径的入口 `executionEntry` 签名（`ipmid-new.cpp:492-494`）：

```cpp
auto executionEntry(boost::asio::yield_context yield, sdbusplus::message_t& m,
                    NetFn netFn, uint8_t lun, Cmd cmd, ipmi::SecureBuffer& data,
                    std::map<std::string, ipmi::Value>& options)
```

- `netFn / lun / cmd`：帧头里的三个字段。
- `data`（`ipmi::SecureBuffer`）：请求体原始字节，就是后面被 `unpack` 的东西。
- `options`：会话相关的附加信息（privilege、userId、currentSessionId 等，在 `ipmid-new.cpp:527-533` 取用）。

返回值的形状也很关键（`ipmid-new.cpp:496-501`）：

```cpp
    const auto dbusResponse =
        [netFn, lun, cmd](Cc cc, const ipmi::SecureBuffer& data = {}) {
            constexpr uint8_t netFnResponse = 0x01;
            uint8_t retNetFn = netFn | netFnResponse;
            return std::make_tuple(retNetFn, lun, cmd, cc, data);
        };
```

注意 `netFn | 0x01`——IPMI 协议里**响应的 NetFn 最低位要置 1**，表示这是"响应"而不是"请求"。老路径里也有同样一行 `netFn |= 0x01`（`ipmid-new.cpp:789`）。这种协议细节是写死在代码里的，看一眼就记住了。

> Java 类比：`dbusResponse` 这个 lambda 就是统一的"响应包装器"，类似 Spring 里的 `ResponseEntity` 工厂——把散落的 `cc` 和 `data` 组装成固定形状的返回对象。

---

## 2. 真实场景二：读传感器 / 读 SEL / 设置开机模式

场景一是"查询一个状态"，下面三条是另外三类典型活：**读传感器（从属性拼读数）**、**读 SEL（从日志拼记录）**、**设置开机模式/电源（往 state-manager 写状态）**。

### 2.1 Get Sensor Reading —— 读传感器

`ipmitool sensor get <id>` 发的是 **Get Sensor Reading**（`netFn=0x04`, `cmd=0x2D`）。

命令号定义：

```cpp
// include/ipmid/api-types.hpp:349, 210
constexpr NetFn netFnSensor = 0x04;
constexpr Cmd cmdGetSensorReading = 0x2D;
```

注册（`sensorhandler.cpp:1617-1620`）：

```cpp
    // <Get Sensor Reading>
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnSensor,
                          ipmi::sensor_event::cmdGetSensorReading,
                          ipmi::Privilege::User, ipmiSensorGetSensorReading);
```

handler 本体在 `sensorhandler.cpp:568-569`，签名很能说明问题：

```cpp
    ipmiSensorGetSensorReading([[maybe_unused]] ipmi::Context::ptr& ctx,
                               uint8_t sensorNum)
```

注意 `uint8_t sensorNum`——**请求里的一个字节，框架自动解包成函数参数**（解包逻辑在 `include/ipmid/handler.hpp:196-203`）。这跟 Java 里 `@RequestParam int sensorNum` 是一回事。

它干的事（默认构建路径，`sensors-cache` 默认关闭，见 `meson.options:181-186`）：

1. `sensorNum == 0xFF` 直接拒绝（`sensorhandler.cpp:571-574`）。
2. 用 `sensorNum` 查全局传感器表 `ipmi::sensor::sensors`（`sensorhandler.cpp:576-580`）。这张表是**构建时用 mako 模板生成的**，不是手写的：`scripts/writesensor.cpp.mako:35` 里写着 `extern const IdInfoMap __attribute__((init_priority(101))) sensors = {`，每个表项的 D-Bus 路径、接口、换算系数都在 `scripts/writesensor.cpp.mako:82-90` 生成。
3. 检查可读性 `Mutability::Read`（`sensorhandler.cpp:581-585`）。
4. 调表项里的 `getFunc` 读数（`sensorhandler.cpp:641-642`）：

```cpp
        ipmi::sensor::GetSensorResponse getResponse =
            iter->second.getFunc(iter->second);
```

这个 `getFunc` 是 `std::function<GetSensorResponse(const Info&)>`（`include/ipmid/types.hpp:179`），模板生成时根据传感器用的 D-Bus 接口绑到 `get::` / `inventory::get::` 开头的函数上（`scripts/writesensor.cpp.mako:12-24`）。往下一层就是 sdbusplus 读属性。**一句话：传感器号 → 生成的表 → D-Bus 属性 → IPMI 读数字节。**

> 另有一条"动态传感器"路线（`dbus-sdr/sensorcommands.cpp`），由 `-Ddynamic-sensors` 开启（`meson.options:155-160`，默认 disabled）；开启后 `sensorhandler.cpp:1606` 的 `#ifndef FEATURE_DYNAMIC_SENSORS` 会让上面这个静态 handler 不再注册。读代码时先看编译开关，别对着两套实现发懵。

### 2.2 Get SEL Info —— 读系统事件日志概况

`ipmitool sel info` 发的是 **Get SEL Info**。这里有个容易踩坑的点：**代码里 `cmdGetSelInfo` 是 0x40，不是 0x43**，`0x43` 是 Get SEL Entry：

```cpp
// include/ipmid/api-types.hpp:238-241
constexpr Cmd cmdGetSelInfo = 0x40;
constexpr Cmd cmdGetSelAllocationInfo = 0x41;
constexpr Cmd cmdReserveSel = 0x42;
constexpr Cmd cmdGetSelEntry = 0x43;
```

> 这就是"术语以代码为锚"的意义——网上资料写 0x43 的都有，但**以本仓库 `include/ipmid/api-types.hpp:238` 为准**。

注册（`storagehandler.cpp:871-874`）：

```cpp
    // <Get SEL Info>
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnStorage,
                          ipmi::storage::cmdGetSelInfo, ipmi::Privilege::User,
                          ipmiStorageGetSelInfo);
```

handler 本体 `ipmiStorageGetSelInfo()` 在 `storagehandler.cpp:242-283`。它**不直接查数据库**，而是查自己维护的缓存 `selCacheMap`：

- `storagehandler.cpp:248-252`：缓存没初始化就调 `initSELCache()`。
- `storagehandler.cpp:253-255`：条目数 = `selCacheMap.size()`。
- `storagehandler.cpp:259-261`：最近添加时间取自最后一条记录的日志对象路径。
- `storagehandler.cpp:276-282`：`responseSuccess(...)` 拼出 SEL 版本、条目数、剩余空间、时间戳和一组 capability 位。

那缓存从哪来？`initSELCache()`（`storagehandler.cpp:186-208`）：

1. `registerSelCallbackHandler()`（`storagehandler.cpp:188`）挂 D-Bus signal：InterfacesAdded / InterfacesRemoved / PropertiesChanged（`storagehandler.cpp:159-184`），日志增删时实时改缓存（`selAddedCallback` / `selRemovedCallback` / `selUpdatedCallback`，`storagehandler.cpp:107-157`）。
2. `ipmi::sel::readLoggingObjectPaths(paths)`（`storagehandler.cpp:192`）从 phosphor-logging 把现有日志对象路径拉过来。
3. 每条路径经 `parseLoggingEntry()`（`storagehandler.cpp:89-105`）转成 SEL 记录塞进 `selCacheMap`。

> **Java 类比**：`selCacheMap` 就像一个本地 Caffeine 缓存，靠监听消息总线（D-Bus signal ≈ MQ）保持和 phosphor-logging 一致；Get SEL Info 只是 `cache.size()`，不去打远端存储。

### 2.3 设置开机模式 / Chassis Control —— 往 state-manager 写状态

两个命令放一起讲，因为它们都是"**写**"而不是"读"。

**(a) Chassis Control（`netFn=0x00`, `cmd=0x02`）—— power on/off**

```cpp
// include/ipmid/api-types.hpp:169
constexpr Cmd cmdChassisControl = 0x02;
```

注册在 `chassishandler.cpp:2575-2578`，handler 是 `ipmiChassisControl`（`chassishandler.cpp:1446-1447`）。真正干活的 switch 在 `chassishandler.cpp:1450-1491`：

```cpp
    switch (chassisControl)
    {
        case cmdPowerOn:
            rc = initiateHostStateTransition(ctx, State::Host::Transition::On);
            break;
        case cmdPowerOff:
            rc = initiateChassisStateTransition(
                ctx, State::Chassis::Transition::Off);
            break;
        case cmdHardReset:
            rc = initiateHostStateTransition(
                ctx, State::Host::Transition::ForceWarmReboot);
            break;
```

`initiateHostStateTransition`（`chassishandler.cpp:873-894`）的做法很典型：

1. 拼出 host 状态对象路径 `/xyz/openbmc_project/state/host0`（`chassishandler.cpp:877-878`）。
2. 把 enum 转成 D-Bus 字符串（`chassishandler.cpp:881-883`）。
3. `ipmi::getService(ctx, HostState::interface, hostStatePath, service)` 找服务（`chassishandler.cpp:885-887`）。
4. `ipmi::setDbusProperty(..., requested_host_transition, request)` 写属性（`chassishandler.cpp:891-893`）。

**这就是"翻译官"的含义**：IPMI 的一个字节 `cmdPowerOn`，被翻译成 state-manager 上一个 D-Bus 属性的写入。开机的实际动作由 state-manager 完成，ipmid 不碰电源。

**(b) Set System Boot Options（`netFn=0x00`, `cmd=0x08`）—— 设置开机模式/启动设备**

```cpp
// include/ipmid/api-types.hpp:175
constexpr Cmd cmdSetSystemBootOptions = 0x08;
```

注册在 `chassishandler.cpp:2585-2589`。相关 D-Bus 路径常量写在 `chassishandler.cpp:149-151`：

```cpp
constexpr auto bootSettingsPath = "/xyz/openbmc_project/control/host0/boot";
constexpr auto bootSettingsOneTimePath =
    "/xyz/openbmc_project/control/host0/boot/one_time";
```

handler 本体是 `ipmiChassisSetSysBootOptions`（`chassishandler.cpp:2187`），它按参数分发到一组辅助函数——比如 `setBootSource`（`chassishandler.cpp:1696`）、`setBootMode`（`chassishandler.cpp:1750`）、`setBootType`（`chassishandler.cpp:1810`），这些函数最终落到 `ipmi::setDbusProperty`（如 `chassishandler.cpp:1705`、`chassishandler.cpp:1758`），写到 BIOS/启动设置相关的 D-Bus 对象上。你以前在 openUBMC 改启动项，本质也是这类操作。

### 2.4 三条命令对照表

| 命令 | NetFn/Cmd | handler | 数据从哪来 / 到哪去 | 溯源 |
| --- | --- | --- | --- | --- |
| Get Chassis Status | 00h/01h | `ipmiGetChassisStatus` | 读 `state/chassis0` 的 power state + 电源恢复策略 | `include/ipmid/api-types.hpp:168`、`chassishandler.cpp:1250-1255`、`chassishandler.cpp:1028-1047` |
| Get Sensor Reading | 04h/2Dh | `ipmiSensorGetSensorReading` | 生成的表 → D-Bus 传感器属性 | `include/ipmid/api-types.hpp:210`、`sensorhandler.cpp:568-642`、`scripts/writesensor.cpp.mako:35` |
| Get SEL Info | 0Ah/40h | `ipmiStorageGetSelInfo` | 本地 `selCacheMap`，缓存来自 phosphor-logging 日志对象 | `include/ipmid/api-types.hpp:238`、`storagehandler.cpp:242-283`、`storagehandler.cpp:186-208` |
| Chassis Control | 00h/02h | `ipmiChassisControl` | 写 `state/host0` 的 `requested_host_transition` | `include/ipmid/api-types.hpp:169`、`chassishandler.cpp:1446-1454`、`chassishandler.cpp:873-893` |
| Set System Boot Options | 00h/08h | `ipmiChassisSetSysBootOptions` | 写 `control/host0/boot` 相关属性 | `include/ipmid/api-types.hpp:175`、`chassishandler.cpp:2586-2589`、`chassishandler.cpp:149-151` |

### 2.5 场景三：Get Device ID —— `ipmitool mc info` 的版本号从哪来

`ipmitool mc info` 发出的是 **Get Device ID**（`netFn=0x06`、`cmd=0x01`，定义见 `include/ipmid/api-types.hpp:350`、`include/ipmid/api-types.hpp:93`）。这条命令最能说明"ipmid 自己不持有信息，全靠现查"。

注册（`apphandler.cpp:1894-1897`）：

```cpp
    // <Get Device ID>
    ipmi::registerHandler(ipmi::prioOpenBmcBase, ipmi::netFnApp,
                          ipmi::app::cmdGetDeviceId, ipmi::Privilege::User,
                          ipmiAppGetDeviceId);
```

handler 签名（`apphandler.cpp:611-621`）直接列出了 IPMI 规范要求的 9 个字段：

```cpp
ipmi::RspType<uint8_t,  // Device ID
              uint8_t,  // Device Revision
              uint8_t,  // Firmware Revision Major
              uint8_t,  // Firmware Revision minor
              uint8_t,  // IPMI version
              uint8_t,  // Additional device support
              uint24_t, // MFG ID
              uint16_t, // Product ID
              uint32_t  // AUX info
              >
    ipmiAppGetDeviceId(ipmi::Context::ptr ctx)
```

固化的静态信息（Device ID、厂商 IANA、Product ID）来自 `/usr/share/ipmi-providers/dev_id.json`（`apphandler.cpp:636`）。而**固件版本号是现查的**：在 `GET_DBUS_ACTIVE_SOFTWARE` 开启时（`apphandler.cpp:640`），调 `getActiveSoftwareVersionInfo(ctx)`（`apphandler.cpp:648`）。

`getActiveSoftwareVersionInfo`（`apphandler.cpp:107-112`）的查询路径：

1. 根目录常量 `softwareRoot = "/xyz/openbmc_project/software"`（`apphandler.cpp:53`）。
2. `ipmi::getAllDbusObjects(ctx, softwareRoot, redundancyIntf, objectTree)`（`apphandler.cpp:111-112`）——通过 ObjectMapper 找所有带"冗余"接口的对象。
3. 遍历对象，对每个再 `getService`（`apphandler.cpp:126-127`）和 `getManagedObjects`（`apphandler.cpp:130-131`）。
4. 挑出 active 的那个固件对象，取版本号，转成 `Revision` 结构（`apphandler.cpp:648-649`）。

> **Java 类比**：这相当于一个"GET /device/info"接口——一部分字段来自本地配置（`dev_id.json`），一部分字段实时调用"软件版本服务"查询，最后拼成一个 DTO 返回。C++ 里的 `ipmi::RspType<...>` 就是这个 DTO 的类型定义。

把三条场景放在一起看，规律很清楚：

```text
Get Chassis Status  → 读 state-manager 的电源属性      (chassishandler.cpp:1039-1041)
Get Sensor Reading  → 读 D-Bus 传感器属性               (sensorhandler.cpp:641-642)
Get SEL Info        → 读从 phosphor-logging 同步来的缓存 (storagehandler.cpp:186-208)
Get Device ID       → 读 D-Bus 软件版本对象             (apphandler.cpp:107-131)
```

**handler 本身几乎不存数据，主要是"查 + 拼 + 打包"。** 这就是为什么 ipmid 的主进程可以那么小。

---

## 3. 结尾：它和 state-manager / sel-logger / bmcweb 的分工

把这几个组件放进一张图，边界就清楚了：

```text
      主机 (ipmitool / BIOS)
        │  IPMI over KCS/BT  ← 字节驱动在别的仓库
        ▼
  ┌──────────────────────────────┐
  │ phosphor-host-ipmid (本文)    │  翻译官：帧 ↔ D-Bus
  │  execute → handlerMap →      │
  │  handler → sdbusplus         │  ipmid-new.cpp:900-904, 332-344
  └──────────────┬───────────────┘
                 │ D-Bus
   ┌─────────────┼──────────────┬──────────────────┐
   ▼             ▼              ▼                  ▼
state-manager  entity-manager  phosphor-logging   bmcweb
(电源/主机状态) (传感器配置)    (SEL 日志源)      (Redfish/Web UI)
   │             │              │
   └─ chassishandler.cpp:1028-1047
                 └─ scripts/writesensor.cpp.mako:35
                                └─ storagehandler.cpp:186-208
```

具体分工：

- **phosphor-host-ipmid**（本仓库）：只做 IPMI 协议层和翻译。它自己**不保存**电源状态、传感器配置、日志内容。`ipmid-new.cpp:332-344` 是入口分发，`libipmid/utils.cpp:89-105` 和 `libipmid/utils.cpp:203-225` 是它访问外部状态的通道。
- **state-manager**：电源/主机状态的真正所有者。Get Chassis Status 读它的 `current_power_state`（`chassishandler.cpp:1039-1041`）；Chassis Control 写它的 `requested_host_transition`（`chassishandler.cpp:891-893`）。
- **sel-logger / phosphor-logging**：日志的真正所有者。ipmid 通过监听它的 D-Bus signal 维护 `selCacheMap`（`storagehandler.cpp:159-184`），再对外回答 Get SEL Info / Get SEL Entry。
- **entity-manager**：传感器/硬件拓扑的提供者。ipmid 这边的传感器表由 `scripts/writesensor.cpp.mako:35` 生成，D-Bus 对象的来源与 entity-manager 的配置体系对应（生成逻辑里绑定了 `xyz.openbmc_project.Inventory.Manager` 接口，见 `scripts/writesensor.cpp.mako:15-17`）。
- **bmcweb**：面向用户的 Redfish/Web 通道，和 IPMI 是**并列的另一条管理通道**，两者都通过 D-Bus 操作同一批后端服务，互不经过对方。

**记住一句话收尾**：IPMI 命令到了 ipmid，先查 `handlerMap` 找到 handler（`ipmid-new.cpp:241-266`），handler 里用 sdbusplus 去读写别的服务的属性，然后 `responseSuccess(...)` 打包回去——**它就是主机和 BMC 内部服务之间的协议转换器**。

机制层（进程怎么起、provider 怎么加载、handler 怎么注册、请求怎么解包、会话权限怎么算）见同目录的《IPMI机制讲解.md》。
