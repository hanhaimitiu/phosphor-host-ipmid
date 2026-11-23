#include <ipmid/entity_map_json.hpp>
#include <ipmid/types.hpp>
#include <nlohmann/json.hpp>

#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace ipmi
{
namespace sensor
{

namespace
{
using ::testing::IsEmpty;

TEST(ValidateJson, FailWithNonArrayReturnsEmpty)
{
    /* The entity map input json is expected to be an array of objects. */
    /* entity map 输入的 json 应为一个对象数组。 */
    auto j = R"(
        {
            "id" : 1,
            "containerEntityId" : 2,
            "containerEntityInstance" : 3,
            "isList" : false,
            "isLinked" : false,
            "entities" : [
                {"id" : 1, "instance" : 2},
                {"id" : 1, "instance" : 3},
                {"id" : 1, "instance" : 4},
                {"id" : 1, "instance" : 5}
            ]
        }
    )"_json;

    EXPECT_THAT(buildJsonEntityMap(j), IsEmpty());
}

TEST(ValidateJson, FailWithMissingFieldReturnsEmpty)
{
    /* There are many required fields, let's just validate that if one is
     * missing, it returns the empty map.
     */
    /* 必填字段有很多，这里只验证如果缺少
     * 其中一个字段，是否会返回空 map。 */
    auto j = R"(
        [
            {
                "id" : 1,
                "containerEntityId" : 2,
                "containerEntityInstance" : 3,
                "isList" : false,
                "entities" : [
                    {"id" : 1, "instance" : 2},
                    {"id" : 1, "instance" : 3},
                    {"id" : 1, "instance" : 4},
                    {"id" : 1, "instance" : 5}
                ]
            }
        ]
    )"_json;

    EXPECT_THAT(buildJsonEntityMap(j), IsEmpty());
}

TEST(ValidateJson, AllValidEntryReturnsExpectedMap)
{
    /* Boring test where we provide completely valid information and expect the
     * resulting map contains that information.
     */
    /* 常规测试：提供完全合法的信息，并期望
     * 生成的 map 包含这些信息。 */
    auto j = R"(
        [
            {
                "id" : 1,
                "containerEntityId" : 2,
                "containerEntityInstance" : 3,
                "isList" : false,
                "isLinked" : false,
                "entities" : [
                    {"id" : 1, "instance" : 2},
                    {"id" : 1, "instance" : 3},
                    {"id" : 1, "instance" : 4},
                    {"id" : 1, "instance" : 5}
                ]
            }
        ]
    )"_json;

    auto map = buildJsonEntityMap(j);
    EXPECT_FALSE(map.find(1) == map.end());
    auto entry = map.find(1);
    EXPECT_EQ(entry->first, 1);

    /* TODO: someone could write an equality operator for this object. */
    /* TODO：可以为该对象编写一个相等比较运算符。 */
    EXPECT_EQ(entry->second.containerEntityId, 2);
    EXPECT_EQ(entry->second.containerEntityInstance, 3);
    EXPECT_FALSE(entry->second.isList);
    EXPECT_FALSE(entry->second.isLinked);
    ContainedEntitiesArray expected = {
        std::make_pair(1, 2), std::make_pair(1, 3), std::make_pair(1, 4),
        std::make_pair(1, 5)};
    EXPECT_EQ(entry->second.containedEntities, expected);
}

TEST(ValidateJson, EntryHasInsufficientContainerEntryCountReturnsEmpty)
{
    /* The container must have four pairs. (I don't know why, and maybe this
     * restriction will change).
     */
    /* container 必须有四个键值对。（我不知道为什么，
     * 也许这个限制以后会变。） */
    auto j = R"(
        [
            {
                "id" : 1,
                "containerEntityId" : 2,
                "containerEntityInstance" : 3,
                "isList" : false,
                "isLinked" : false,
                "entities" : [
                    {"id" : 1, "instance" : 2},
                    {"id" : 1, "instance" : 3},
                    {"id" : 1, "instance" : 4}
                ]
            }
        ]
    )"_json;

    EXPECT_THAT(buildJsonEntityMap(j), IsEmpty());
}

TEST(ValidateJson, ThereAreTwoEntriesOneInvalidReturnsEmpty)
{
    /* If any entry in the file is corrupt, the file is disregarded. */
    /* 如果文件中有任何条目损坏，则忽略整个文件。 */
    auto j = R"(
        [
            {
                "id" : 1,
                "containerEntityId" : 2,
                "containerEntityInstance" : 3,
                "isList" : false,
                "isLinked" : false,
                "entities" : [
                    {"id" : 1, "instance" : 2},
                    {"id" : 1, "instance" : 3},
                    {"id" : 1, "instance" : 4},
                    {"id" : 1, "instance" : 5}
                ]
            },
            {
                "id" : 2,
                "containerEntityId" : 2,
                "containerEntityInstance" : 3,
                "isList" : false,
                "isLinked" : false,
                "entities" : [
                    {"id" : 1, "instance" : 2},
                    {"id" : 1, "instance" : 3},
                    {"id" : 1, "instance" : 4}
                ]
            }
        ]
    )"_json;

    EXPECT_THAT(buildJsonEntityMap(j), IsEmpty());
}

TEST(ValidateJson, ThereAreTwoEntriesBothValidReturnsBoth)
{
    /* The map supports more than one entry, just validate this. */
    /* map 支持多个条目，这里仅验证这一点。 */
    auto j = R"(
        [
            {
                "id" : 1,
                "containerEntityId" : 2,
                "containerEntityInstance" : 3,
                "isList" : false,
                "isLinked" : false,
                "entities" : [
                    {"id" : 1, "instance" : 2},
                    {"id" : 1, "instance" : 3},
                    {"id" : 1, "instance" : 4},
                    {"id" : 1, "instance" : 5}
                ]
            },
            {
                "id" : 2,
                "containerEntityId" : 2,
                "containerEntityInstance" : 3,
                "isList" : false,
                "isLinked" : false,
                "entities" : [
                    {"id" : 1, "instance" : 6},
                    {"id" : 1, "instance" : 7},
                    {"id" : 1, "instance" : 8},
                    {"id" : 1, "instance" : 9}
                ]
            }
        ]
    )"_json;

    auto map = buildJsonEntityMap(j);
    EXPECT_FALSE(map.find(1) == map.end());
    EXPECT_FALSE(map.find(2) == map.end());

    auto entry = map.find(1);
    EXPECT_EQ(entry->first, 1);
    EXPECT_EQ(entry->second.containerEntityId, 2);
    EXPECT_EQ(entry->second.containerEntityInstance, 3);
    EXPECT_FALSE(entry->second.isList);
    EXPECT_FALSE(entry->second.isLinked);
    ContainedEntitiesArray expected = {
        std::make_pair(1, 2), std::make_pair(1, 3), std::make_pair(1, 4),
        std::make_pair(1, 5)};
    EXPECT_EQ(entry->second.containedEntities, expected);

    entry = map.find(2);
    expected = {std::make_pair(1, 6), std::make_pair(1, 7),
                std::make_pair(1, 8), std::make_pair(1, 9)};
    EXPECT_EQ(entry->second.containedEntities, expected);
}

} // namespace
} // namespace sensor
} // namespace ipmi
