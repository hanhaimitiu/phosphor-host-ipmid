#pragma once

/**
 * @file entity_map_json.hpp
 * @brief entity-map JSON 解析的声明。
 */
/*  @file entity_map_json.hpp
 *  @brief entity-map JSON 解析的声明。 */

#include <ipmid/types.hpp>
#include <nlohmann/json.hpp>

#include <memory>

namespace ipmi
{
namespace sensor
{

/**
 * @brief Grab a handle to the entity map.
 */
/* @brief 获取 entity map 的句柄。 */
const EntityInfoMap& getIpmiEntityRecords();

/**
 * @brief Open the default entity map json file, and if present and valid json,
 * return a built entity map.
 *
 * @return the map
 */
/* @brief 打开默认的 entity map json 文件，如果存在且 json 有效，
 * 则返回构建好的 entity map。
 * @return 该 map */
EntityInfoMap buildEntityMapFromFile();

/**
 * @brief Given json data validate the data matches the expected format for the
 * entity map configuration and parse the data into a map of the entities.
 *
 * If any entry is invalid, the entire contents passed in is disregarded as
 * possibly corrupt.
 *
 * @param[in] data - the json data
 * @return the map
 */
/* @brief 给定 json 数据，验证其是否符合 entity map 配置的
 * 预期格式，并将数据解析为实体的 map。
 * 如果任何条目无效，传入的全部内容都会因
 * 可能损坏而被丢弃。
 * @param[in] data - json 数据
 * @return 该 map */
EntityInfoMap buildJsonEntityMap(const nlohmann::json& data);

/**
 * @brief Owner of the EntityInfoMap.
 */
/* @brief EntityInfoMap 的所有者。 */
class EntityInfoMapContainer
{
  public:
    /** Get ahold of the owner. */
    /* 获取所有者（owner）。 */
    static EntityInfoMapContainer* getContainer();
    /** Get ahold of the records. */
    /* 获取记录（records）。 */
    const EntityInfoMap& getIpmiEntityRecords();

  private:
    EntityInfoMapContainer(const EntityInfoMap& entityRecords) :
        entityRecords(entityRecords)
    {}
    EntityInfoMap entityRecords;
};

} // namespace sensor
} // namespace ipmi
