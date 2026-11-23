
/**
 * @file channel.cpp
 * @brief APP/IPMI Channel 能力相关命令的实现（Get Channel Info 等）。
 */
/*  @file channel.cpp
 *  @brief APP/IPMI Channel 能力相关命令的实现（Get Channel Info 等）。 */
#include "channel.hpp"

#include "user_channel/channel_layer.hpp"

#include <arpa/inet.h>

#include <ipmid/types.hpp>
#include <ipmid/utils.hpp>
#include <phosphor-logging/elog-errors.hpp>
#include <phosphor-logging/lg2.hpp>
#include <xyz/openbmc_project/Common/error.hpp>

#include <fstream>
#include <set>
#include <string>

using namespace phosphor::logging;
using namespace sdbusplus::error::xyz::openbmc_project::common;

namespace ipmi
{
constexpr Cc ccPayloadTypeNotSupported = 0x80;

static inline auto responsePayloadTypeNotSupported()
{
    return response(ccPayloadTypeNotSupported);
}
} // namespace ipmi

namespace cipher
{

/** @brief Get the supported Cipher records
 *
 * The cipher records are read from the JSON file and converted into
 * 1. cipher suite record format mentioned in the IPMI specification. The
 * records can be either OEM or standard cipher. Each json entry is parsed and
 * converted into the cipher record format and pushed into the vector.
 * 2. Algorithms listed in vector format
 *
 * @return pair of vector containing 1. all the cipher suite records. 2.
 * Algorithms supported
 *
 */
/* @brief 获取支持的 Cipher 记录
 *  从 JSON 文件读取 cipher 记录并转换为：
 *  1. IPMI 规范中所述的 cipher suite record 格式。记录可以是
 *  OEM 或标准 cipher。每个 json 条目都会被解析并转换为
 *  cipher record 格式，然后压入 vector。
 *  2. 以 vector 形式列出的算法
 *  @return 由 vector 组成的 pair：1. 所有 cipher suite 记录。
 *  2. 支持的算法 */
std::pair<std::vector<uint8_t>, std::vector<uint8_t>> getCipherRecords()
{
    std::vector<uint8_t> cipherRecords;
    std::vector<uint8_t> supportedAlgorithmRecords;
    // create set to get the unique supported algorithms
    // 创建 set 以获取去重后的支持算法
    std::set<uint8_t> supportedAlgorithmSet;

    std::ifstream jsonFile(configFile);
    if (!jsonFile.is_open())
    {
        lg2::error("Channel Cipher suites file not found");
        elog<InternalFailure>();
    }

    auto data = Json::parse(jsonFile, nullptr, false);
    if (data.is_discarded())
    {
        lg2::error("Parsing channel cipher suites JSON failed");
        elog<InternalFailure>();
    }

    for (const auto& record : data)
    {
        if (record.find(oem) != record.end())
        {
            // OEM cipher suite - 0xC1
            // OEM 密码套件（cipher suite）- 0xC1
            cipherRecords.push_back(oemCipherSuite);
            // Cipher Suite ID
            // 密码套件 ID（Cipher Suite ID）
            cipherRecords.push_back(record.value(cipher, 0));
            // OEM IANA - 3 bytes
            // OEM IANA - 3 字节
            cipherRecords.push_back(record.value(oem, 0));
            cipherRecords.push_back(record.value(oem, 0) >> 8);
            cipherRecords.push_back(record.value(oem, 0) >> 16);
        }
        else
        {
            // Standard cipher suite - 0xC0
            // 标准密码套件（cipher suite）- 0xC0
            cipherRecords.push_back(stdCipherSuite);
            // Cipher Suite ID
            // 密码套件 ID（Cipher Suite ID）
            cipherRecords.push_back(record.value(cipher, 0));
        }

        // Authentication algorithm number
        // 认证算法编号
        cipherRecords.push_back(record.value(auth, 0));
        supportedAlgorithmSet.insert(record.value(auth, 0));

        // Integrity algorithm number
        // 完整性算法编号
        cipherRecords.push_back(record.value(integrity, 0) | integrityTag);
        supportedAlgorithmSet.insert(record.value(integrity, 0) | integrityTag);

        // Confidentiality algorithm number
        // 机密性算法编号
        cipherRecords.push_back(record.value(conf, 0) | confTag);
        supportedAlgorithmSet.insert(record.value(conf, 0) | confTag);
    }

    // copy the set to supportedAlgorithmRecord which is vector based.
    // 将 set 复制到基于 vector 的 supportedAlgorithmRecord 中。
    std::copy(supportedAlgorithmSet.begin(), supportedAlgorithmSet.end(),
              std::back_inserter(supportedAlgorithmRecords));

    return std::make_pair(cipherRecords, supportedAlgorithmRecords);
}

} // namespace cipher

