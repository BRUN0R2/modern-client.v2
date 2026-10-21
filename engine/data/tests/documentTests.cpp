// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.data;

#include "contractRecorder.hpp"

namespace {

using nx::data::DataError;
using nx::data::Document;
using nx::data::NodeKind;
using nx::data::SourcePosition;

constexpr std::string_view kSample = R"(profile = "classic"
volume = 0.5
rounds = 30
enabled = true

[goldsrcInstall]
path = "C:/Games/Half-Life"
mounts = ["cstrike", "valve"]
)";

TEST(Document, ViewsReadEveryKindOfValue) {
    const auto document = Document::parse(kSample, "content.toml");
    ASSERT_TRUE(document.has_value()) << nx::describe(document.error());
    const auto root = document->root();

    EXPECT_EQ(root.find("profile")->asString(), std::optional<std::string_view>{"classic"});
    EXPECT_EQ(root.find("volume")->asFloat(), std::optional{0.5});
    EXPECT_EQ(root.find("rounds")->asInteger(), std::optional<std::int64_t>{30});
    EXPECT_FALSE(root.find("rounds")->asFloat().has_value()) << "an integer is not a float";
    EXPECT_EQ(root.find("enabled")->asBool(), std::optional{true});
    EXPECT_EQ(root.find("enabled")->kind(), NodeKind::Boolean);
    EXPECT_FALSE(root.find("missing").has_value());

    const auto install = root.find("goldsrcInstall")->asTable();
    ASSERT_TRUE(install.has_value());
    const auto mounts = install->find("mounts")->asArray();
    ASSERT_TRUE(mounts.has_value());
    const auto elements = mounts->elements();
    ASSERT_EQ(elements.size(), 2U);
    EXPECT_EQ(elements[1].asString(), std::optional<std::string_view>{"valve"});
}

TEST(Document, EntriesFollowTheFileOrderWithTheirPositions) {
    const auto document = Document::parse(kSample, "content.toml");
    ASSERT_TRUE(document.has_value());

    std::vector<std::string_view> keys;
    for (const auto& entry : document->root().entries()) {
        keys.push_back(entry.key);
    }
    EXPECT_EQ(keys,
              (std::vector<std::string_view>{"profile", "volume", "rounds", "enabled", "goldsrcInstall"}));
    EXPECT_EQ(document->root().entries()[1].position, (SourcePosition{.line = 2, .column = 1}));
    EXPECT_EQ(document->root().find("rounds")->position(), (SourcePosition{.line = 3, .column = 10}));
}

TEST(Document, SyntaxErrorsNameTheFileLineAndColumn) {
    const auto document = Document::parse("profile = \"classic\"\nvolume = = 1\n", "content.toml");
    ASSERT_FALSE(document.has_value());
    EXPECT_EQ(document.error().code, DataError::Syntax);
    EXPECT_TRUE(document.error().detail.starts_with("content.toml:2:")) << document.error().detail;
}

TEST(Document, ADocumentOutlivesTheTextItWasParsedFrom) {
    auto text = std::make_unique<std::string>("name = \"de_dust2\"");
    const auto document = Document::parse(*text, "map.toml");
    text.reset();
    ASSERT_TRUE(document.has_value());
    EXPECT_EQ(document->root().find("name")->asString(), std::optional<std::string_view>{"de_dust2"});
    EXPECT_EQ(document->sourceName(), "map.toml");
}

TEST(Document, AMovedFromDocumentBreaksTheContractAndReadsAsEmpty) {
    const nx::test::ContractRecorder recorder;
    auto document = Document::parse("name = \"de_dust2\"", "map.toml");
    ASSERT_TRUE(document.has_value());
    const Document moved = std::move(*document);

    const auto root = document->root(); // NOLINT(bugprone-use-after-move): the contract under test
    EXPECT_EQ(recorder.count(), 1);
    EXPECT_FALSE(root.find("name").has_value()) << "observed: an empty document";
    EXPECT_TRUE(moved.root().find("name").has_value());
}

} // namespace
