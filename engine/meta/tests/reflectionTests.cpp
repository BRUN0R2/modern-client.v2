// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.meta;

// Described types live in a named namespace: their qualified names are part of what is checked.
namespace nx_test {

struct Replicated {
    std::uint8_t audience = 0;
    auto operator==(const Replicated&) const -> bool = default;
};

struct Range {
    float minimum = 0.0F;
    float maximum = 0.0F;
    auto operator==(const Range&) const -> bool = default;
};

struct Ammo {
    std::int16_t clip = 0;
    std::int16_t reserve = 0;
};

struct Health {
    float value = 0.0F;
};

} // namespace nx_test

template <>
struct nx::meta::Description<nx_test::Ammo> {
    static constexpr std::string_view kName = "nx_test::Ammo";
    static constexpr std::tuple kAnnotations{nx_test::Replicated{.audience = 1}};
    static constexpr std::tuple kFields{
        nx::meta::field<&nx_test::Ammo::clip>("clip", nx_test::Range{.minimum = 0.0F, .maximum = 100.0F}),
        nx::meta::field<&nx_test::Ammo::reserve>("reserve"),
    };
};

template <>
struct nx::meta::Description<nx_test::Health> {
    static constexpr std::string_view kName = "nx_test::Health";
    static constexpr std::tuple kFields{nx::meta::field<&nx_test::Health::value>("value")};
};

namespace {

using nx_test::Ammo;
using nx_test::Range;
using nx_test::Replicated;

static_assert(nx::meta::Described<Ammo>);
static_assert(!nx::meta::Described<int>);
static_assert(!nx::meta::Described<Replicated>, "a type without a Description is not described");

TEST(Reflection, NamesAndCountsComeFromTheDescription) {
    EXPECT_EQ(nx::meta::qualifiedName<Ammo>(), "nx_test::Ammo");
    EXPECT_EQ(nx::meta::fieldCount<Ammo>(), 2U);
    EXPECT_EQ(nx::meta::fieldCount<nx_test::Health>(), 1U);
}

TEST(Reflection, ForEachFieldVisitsTheFieldsInDeclarationOrder) {
    Ammo ammo{.clip = 30, .reserve = 90};
    std::vector<std::string_view> names;
    std::vector<std::int16_t> values;
    nx::meta::forEachField<Ammo>([&]<class Field>(Field) {
        names.push_back(Field::kName);
        values.push_back(Field::get(std::as_const(ammo)));
        Field::get(ammo) = static_cast<std::int16_t>(Field::get(ammo) + 1);
    });

    EXPECT_EQ(names, (std::vector<std::string_view>{"clip", "reserve"}));
    EXPECT_EQ(values, (std::vector<std::int16_t>{30, 90}));
    EXPECT_EQ(ammo.clip, 31);
    EXPECT_EQ(ammo.reserve, 91);
}

TEST(Reflection, AnnotationsOfTypesAndFields) {
    static_assert(nx::meta::typeAnnotation<Replicated, Ammo>() == Replicated{.audience = 1});
    static_assert(!nx::meta::typeAnnotation<Range, Ammo>().has_value());
    static_assert(!nx::meta::typeAnnotation<Replicated, nx_test::Health>().has_value());

    using Clip = nx::meta::Field<Ammo, 0>;
    using Reserve = nx::meta::Field<Ammo, 1>;
    static_assert(std::same_as<Clip::ValueType, std::int16_t>);
    static_assert(Clip::annotation<Range>() == Range{.minimum = 0.0F, .maximum = 100.0F});
    static_assert(!Reserve::annotation<Range>().has_value());
    SUCCEED();
}

} // namespace

// The native path: GCC 16 with -freflection compares every description with its type.
#if defined(__cpp_impl_reflection)

namespace nx_test {

struct[[= Replicated{.audience = 2}]] Annotated {
    [[= Range{.minimum = 1.0F, .maximum = 5.0F}]] float scale = 1.0F;
};

struct Pair {
    int first = 0;
    int second = 0;
};

struct Renamed {
    int value = 0;
};
struct Missing {
    int first = 0;
    int second = 0;
};
struct Reordered {
    int first = 0;
    int second = 0;
};
struct OtherMember {
    int first = 0;
    int second = 0;
};
struct[[= Replicated{.audience = 3}]] OtherAnnotation {
    int value = 0;
};

} // namespace nx_test

