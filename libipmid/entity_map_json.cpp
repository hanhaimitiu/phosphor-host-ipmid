
/**
 * @file entity_map_json.cpp
 * @brief entity-map JSON 解析的实现。
 */
/*  @file entity_map_json.cpp
 *  @brief entity-map JSON 解析的实现。 */
#include <ipmid/entity_map_json.hpp>
#include <ipmid/types.hpp>
#include <nlohmann/json.hpp>

#include <exception>
#include <fstream>
#include <memory>
#include <string>
#include <utility>

namespace ipmi
{
namespace sensor
{

EntityInfoMapContainer* EntityInfoMapContainer::getContainer()
{
    static std::unique_ptr<EntityInfoMapContainer> instance;

    if (!instance)
    {
        /* TODO: With multi-threading this would all need to be locked so
         * the first thread to hit it would set it up.
         */
        /* TODO：若使用多线程，这些都需要加锁，
         * 以便第一个访问它的线程完成初始化。 */
        EntityInfoMap builtEntityMap = buildEntityMapFromFile();
        instance = std::unique_ptr<EntityInfoMapContainer>(
            new EntityInfoMapContainer(builtEntityMap));
    }

    return instance.get();
}

const EntityInfoMap& EntityInfoMapContainer::getIpmiEntityRecords()
{
    return entityRecords;
}

EntityInfoMap buildEntityMapFromFile()
{
    const char* entityMapJsonFilename =
        "/usr/share/ipmi-providers/entity-map.json";
    EntityInfoMap builtMap;

    std::ifstream mapFile(entityMapJsonFilename);
    if (!mapFile.is_open())
    {
        return builtMap;
    }

    auto data = nlohmann::json::parse(mapFile, nullptr, false);
    if (data.is_discarded())
    {
        return builtMap;
    }

    return buildJsonEntityMap(data);
}

EntityInfoMap buildJsonEntityMap(const nlohmann::json& data)
{
    EntityInfoMap builtMap;

    if (data.type() != nlohmann::json::value_t::array)
    {
        return builtMap;
    }

    try
    {
        for (const auto& entry : data)
        {
            /* It's an array entry with the following fields: id,
             * containerEntityId, containerEntityInstance, isList, isLinked,
             * entities[4]
             */
            /* 这是一个数组条目，包含以下字段：id、
             * containerEntityId、containerEntityInstance、isList、isLinked、
             * entities[4] */
            EntityInfo obj;
            Id recordId = entry.at("id").get<Id>();
            obj.containerEntityId =
                entry.at("containerEntityId").get<uint8_t>();
            obj.containerEntityInstance =
                entry.at("containerEntityInstance").get<uint8_t>();
            obj.isList = entry.at("isList").get<bool>();
            obj.isLinked = entry.at("isLinked").get<bool>();

            auto jsonEntities = entry.at("entities");

            if (jsonEntities.type() != nlohmann::json::value_t::array)
            {
                throw std::runtime_error(
                    "Invalid type for entities entry, must be array");
            }
            if (jsonEntities.size() != obj.containedEntities.size())
            {
                throw std::runtime_error(
                    "Entities must be in pairs of " +
                    std::to_string(obj.containedEntities.size()));
            }

            for (std::size_t i = 0; i < obj.containedEntities.size(); i++)
            {
                obj.containedEntities[i] = std::make_pair(
                    jsonEntities[i].at("id").get<uint8_t>(),
                    jsonEntities[i].at("instance").get<uint8_t>());
            }

            builtMap.insert({recordId, obj});
        }
    }
    catch (const std::exception& e)
    {
        /* If any entry is invalid, the entire file cannot be trusted. */
        /* 如果任何条目无效，则整个文件都不可信。 */
        builtMap.clear();
    }

    return builtMap;
}

} // namespace sensor
} // namespace ipmi
