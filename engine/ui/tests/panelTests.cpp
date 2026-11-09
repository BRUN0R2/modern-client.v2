// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Panels: placed by a layout's settings as the installation's layouts write them, and following
// their parent as it changes size by their pinCorner and autoResize.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;
import nx.ui;

#include "pixelGeometry.hpp"
namespace {

using nx::ui::AutoResize;
using nx::ui::Panel;
using nx::ui::PanelError;
using nx::ui::PinCorner;

[[nodiscard]] auto entriesOf(std::string_view text) -> std::vector<nx::assets::KeyValuesEntry> {
    auto entries = nx::assets::parseKeyValues(text);
    EXPECT_TRUE(entries.has_value());
    return entries.value_or(std::vector<nx::assets::KeyValuesEntry>{});
}

/// A dialog 512 by 400 with an OK and a Cancel button, laid out as OptionsSubMultiplayer.res lays its
/// buttons out, a list that grows with it, and a label from its right and middle.
class LayoutTest : public testing::Test {
protected:
    LayoutTest() {
        for (const std::string_view name : {"Cancel", "OK", "List", "Label"}) {
            std::ignore = dialog.add(std::make_unique<Panel>(std::string{name}));
        }
    }

    Panel dialog{"Dialog", nx::test::rectOf(0, 0, 512, 400)};
};

TEST_F(LayoutTest, PlacesPanelsAsALayoutSetsThem) {
    const auto layout = entriesOf(R"("Resource/OptionsSubMultiplayer.res"
{
	"Cancel"
	{
		"ControlName"		"Button"
		"fieldName"		"Cancel"
		"xpos"		"378"
		"ypos"		"322"
		"wide"		"64"
		"tall"		"24"
		"autoResize"		"0"
		"pinCorner"		"3"
		"visible"		"1"
		"enabled"		"1"
		"tabPosition"		"2"
	}
	"ok" { "xpos" "308" "ypos" "322" "wide" "64" "tall" "24" "PinCorner" "3" "enabled" "0" }
	"Label" { "xpos" "r80" "ypos" "c-12" "wide" "70" "tall" "24" "visible" "0" }
	"Nothing" { "xpos" "1" }
}
)");
    ASSERT_EQ(layout.size(), 1U);
    ASSERT_TRUE(dialog.applyLayout(layout.front()).has_value());

    const Panel& cancel = *dialog.find("Cancel");
    EXPECT_EQ(cancel.bounds(), nx::test::rectOf(378, 322, 64, 24));
    EXPECT_EQ(cancel.pinCorner(), PinCorner::BottomRight);
    EXPECT_EQ(cancel.autoResize(), AutoResize::None);
    EXPECT_TRUE(cancel.isVisible());
    EXPECT_EQ(cancel.tabPosition(), 2);
    const Panel& ok = *dialog.find("OK");
    EXPECT_EQ(ok.bounds().x, nx::Pixels{308}) << "its block found in any case";
    EXPECT_EQ(ok.pinCorner(), PinCorner::BottomRight) << "a setting's name in any case";
    EXPECT_FALSE(ok.isEnabled());
    const Panel& label = *dialog.find("Label");
    EXPECT_EQ(label.bounds(), nx::test::rectOf(512 - 80, (400 / 2) - 12, 70, 24))
        << "r from the parent's right, c from its middle";
    EXPECT_FALSE(label.isVisible());
    EXPECT_EQ(dialog.find("List")->bounds(), nx::ui::PixelRect{}) << "no block: left as it is";
}

TEST_F(LayoutTest, PanelsFollowTheirParentByTheirCornerAndResizing) {
    Panel& cancel = *dialog.find("Cancel");
    cancel.setBounds(nx::test::rectOf(378, 322, 64, 24));
    cancel.setAnchors(PinCorner::BottomRight, AutoResize::None);
    Panel& list = *dialog.find("List");
    list.setBounds(nx::test::rectOf(10, 10, 492, 300));
    list.setAnchors(PinCorner::TopLeft, AutoResize::DownAndRight);
    Panel& label = *dialog.find("Label");
    label.setBounds(nx::test::rectOf(10, 330, 100, 20));
    label.setAnchors(PinCorner::BottomLeft, AutoResize::Right);
    Panel& ok = *dialog.find("OK");
    ok.setBounds(nx::test::rectOf(308, 322, 64, 24));
    ok.setAnchors(PinCorner::TopRight, AutoResize::Down);

    dialog.setBounds(nx::test::rectOf(0, 0, 612, 450));

    EXPECT_EQ(cancel.bounds(), nx::test::rectOf(478, 372, 64, 24))
        << "pinned to the bottom right: kept as far from it";
    EXPECT_EQ(list.bounds(), nx::test::rectOf(10, 10, 592, 350)) << "grows down and right with its parent";
    EXPECT_EQ(label.bounds(), nx::test::rectOf(10, 380, 200, 20))
        << "pinned to the bottom left, its right edge following the parent's";
    EXPECT_EQ(ok.bounds(), nx::test::rectOf(408, 322, 64, 74))
        << "pinned to the top right, its bottom edge following the parent's";
}

TEST_F(LayoutTest, ChildrenOfChildrenFollowToo) {
    Panel& list = *dialog.find("List");
    list.setBounds(nx::test::rectOf(0, 0, 512, 400));
    list.setAnchors(PinCorner::TopLeft, AutoResize::DownAndRight);
    Panel& scrollBar = list.add(std::make_unique<Panel>("ScrollBar", nx::test::rectOf(496, 0, 16, 400)));
    scrollBar.setAnchors(PinCorner::TopRight, AutoResize::Down);
    EXPECT_EQ(scrollBar.parent(), &list);

    dialog.setBounds(nx::test::rectOf(0, 0, 640, 480));

    EXPECT_EQ(scrollBar.bounds(), nx::test::rectOf(624, 0, 16, 480));
}

TEST_F(LayoutTest, ASettingThatCannotBeReadChangesNothing) {
    Panel& cancel = *dialog.find("Cancel");
    cancel.setBounds(nx::test::rectOf(1, 2, 3, 4));
    for (const std::string_view text : {R"("Cancel" { "xpos" "10" "wide" "big" })",
                                        R"("Cancel" { "xpos" "x10" })", R"("Cancel" { "pinCorner" "4" })",
                                        R"("Cancel" { "autoResize" "-1" })", R"("Cancel" { "ypos" "r" })"}) {
        const auto refused = cancel.applySettings(entriesOf(text).front());
        ASSERT_FALSE(refused.has_value()) << text;
        EXPECT_EQ(refused.error().code, PanelError::BadSetting) << text;
        EXPECT_EQ(cancel.bounds(), nx::test::rectOf(1, 2, 3, 4)) << text;
    }
}

} // namespace
