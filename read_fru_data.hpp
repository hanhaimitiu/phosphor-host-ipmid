#pragma once

/**
 * @file read_fru_data.hpp
 * @brief FRU 分块读取的声明。
 */
/* @file read_fru_data.hpp
 * @brief FRU 分块读取的声明。 */
#include "ipmi_fru_info_area.hpp"

#include <sdbusplus/bus.hpp>

#include <string>

namespace ipmi
{
namespace fru
{
using FRUId = uint8_t;
using FRUAreaMap = std::map<FRUId, FruAreaData>;

static constexpr auto xyzPrefix = "/xyz/openbmc_project/";
static constexpr auto invMgrInterface = "xyz.openbmc_project.Inventory.Manager";
static constexpr auto invObjPath = "/xyz/openbmc_project/inventory";
static constexpr auto propInterface = "org.freedesktop.DBus.Properties";
static constexpr auto invItemInterface = "xyz.openbmc_project.Inventory.Item";
static constexpr auto itemPresentProp = "Present";

/**
 * @brief Get fru area data as per IPMI specification
 *
 * @param[in] fruNum FRU ID
 *
 * @return FRU area data as per IPMI specification
 */
/* @brief 按 IPMI 规范获取 FRU 区数据
 * @param[in] fruNum FRU ID
 * @return 符合 IPMI 规范的 FRU 区数据 */
const FruAreaData& getFruAreaData(const FRUId& fruNum);

/**
 * @brief Register callback handler into DBUS for PropertyChange events
 *
 * @return negative value on failure
 */
/* @brief 向 DBUS 注册 PropertyChange 事件的回调处理程序
 * @return 失败时返回负值 */
int registerCallbackHandler();
} // namespace fru
} // namespace ipmi
