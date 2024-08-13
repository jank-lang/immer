#include <catch2/catch_test_macros.hpp>

#include <immer/extra/persist/cereal/with_pools.hpp>
#include <immer/extra/persist/transform.hpp>

#include "utils.hpp"
#include <nlohmann/json.hpp>

namespace {

// include:intro/start-types
// Set the BL constant to 1, so that only 2 elements are stored in leaves.
// This allows to demonstrate structural sharing even in vectors with just a few
// elements.
using vector_one =
    immer::vector<int, immer::default_memory_policy, immer::default_bits, 1>;

struct document
{
    // Make it a boost::hana Struct.
    // This allows the persist library to determine what pool types are needed
    // and also to name the pools.
    BOOST_HANA_DEFINE_STRUCT(document,
                             (vector_one, ints),
                             (vector_one, ints2) //
    );

    friend bool operator==(const document&, const document&) = default;

    // Make the struct serializable with cereal as usual, nothing special
    // related to immer-persist.
    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(CEREAL_NVP(ints), CEREAL_NVP(ints2));
    }
};

using json_t = nlohmann::json;
// include:intro/end-types

} // namespace

TEST_CASE("Docs save with immer-persist", "[docs]")
{
    // include:intro/start-prepare-value
    const auto v1    = vector_one{1, 2, 3};
    const auto v2    = v1.push_back(4).push_back(5).push_back(6);
    const auto value = document{v1, v2};
    // include:intro/end-prepare-value

    SECTION("Without immer-persist")
    {
        const auto expected_json = json_t::parse(R"(
{"value0": {"ints": [1, 2, 3], "ints2": [1, 2, 3, 4, 5, 6]}}
)");
        const auto str           = [&] {
            // include:intro/start-serialize-with-cereal
            auto os = std::ostringstream{};
            {
                auto ar = cereal::JSONOutputArchive{os};
                ar(value);
            }
            return os.str();
            // include:intro/end-serialize-with-cereal
        }();
        REQUIRE(json_t::parse(str) == expected_json);

        const auto loaded_value = [&] {
            auto is = std::istringstream{str};
            auto ar = cereal::JSONInputArchive{is};
            auto r  = document{};
            ar(r);
            return r;
        }();

        REQUIRE(value == loaded_value);
    }

    SECTION("With immer-persist")
    {
        // Immer-persist uses policies to control certain aspects of
        // serialization:
        // - types of pools that should be used
        // - names of those pools
        // include:intro/start-serialize-with-persist
        const auto policy =
            immer::persist::hana_struct_auto_member_name_policy(document{});
        const auto str = immer::persist::cereal_save_with_pools(value, policy);
        // include:intro/end-serialize-with-persist

        // The resulting JSON looks much more complicated for this little
        // example but the more structural sharing is used inside the serialized
        // value, the bigger the benefit from using immer-persist.
        //
        // Notable points for the structure of this JSON:
        // - vectors "ints" and "ints2" are serialized as integers, referring to
        // the vectors inside the pools
        // - there is a "pools" object serialized next to the value itself
        // - the "pools" object contains pools per type of the container, in
        // this example only one, for `immer::vector<int>`
        //
        // The vector pool contains:
        // - B and BL constants for the corresponding `immer::vector` type
        // - "inners" and "leaves" maps that store the actual nodes of the
        // vector
        // - "vectors" list that allows to store the root and tail of the vector
        // structure and to refer to the vector with just one integer:
        // `{"ints": 0, "ints2": 1}`: 0 and 1 refer to the indices of this
        // array.

        // include:intro/start-persist-json
        const auto expected_json = json_t::parse(R"(
{
  "value0": {"ints": 0, "ints2": 1},
  "pools": {
    "ints": {
      "B": 5,
      "BL": 1,
      "inners": [
        {"key": 0, "value": {"children": [2], "relaxed": false}},
        {"key": 3, "value": {"children": [2, 5], "relaxed": false}}
      ],
      "leaves": [
        {"key": 1, "value": [3]},
        {"key": 2, "value": [1, 2]},
        {"key": 4, "value": [5, 6]},
        {"key": 5, "value": [3, 4]}
      ],
      "vectors": [{"root": 0, "tail": 1}, {"root": 3, "tail": 4}]
    }
  }
}
    )");
        // include:intro/end-persist-json
        REQUIRE(json_t::parse(str) == expected_json);

        const auto loaded_value =
            immer::persist::cereal_load_with_pools<document>(str, policy);
        REQUIRE(value == loaded_value);
    }
}

namespace {
// include:start-doc_2-type
using vector_str = immer::
    vector<std::string, immer::default_memory_policy, immer::default_bits, 1>;

struct extra_data
{
    vector_str comments;

    friend bool operator==(const extra_data&, const extra_data&) = default;

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(CEREAL_NVP(comments));
    }
};

struct doc_2
{
    vector_one ints;
    vector_one ints2;
    vector_str strings;
    extra_data extra;

    friend bool operator==(const doc_2&, const doc_2&) = default;

    template <class Archive>
    void serialize(Archive& ar)
    {
        ar(CEREAL_NVP(ints),
           CEREAL_NVP(ints2),
           CEREAL_NVP(strings),
           CEREAL_NVP(extra));
    }
};
// include:end-doc_2-type