/** @brief this command is used to look up what authentication, integrity,
 *  confidentiality algorithms are supported.
 *
 *  @ param ctx - context pointer
 *  @ param channelNumber - channel number
 *  @ param payloadType - payload type
 *  @ param listIndex - list index
 *  @ param algoSelectBit - list algorithms
 *
 *  @returns ipmi completion code plus response data
 *  - rspChannel - channel number for authentication algorithm.
 *  - rspRecords - cipher suite records.
 **/
/* @brief 本命令用于查询支持哪些认证、完整性、
 *  机密性算法。
 *  @ param ctx - 上下文指针
 *  @ param channelNumber - 通道号
 *  @ param payloadType - 负载类型
 *  @ param listIndex - 列表索引
 *  @ param algoSelectBit - 列出算法
 *  @returns ipmi 完成码加上响应数据
 *  - rspChannel - 认证算法对应的通道号。
 *  - rspRecords - cipher suite 记录。 */
ipmi::RspType<uint8_t,             // Channel Number
              std::vector<uint8_t> // Cipher Records
              >
    getChannelCipherSuites(ipmi::Context::ptr ctx, uint4_t channelNumber,
                           uint4_t reserved1, uint8_t payloadType,
                           uint6_t listIndex, uint1_t reserved2,
                           uint1_t algoSelectBit)
{
    static std::vector<uint8_t> cipherRecords;
    static std::vector<uint8_t> supportedAlgorithms;
    static auto recordInit = false;

    uint8_t rspChannel = ipmi::convertCurrentChannelNum(
        static_cast<uint8_t>(channelNumber), ctx->channel);

    if (!ipmi::isValidChannel(rspChannel) || reserved1 != 0 || reserved2 != 0)
    {
        return ipmi::responseInvalidFieldRequest();
    }
    if (!ipmi::isValidPayloadType(static_cast<ipmi::PayloadType>(payloadType)))
    {
        lg2::debug("Get channel cipher suites - Invalid payload type");
        return ipmi::responsePayloadTypeNotSupported();
    }

    if (!recordInit)
    {
        try
        {
            std::tie(cipherRecords, supportedAlgorithms) =
                cipher::getCipherRecords();
            recordInit = true;
        }
        catch (const std::exception& e)
        {
            return ipmi::responseUnspecifiedError();
        }
    }

    const std::vector<uint8_t>& records =
        algoSelectBit ? cipherRecords : supportedAlgorithms;
    static constexpr auto respSize = 16;

    // Session support is available in active LAN channels.
    // 会话（Session）支持仅在激活的 LAN 通道上可用。
    if ((ipmi::getChannelSessionSupport(rspChannel) ==
         ipmi::EChannelSessSupported::none) ||
        !(ipmi::doesDeviceExist(rspChannel)))
    {
        lg2::debug("Get channel cipher suites - Device does not exist");
        return ipmi::responseInvalidFieldRequest();
    }

    // List index(00h-3Fh), 0h selects the first set of 16, 1h selects the next
    // set of 16 and so on.
    // 列表索引（00h-3Fh），0h 选择前 16 个一组，1h 选择接下来
    // 的 16 个一组，依此类推。

    // Calculate the number of record data bytes to be returned.
    // 计算要返回的记录数据字节数。
    auto start =
        std::min(static_cast<size_t>(listIndex) * respSize, records.size());
    auto end = std::min((static_cast<size_t>(listIndex) * respSize) + respSize,
                        records.size());
    auto size = end - start;

    std::vector<uint8_t> rspRecords;
    std::copy_n(records.data() + start, size, std::back_inserter(rspRecords));

    return ipmi::responseSuccess(rspChannel, rspRecords);
}
