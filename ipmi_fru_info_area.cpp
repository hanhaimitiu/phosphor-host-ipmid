
/**
 * @file ipmi_fru_info_area.cpp
 * @brief FRU 信息区（Info Area）构建器：生成多记录 FRU 布局。
 */
/* @file ipmi_fru_info_area.cpp
 * @brief FRU 信息区（Info Area）构建器：生成多记录 FRU 布局。 */
#include "ipmi_fru_info_area.hpp"

#include <ipmid/utils.hpp>
#include <phosphor-logging/elog.hpp>
#include <phosphor-logging/lg2.hpp>

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <map>
#include <numeric>
#include <sstream>

namespace ipmi
{
namespace fru
{
using namespace phosphor::logging;

// Property variables
// 属性变量
static constexpr auto partNumber = "Part Number";
static constexpr auto serialNumber = "Serial Number";
static constexpr auto manufacturer = "Manufacturer";
static constexpr auto buildDate = "Mfg Date";
static constexpr auto modelNumber = "Model Number";
static constexpr auto prettyName = "Name";
static constexpr auto version = "Version";
static constexpr auto type = "Type";

// Board info areas
// 板卡信息区（Board info areas）
static constexpr auto board = "Board";
static constexpr auto chassis = "Chassis";
static constexpr auto product = "Product";

static constexpr auto specVersion = 0x1;
static constexpr auto recordUnitOfMeasurement = 0x8; // size in bytes
static constexpr auto checksumSize = 0x1;            // size in bytes
static constexpr auto recordNotPresent = 0x0;
static constexpr auto englishLanguageCode = 0x0;
static constexpr auto typeLengthByteNull = 0x0;
static constexpr auto endOfCustomFields = 0xC1;
static constexpr auto commonHeaderFormatSize = 0x8; // size in bytes
static constexpr auto areaSizeOffset = 0x1;
static constexpr uint8_t typeASCII = 0xC0;
static constexpr auto maxRecordAttributeValue = 0x3F;

static constexpr auto secs_from_1970_1996 = 820454400;
static constexpr auto maxMfgDateValue = 0xFFFFFF; // 3 Byte length
static constexpr auto secs_per_min = 60;
static constexpr auto secsToMaxMfgdate =
    secs_from_1970_1996 + secs_per_min * maxMfgDateValue;

// Minimum size of resulting FRU blob.
// This is also the theoretical maximum size according to the spec:
// 8 bytes header + 5 areas at 0xff*8 bytes max each
// 8 + 5*0xff*8 = 0x27e0
// 生成的 FRU blob 的最小大小。
// 根据规范，这也是理论上的最大大小：
// 8 字节头部 + 5 个区，每个最大 0xff*8 字节
// 8 + 5*0xff*8 = 0x27e0
static constexpr auto fruMinSize = 0x27E0;

// Value to use for padding.
// Using 0xff to match the default (blank) value in a physical EEPROM.
// 用于填充的值。
// 使用 0xff 以匹配物理 EEPROM 中的默认（空白）值。
static constexpr auto fruPadValue = 0xff;

/**
 * @brief Format Beginning of Individual IPMI FRU Data Section
 *
 * @param[in] langCode Language code
 * @param[in/out] data FRU area data
 */
/* @brief 格式化单个 IPMI FRU 数据区的起始部分
 * @param[in] langCode 语言代码
 * @param[in/out] data FRU 区数据 */
void preFormatProcessing(bool langCode, FruAreaData& data)
{
    // Add id for version of FRU Info Storage Spec used
    // 添加所用 FRU Info Storage Spec 版本的 id
    data.emplace_back(specVersion);

    // Add Data Size - 0 as a placeholder, can edit after the data is finalized
    // 添加 Data Size - 先以 0 占位，数据确定后可再修改
    data.emplace_back(typeLengthByteNull);

    if (langCode)
    {
        data.emplace_back(englishLanguageCode);
    }
}

/**
 * @brief Append checksum of the FRU area data
 *
 * @param[in/out] data FRU area data
 */
/* @brief 追加 FRU 区数据的校验和
 * @param[in/out] data FRU 区数据 */
void appendDataChecksum(FruAreaData& data)
{
    uint8_t checksumVal = std::accumulate(data.begin(), data.end(), 0);
    // Push the Zero checksum as the last byte of this data
    // This appears to be a simple summation of all the bytes
    // 将 Zero 校验和作为该数据的最后一个字节压入
    // 这似乎只是所有字节的简单求和。
    data.emplace_back(-checksumVal);
}

/**
 * @brief Append padding bytes for the FRU area data
 *
 * @param[in/out] data FRU area data
 */
/* @brief 为 FRU 区数据追加填充字节
 * @param[in/out] data FRU 区数据 */
void padData(FruAreaData& data)
{
    uint8_t pad = (data.size() + checksumSize) % recordUnitOfMeasurement;
    if (pad)
    {
        data.resize((data.size() + recordUnitOfMeasurement - pad));
    }
}

/**
 * @brief Format End of Individual IPMI FRU Data Section
 *
 * @param[in/out] fruAreaData FRU area info data
 */
/* @brief 格式化单个 IPMI FRU 数据区的结束部分
 * @param[in/out] fruAreaData FRU 区信息数据 */
void postFormatProcessing(FruAreaData& data)
{
    // This area needs to be padded to a multiple of 8 bytes (after checksum)
    // 该区（在校验和之后）需要填充到 8 字节的整数倍
    padData(data);

    // Set size of data info area
    // 设置 data info 区的大小
    data.at(areaSizeOffset) =
        (data.size() + checksumSize) / (recordUnitOfMeasurement);

    // Finally add area checksum
    // 最后添加区校验和
    appendDataChecksum(data);
}

/**
 * @brief Read chassis type property value from inventory and append to the FRU
 * area data.
 *
 * @param[in] propMap map of property values
 * @param[in,out] data FRU area data to be appended
 */
/* @brief 从 inventory 读取机箱类型属性值，并追加到
 * FRU 区数据。
 * @param[in] propMap 属性值映射
 * @param[in,out] data 要追加到的 FRU 区数据 */
void appendChassisType(const PropertyMap& propMap, FruAreaData& data)
{
    uint8_t chassisType = 0; // Not specified
    auto iter = propMap.find(type);
    if (iter != propMap.end())
    {
        auto value = iter->second;
        if (!ipmi::tryParse(value, chassisType))
        {
            lg2::error("Could not parse chassis type, value: {VALUE}", "VALUE",
                       value);
            chassisType = 0;
        }
    }
    data.emplace_back(chassisType);
}

/**
 * @brief Read property value from inventory and append to the FRU area data
 *
 * @param[in] key key to search for in the property inventory data
 * @param[in] propMap map of property values
 * @param[in,out] data FRU area data to be appended
 */
/* @brief 从 inventory 读取属性值并追加到 FRU 区数据
 * @param[in] key 在属性 inventory 数据中要查找的键
 * @param[in] propMap 属性值映射
 * @param[in,out] data 要追加到的 FRU 区数据 */
void appendData(const Property& key, const PropertyMap& propMap,
                FruAreaData& data)
{
    auto iter = propMap.find(key);
    if (iter != propMap.end())
    {
        auto value = iter->second;
        // If starts with 0x or 0X remove them
        // ex: 0x123a just take 123a
        // 若以 0x 或 0X 开头则去掉它们
        // 例如：0x123a 只取 123a
        if ((value.compare(0, 2, "0x")) == 0 ||
            (value.compare(0, 2, "0X") == 0))
        {
            value.erase(0, 2);
        }

        // 6 bits for length as per FRU spec v1.0
        // if length is greater then 63(2^6) bytes then trim the data to 63
        // bytes.
        // 按 FRU 规范 v1.0，长度占 6 位
        // 若长度大于 63(2^6) 字节，则将数据裁剪为 63
        // 字节。
        auto valueLength = (value.length() > maxRecordAttributeValue)
                               ? maxRecordAttributeValue
                               : value.length();
        // 2 bits for type
        // Set the type to ascii
        // 类型占 2 位
        // 将类型设为 ascii
        uint8_t typeLength = valueLength | ipmi::fru::typeASCII;

        data.emplace_back(typeLength);
        std::copy(value.begin(), value.begin() + valueLength,
                  std::back_inserter(data));
    }
    else
    {
        // set 0 size
        // 设置大小为 0
        data.emplace_back(typeLengthByteNull);
    }
}

std::time_t timeStringToRaw(const std::string& input)
{
    // TODO: For non-US region timestamps, pass in region information for the
    // FRU to avoid the month/day swap.
    // 2017-02-24 - 13:59:00, Tue Nov 20 23:08:00 2018, 20251224T064200Z
    // TODO: 对于非 US 地区的时间戳，应向 FRU 传入地区信息，
    // 以避免月/日颠倒。
    // 2017-02-24 - 13:59:00, Tue Nov 20 23:08:00 2018, 20251224T064200Z
    static const std::vector<std::string> patterns = {
        "%Y-%m-%d - %H:%M:%S", "%a %b %d %H:%M:%S %Y", "%Y%m%dT%H%M%SZ"};

    std::tm time = {};

    for (const auto& pattern : patterns)
    {
        std::istringstream timeStream(input);
        timeStream >> std::get_time(&time, pattern.c_str());
        if (!timeStream.fail())
        {
            break;
        }
    }

    return timegm(&time);
}

/**
 * @brief Appends Build Date
 *
 * @param[in] propMap map of property values
 * @param[in/out] data FRU area to add the manfufacture date
 */
/* @brief 追加构建日期（Build Date）
 * @param[in] propMap 属性值映射
 * @param[in/out] data 要添加制造日期的 FRU 区 */
void appendMfgDate(const PropertyMap& propMap, FruAreaData& data)
{
    // MFG Date/Time
    // 制造日期/时间（MFG Date/Time）
    auto iter = propMap.find(buildDate);
    if ((iter != propMap.end()) && (iter->second.size() > 0))
    {
        std::time_t raw = timeStringToRaw(iter->second);

        // From FRU Spec:
        // "Mfg. Date / Time
        // Number of minutes from 0:00 hrs 1/1/96.
        // LSbyte first (little endian)
        // 00_00_00h = unspecified."
        // 摘自 FRU 规范：
        // "Mfg. Date / Time
        // 自 1996/1/1 0:00 起的分钟数。
        // 低字节在前（小端序）
        // 00_00_00h = 未指定。"
        if ((raw >= secs_from_1970_1996) && (raw <= secsToMaxMfgdate))
        {
            raw -= secs_from_1970_1996;
            raw /= secs_per_min;
            uint8_t fru_raw[3];
            fru_raw[0] = raw & 0xFF;
            fru_raw[1] = (raw >> 8) & 0xFF;
            fru_raw[2] = (raw >> 16) & 0xFF;
            std::copy(fru_raw, fru_raw + 3, std::back_inserter(data));
            return;
        }
        std::fprintf(stderr, "MgfDate invalid date: %u secs since UNIX epoch\n",
                     static_cast<unsigned int>(raw));
    }
    // Blank date
    // 空白日期
    data.emplace_back(0);
    data.emplace_back(0);
    data.emplace_back(0);
}

/**
 * @brief Builds a section of the common header
 *
 * @param[in] infoAreaSize size of the FRU area to write
 * @param[in] offset Current offset for data in overall record
 * @param[in/out] data Common Header section data container
 */
/* @brief 构建公共头（common header）的一部分
 * @param[in] infoAreaSize 要写入的 FRU 区大小
 * @param[in] offset 整个记录中数据的当前偏移
 * @param[in/out] data Common Header 区数据容器 */
void buildCommonHeaderSection(const uint32_t& infoAreaSize, uint16_t& offset,
                              FruAreaData& data)
{
    // Check if data for internal use section populated
    // 检查内部使用区（internal use section）是否已填充数据
    if (infoAreaSize == 0)
    {
        // Indicate record not present
        // 表示该记录不存在
        data.emplace_back(recordNotPresent);
    }
    else
    {
        // offset should be multiple of 8.
        // offset 应为 8 的倍数。
        auto remainder = offset % recordUnitOfMeasurement;
        // add the padding bytes in the offset so that offset
        // will be multiple of 8 byte.
        // 在 offset 处添加填充字节，使 offset
        // 成为 8 字节的倍数。
        offset += (remainder > 0) ? recordUnitOfMeasurement - remainder : 0;
        // Place data to define offset to area data section
        // 放入用于定义各数据区偏移的数据
        data.emplace_back(offset / recordUnitOfMeasurement);

        offset += infoAreaSize;
    }
}

/**
 * @brief Builds the Chassis info area data section
 *
 * @param[in] propMap map of properties for chassis info area
 * @return FruAreaData container with chassis info area
 */
/* @brief 构建机箱（Chassis）信息区数据段
 * @param[in] propMap 机箱信息区的属性映射
 * @return 含机箱信息区的 FruAreaData 容器 */
FruAreaData buildChassisInfoArea(const PropertyMap& propMap)
{
    FruAreaData fruAreaData;
    if (!propMap.empty())
    {
        // Set formatting data that goes at the beginning of the record
        // 设置记录开头的格式化数据
        preFormatProcessing(false, fruAreaData);

        // chassis type
        // 机箱类型
        appendChassisType(propMap, fruAreaData);

        // Chassis part number, in config.yaml it is configured as model
        // 机箱部件号，在 config.yaml 中配置为 model
        appendData(modelNumber, propMap, fruAreaData);

        // Board serial number
        // 板卡序列号
        appendData(serialNumber, propMap, fruAreaData);

        // Indicate End of Custom Fields
        // 表示自定义字段结束
        fruAreaData.emplace_back(endOfCustomFields);

        // Complete record data formatting
        // 完成记录数据格式化
        postFormatProcessing(fruAreaData);
    }
    return fruAreaData;
}

/**
 * @brief Builds the Board info area data section
 *
 * @param[in] propMap map of properties for board info area
 * @return FruAreaData container with board info area
 */
/* @brief 构建板卡（Board）信息区数据段
 * @param[in] propMap 板卡信息区的属性映射
 * @return 含板卡信息区的 FruAreaData 容器 */
FruAreaData buildBoardInfoArea(const PropertyMap& propMap)
{
    FruAreaData fruAreaData;
    if (!propMap.empty())
    {
        preFormatProcessing(true, fruAreaData);

        // Manufacturing date
        // 生产日期
        appendMfgDate(propMap, fruAreaData);

        // manufacturer
        // 制造商
        appendData(manufacturer, propMap, fruAreaData);

        // Product name/Pretty name
        // 产品名称/Pretty name
        appendData(prettyName, propMap, fruAreaData);

        // Board serial number
        // 板卡序列号
        appendData(serialNumber, propMap, fruAreaData);

        // Board part number
        // 板卡部件号
        appendData(partNumber, propMap, fruAreaData);

        // FRU File ID - Empty
        // FRU File ID - 空
        fruAreaData.emplace_back(typeLengthByteNull);

        // Empty FRU File ID bytes
        // 空的 FRU File ID 字节
        fruAreaData.emplace_back(recordNotPresent);

        // End of custom fields
        // 自定义字段结束
        fruAreaData.emplace_back(endOfCustomFields);

        postFormatProcessing(fruAreaData);
    }
    return fruAreaData;
}

/**
 * @brief Builds the Product info area data section
 *
 * @param[in] propMap map of FRU properties for Board info area
 * @return FruAreaData container with product info area data
 */
/* @brief 构建产品（Product）信息区数据段
 * @param[in] propMap 板卡信息区的 FRU 属性映射
 * @return 含产品信息区数据的 FruAreaData 容器 */
FruAreaData buildProductInfoArea(const PropertyMap& propMap)
{
    FruAreaData fruAreaData;
    if (!propMap.empty())
    {
        // Set formatting data that goes at the beginning of the record
        // 设置记录开头的格式化数据
        preFormatProcessing(true, fruAreaData);

        // manufacturer
        // 制造商
        appendData(manufacturer, propMap, fruAreaData);

        // Product name/Pretty name
        // 产品名称/Pretty name
        appendData(prettyName, propMap, fruAreaData);

        // Product part/model number
        // 产品部件号/型号
        appendData(modelNumber, propMap, fruAreaData);

        // Product version
        // 产品版本
        appendData(version, propMap, fruAreaData);

        // Serial Number
        // 序列号
        appendData(serialNumber, propMap, fruAreaData);

        // Add Asset Tag
        // 添加资产标签（Asset Tag）
        fruAreaData.emplace_back(recordNotPresent);

        // FRU File ID - Empty
        // FRU File ID - 空
        fruAreaData.emplace_back(typeLengthByteNull);

        // Empty FRU File ID bytes
        // 空的 FRU File ID 字节
        fruAreaData.emplace_back(recordNotPresent);

        // End of custom fields
        // 自定义字段结束
        fruAreaData.emplace_back(endOfCustomFields);

        postFormatProcessing(fruAreaData);
    }
    return fruAreaData;
}

FruAreaData buildFruAreaData(const FruInventoryData& inventory)
{
    FruAreaData combFruArea{};
    // Now build common header with data for this FRU Inv Record
    // Use this variable to increment size of header as we go along to determine
    // offset for the subsequent area offsets
    // 现在用该 FRU 库存记录的数据构建公共头
    // 使用该变量递增头部大小，以确定
    // 后续各区偏移的基准。
    uint16_t curDataOffset = commonHeaderFormatSize;
    // First byte is id for version of FRU Info Storage Spec used
    // 第一个字节是所用 FRU Info Storage Spec 版本的 id
    combFruArea.emplace_back(specVersion);

    // 2nd byte is offset to internal use data
    // 第二个字节是内部使用数据的偏移
    combFruArea.emplace_back(recordNotPresent);

    // 3rd byte is offset to chassis data
    // 第三个字节是机箱数据的偏移
    FruAreaData chassisArea;
    auto chassisIt = inventory.find(chassis);
    if (chassisIt != inventory.end())
    {
        chassisArea = buildChassisInfoArea(chassisIt->second);
    }
    // update the offset to chassis data.
    // 更新机箱数据的偏移。
    buildCommonHeaderSection(chassisArea.size(), curDataOffset, combFruArea);

    // 4th byte is offset to board data
    // 第四个字节是板卡数据的偏移
    FruAreaData boardArea;
    auto boardIt = inventory.find(board);
    if (boardIt != inventory.end())
    {
        boardArea = buildBoardInfoArea(boardIt->second);
    }
    // update the offset to the board data.
    // 更新板卡数据的偏移。
    buildCommonHeaderSection(boardArea.size(), curDataOffset, combFruArea);

    // 5th byte is offset to product data
    // 第五个字节是产品数据的偏移
    FruAreaData prodArea;
    auto prodIt = inventory.find(product);
    if (prodIt != inventory.end())
    {
        prodArea = buildProductInfoArea(prodIt->second);
    }
    // update the offset to the product data.
    // 更新产品数据的偏移。
    buildCommonHeaderSection(prodArea.size(), curDataOffset, combFruArea);

    // 6th byte is offset to multirecord data
    // 第六个字节是多记录（multirecord）数据的偏移
    combFruArea.emplace_back(recordNotPresent);

    // 7th byte is PAD
    // 第七个字节是 PAD
    combFruArea.emplace_back(recordNotPresent);

    // 8th (Final byte of Header Format) is the checksum
    // 第八个（头格式的最后一个字节）是校验和
    appendDataChecksum(combFruArea);

    // Combine everything into one full IPMI FRU specification Record
    // add chassis use area data
    // 将所有内容组合成一条完整的 IPMI FRU 规范记录
    // 添加机箱使用区数据
    combFruArea.insert(combFruArea.end(), chassisArea.begin(),
                       chassisArea.end());

    // add board area data
    // 添加板卡区数据
    combFruArea.insert(combFruArea.end(), boardArea.begin(), boardArea.end());

    // add product use area data
    // 添加产品使用区数据
    combFruArea.insert(combFruArea.end(), prodArea.begin(), prodArea.end());

    // If area is smaller than the minimum size, pad it. This enables ipmitool
    // to update the FRU blob with values longer than the original payload.
    // 若某个区小于最小尺寸，则将其填充。这样 ipmitool
    // 才能用比原始 payload 更长的值更新 FRU blob。
    if (combFruArea.size() < fruMinSize)
    {
        combFruArea.resize(fruMinSize, fruPadValue);
    }

    return combFruArea;
}

} // namespace fru
} // namespace ipmi