// include:start-doc_2_policy
struct doc_2_policy
{
    auto get_pool_types(const auto&) const
    {
        return boost::hana::to_set(
            boost::hana::tuple_t<vector_one, vector_str>);
    }

    template <class Archive>
    void save(Archive& ar, const doc_2& doc2_value) const
    {
        ar(CEREAL_NVP(doc2_value));
    }

    template <class Archive>
    void load(Archive& ar, doc_2& doc2_value) const
    {
        ar(CEREAL_NVP(doc2_value));
    }

    auto get_pool_name(const vector_one&) const { return "vector_of_ints"; }
    auto get_pool_name(const vector_str&) const { return "vector_of_strings"; }
};
// include:end-doc_2_policy
} // namespace

TEST_CASE("Custom policy", "[docs]")
{
    // include:start-doc_2-cereal_save_with_pools
    const auto v1   = vector_one{1, 2, 3};
    const auto v2   = v1.push_back(4).push_back(5).push_back(6);
    const auto str1 = vector_str{"one", "two"};
    const auto str2 =
        str1.push_back("three").push_back("four").push_back("five");
    const auto value = doc_2{v1, v2, str1, extra_data{str2}};

    const auto str =
        immer::persist::cereal_save_with_pools(value, doc_2_policy{});
    // include:end-doc_2-cereal_save_with_pools

    // include:start-doc_2-json
    const auto expected_json = json_t::parse(R"(
{
  "doc2_value": {"ints": 0, "ints2": 1, "strings": 0, "extra": {"comments": 1}},
  "pools": {
    "vector_of_ints": {
      "B": 5,
      "BL": 1,
      "leaves": [
        {"key": 1, "value": [3]},
        {"key": 2, "value": [1, 2]},
        {"key": 4, "value": [5, 6]},
        {"key": 5, "value": [3, 4]}
      ],
      "inners": [
        {"key": 0, "value": {"children": [2], "relaxed": false}},
        {"key": 3, "value": {"children": [2, 5], "relaxed": false}}
      ],
      "vectors": [{"root": 0, "tail": 1}, {"root": 3, "tail": 4}]
    },
    "vector_of_strings": {
      "B": 5,
      "BL": 1,
      "leaves": [
        {"key": 1, "value": ["one", "two"]},
        {"key": 3, "value": ["five"]},
        {"key": 4, "value": ["three", "four"]}
      ],
      "inners": [
        {"key": 0, "value": {"children": [], "relaxed": false}},
        {"key": 2, "value": {"children": [1, 4], "relaxed": false}}
      ],
      "vectors": [{"root": 0, "tail": 1}, {"root": 2, "tail": 3}]
    }
  }
}
    )");
    // include:end-doc_2-json
    REQUIRE(json_t::parse(str) == expected_json);

    // include:start-doc_2-load
    const auto loaded_value =
        immer::persist::cereal_load_with_pools<doc_2>(str, doc_2_policy{});
    // include:end-doc_2-load
    REQUIRE(value == loaded_value);
}

TEST_CASE("Transform into same type", "[docs]")
{
    const auto v1    = vector_one{1, 2, 3};
    const auto v2    = v1.push_back(4).push_back(5).push_back(6);
    const auto value = document{v1, v2};

    // include:start-get_auto_pool
    const auto pools = immer::persist::get_auto_pool(value);
    // include:end-get_auto_pool

    // include:start-conversion_map
    namespace hana            = boost::hana;
    const auto conversion_map = hana::make_map(hana::make_pair(
        hana::type_c<vector_one>, [](int val) { return val * 10; }));
    // include:end-conversion_map

    // include:start-transformed_pools
    auto transformed_pools =
        immer::persist::transform_output_pool(pools, conversion_map);
    // include:end-transformed_pools

    // include:start-convert-containers
    const auto new_v1 =
        immer::persist::convert_container(pools, transformed_pools, v1);
    const auto expected_new_v1 = vector_one{10, 20, 30};
    REQUIRE(new_v1 == expected_new_v1);

    const auto new_v2 =
        immer::persist::convert_container(pools, transformed_pools, v2);
    const auto expected_new_v2 = vector_one{10, 20, 30, 40, 50, 60};
    REQUIRE(new_v2 == expected_new_v2);

    const auto new_value = document{new_v1, new_v2};
    // include:end-convert-containers

    // include:start-save-new_value
    const auto policy =
        immer::persist::hana_struct_auto_member_name_policy(document{});
    const auto str = immer::persist::cereal_save_with_pools(new_value, policy);
    const auto expected_json = json_t::parse(R"(
{
  "pools": {
    "ints": {
      "B": 5,
      "BL": 1,
      "inners": [
        {"key": 0, "value": {"children": [2], "relaxed": false}},
        {"key": 3, "value": {"children": [2, 5], "relaxed": false}}
      ],
      "leaves": [
        {"key": 1, "value": [30]},
        {"key": 2, "value": [10, 20]},
        {"key": 4, "value": [50, 60]},
        {"key": 5, "value": [30, 40]}
      ],
      "vectors": [{"root": 0, "tail": 1}, {"root": 3, "tail": 4}]
    }
  },
  "value0": {"ints": 0, "ints2": 1}
}
    )");
    REQUIRE(json_t::parse(str) == expected_json);
    // include:end-save-new_value
}