template <>
struct nx::meta::Description<nx_test::Annotated> {
    static constexpr std::string_view kName = "nx_test::Annotated";
    static constexpr std::tuple kAnnotations{nx_test::Replicated{.audience = 2}};
    static constexpr std::tuple kFields{nx::meta::field<&nx_test::Annotated::scale>(
        "scale", nx_test::Range{.minimum = 1.0F, .maximum = 5.0F})};
};

template <>
struct nx::meta::Description<nx_test::Pair> {
    static constexpr std::string_view kName = "nx_test::Pair";
    static constexpr std::tuple kFields{nx::meta::field<&nx_test::Pair::first>("first"),
                                        nx::meta::field<&nx_test::Pair::second>("second")};
};

template <>
struct nx::meta::Description<nx_test::Renamed> {
    static constexpr std::string_view kName = "Renamed";
    static constexpr std::tuple kFields{nx::meta::field<&nx_test::Renamed::value>("value")};
};

template <>
struct nx::meta::Description<nx_test::Missing> {
    static constexpr std::string_view kName = "nx_test::Missing";
    static constexpr std::tuple kFields{nx::meta::field<&nx_test::Missing::first>("first")};
};

template <>
struct nx::meta::Description<nx_test::Reordered> {
    static constexpr std::string_view kName = "nx_test::Reordered";
    static constexpr std::tuple kFields{nx::meta::field<&nx_test::Reordered::second>("second"),
                                        nx::meta::field<&nx_test::Reordered::first>("first")};
};

template <>
struct nx::meta::Description<nx_test::OtherMember> {
    static constexpr std::string_view kName = "nx_test::OtherMember";
    static constexpr std::tuple kFields{nx::meta::field<&nx_test::OtherMember::second>("first"),
                                        nx::meta::field<&nx_test::OtherMember::second>("second")};
};

template <>
struct nx::meta::Description<nx_test::OtherAnnotation> {
    static constexpr std::string_view kName = "nx_test::OtherAnnotation";
    static constexpr std::tuple kAnnotations{nx_test::Replicated{.audience = 1}};
    static constexpr std::tuple kFields{nx::meta::field<&nx_test::OtherAnnotation::value>("value")};
};

namespace {

TEST(NativeReflection, MatchingDescriptionsPass) {
    static_assert(nx::meta::native::kAvailable);
    static_assert(nx::meta::native::mismatch<nx_test::Ammo>().empty());
    static_assert(nx::meta::native::mismatch<nx_test::Pair>().empty());
    static_assert(nx::meta::native::mismatch<nx_test::Annotated>().empty(),
                  "annotations written on the type (P3394) agree with the description");
    static_assert(nx::meta::fieldCount<nx_test::Annotated>() == 1);
    SUCCEED();
}

TEST(NativeReflection, EveryKindOfDriftIsReported) {
    using nx::meta::native::mismatch;
    EXPECT_EQ(mismatch<nx_test::Renamed>(),
              R"(the description is named "Renamed", the type "nx_test::Renamed")");
    EXPECT_EQ(
        mismatch<nx_test::Missing>(),
        R"(the description of "nx_test::Missing" lists a different number of fields than the type declares)");
    EXPECT_EQ(mismatch<nx_test::Reordered>(),
              R"(field "second" is "first" in the type; fields are listed in declaration order)");
    EXPECT_EQ(mismatch<nx_test::OtherMember>(), R"(field "first" names another member of the type)");
    EXPECT_EQ(mismatch<nx_test::OtherAnnotation>(),
              R"("nx_test::OtherAnnotation" has other annotations in the type)");
}

} // namespace

#else

TEST(NativeReflection, UnavailableOnThisCompiler) {
    static_assert(!nx::meta::native::kAvailable);
    GTEST_SKIP() << "no C++26 reflection (P2996): descriptions are not checked against their types";
}

#endif
