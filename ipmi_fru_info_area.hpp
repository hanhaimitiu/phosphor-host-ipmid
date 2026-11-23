#pragma once

/**
 * @file ipmi_fru_info_area.hpp
 * @brief FRU 信息区构建器的声明。
 */
/* @file ipmi_fru_info_area.hpp
 * @brief FRU 信息区构建器的声明。 */

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ipmi
{
namespace fru
{
using FruAreaData = std::vector<uint8_t>;
using Section = std::string;
using Value = std::string;
using Property = std::string;
using PropertyMap = std::map<Property, Value>;
using FruInventoryData = std::map<Section, PropertyMap>;

/**
 * @brief Builds Fru area data from inventory data
 *
 * @param[in] invData FRU properties values read from inventory
 *
 * @return FruAreaData FRU area data as per IPMI specification
 */
/* @brief 根据 inventory 数据构建 FRU 区数据
 * @param[in] invData 从 inventory 读取的 FRU 属性值
 * @return FruAreaData 符合 IPMI 规范的 FRU 区数据 */
FruAreaData buildFruAreaData(const FruInventoryData& inventory);

} // namespace fru
} // namespace ipmi
