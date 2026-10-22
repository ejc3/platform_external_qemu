// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "android/skin/qt/extended-pages/snapshot-page.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <QApplication>
#include <QCoreApplication>
#include <QSettings>
#include <QTextEdit>
#include <QTreeWidget>
#include <cstdlib>
#include <cstring>

#include "android/cmdline-definitions.h"
#include "android/console.h"
#include "android/skin/qt/extended-pages/snapshot_controller.h"
#include "android/skin/qt/qt-settings.h"
#include "android/skin/qt/stylesheet.h"
#include "host-common/hw-config.h"

namespace Ui {
const QString& stylesheetForTheme(SettingsTheme theme) {
    static QString empty;
    return empty;
}
const QHash<QString, QString>& stylesheetValues(SettingsTheme theme) {
    static QHash<QString, QString> values;
    return values;
}
const char THEME_PATH_VAR[] = "theme_path";
const char THEME_TEXT_COLOR[] = "text_color";
}  // namespace Ui

using android::emulation::control::SnapshotController;
using android::emulation::control::SnapshotInfo;
using android::emulation::control::SnapshotScreenshot;
using android::emulation::control::SnapshotStatus;
using ::testing::_;
using ::testing::NiceMock;

class MockSnapshotController : public SnapshotController {
public:
    MOCK_METHOD(void,
                listSnapshots,
                (std::function<void(absl::StatusOr<std::vector<SnapshotInfo>>)>
                         callback),
                (override));
    MOCK_METHOD(void,
                loadSnapshot,
                (const std::string& snapshotId,
                 const std::string& destination,
                 std::function<void(absl::Status)> callback),
                (override));
    MOCK_METHOD(void,
                saveSnapshot,
                (const std::string& snapshotId,
                 std::function<void(absl::Status)> callback),
                (override));
    MOCK_METHOD(void,
                deleteSnapshot,
                (const std::string& snapshotId,
                 std::function<void(absl::Status)> callback),
                (override));
    MOCK_METHOD(void,
                updateSnapshot,
                (const SnapshotInfo& details,
                 std::function<void(absl::Status)> callback),
                (override));
    MOCK_METHOD(void,
                getScreenshot,
                (const std::string& snapshotId,
                 std::function<void(absl::StatusOr<SnapshotScreenshot>)>
                         callback),
                (override));
};

class SnapshotPageTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char* argv[] = {(char*)"test"};
            new QApplication(argc, argv);
        }
        QCoreApplication::setOrganizationName("GoogleTest");
        QCoreApplication::setApplicationName("SnapshotPageTest");

        static AndroidOptions emptyOpts = {};
        getConsoleAgents()->settings->inject_cmdLineOptions(&emptyOpts);
        mAvdName = strdup("test_avd");
        getConsoleAgents()->settings->hw()->avd_name = mAvdName;
    }

    void TearDown() override {
        mPage.reset();
        QSettings settings;
        settings.clear();
        getConsoleAgents()->settings->hw()->avd_name = nullptr;
        free(mAvdName);
        mAvdName = nullptr;
    }

    void createPageWithSetting(Ui::Settings::DeleteInvalidSnapshots setting) {
        QSettings settings;
        settings.setValue(Ui::Settings::DELETE_INVALID_SNAPSHOTS,
                          static_cast<int>(setting));

        mPage = std::make_unique<SnapshotPage>();
        auto mock = std::make_unique<NiceMock<MockSnapshotController>>();
        mController = mock.get();
        mPage->setControllerForTest(std::move(mock));
    }

    static std::vector<SnapshotInfo> sampleSnapshots() {
        return {
                {.snapshot_id = "snap_compatible",
                 .status = SnapshotStatus::Compatible,
                 .logical_name = "Compatible Snap",
                 .size = 128ULL * 1024 * 1024},
                {.snapshot_id = "snap_incompatible_1",
                 .status = SnapshotStatus::Incompatible,
                 .logical_name = "Incompatible Snap 1",
                 .size = 256ULL * 1024 * 1024},
                {.snapshot_id = "snap_incompatible_2",
                 .status = SnapshotStatus::Incompatible,
                 .logical_name = "Incompatible Snap 2",
                 .size = 512ULL * 1024 * 1024},
        };
    }

    std::unique_ptr<SnapshotPage> mPage;
    MockSnapshotController* mController = nullptr;
    char* mAvdName = nullptr;
};

// Regression test for b/562506498: listing incompatible snapshots when
// DeleteInvalidSnapshots is No must NOT silently delete them in the background.
TEST_F(SnapshotPageTest, ListingIncompatibleSnapshotsDoesNotDeleteWhenSetToNo) {
    createPageWithSetting(Ui::Settings::DeleteInvalidSnapshots::No);

    EXPECT_CALL(*mController, listSnapshots(_)).WillOnce([](auto cb) {
        cb(sampleSnapshots());
    });
    EXPECT_CALL(*mController, deleteSnapshot(_, _)).Times(0);

    mPage->show();

    auto* tree = mPage->findChild<QTreeWidget*>("snapshotDisplay");
    ASSERT_NE(tree, nullptr);
    EXPECT_EQ(tree->topLevelItemCount(), 3);
}

// Regression test for b/562506498: listing incompatible snapshots when
// DeleteInvalidSnapshots is Ask (default) must NOT delete them before user
// confirmation, and must emit askAboutInvalidSnapshots with only the
// incompatible snapshots.
TEST_F(SnapshotPageTest,
       ListingIncompatibleSnapshotsDoesNotDeleteBeforeConfirmationWhenSetToAsk) {
    createPageWithSetting(Ui::Settings::DeleteInvalidSnapshots::Ask);

    std::vector<SnapshotInfo> askedSnapshots;
    QObject::connect(mPage.get(), &SnapshotPage::askAboutInvalidSnapshots,
                     [&askedSnapshots](std::vector<SnapshotInfo> snaps) {
                         askedSnapshots = std::move(snaps);
                     });

    EXPECT_CALL(*mController, listSnapshots(_)).WillOnce([](auto cb) {
        cb(sampleSnapshots());
    });
    EXPECT_CALL(*mController, deleteSnapshot(_, _)).Times(0);

    mPage->show();

    auto* tree = mPage->findChild<QTreeWidget*>("snapshotDisplay");
    ASSERT_NE(tree, nullptr);
    EXPECT_EQ(tree->topLevelItemCount(), 3);
    ASSERT_EQ(askedSnapshots.size(), 2u);
    EXPECT_EQ(askedSnapshots[0].snapshot_id, "snap_incompatible_1");
    EXPECT_EQ(askedSnapshots[1].snapshot_id, "snap_incompatible_2");
}

// Verify Auto mode still automatically deletes only the incompatible snapshots
// and refreshes the display list once without infinite recursion.
TEST_F(SnapshotPageTest, ListingIncompatibleSnapshotsAutoDeletesWhenSetToAuto) {
    createPageWithSetting(Ui::Settings::DeleteInvalidSnapshots::Auto);

    EXPECT_CALL(*mController, listSnapshots(_))
            .Times(2)
            .WillOnce([](auto cb) { cb(sampleSnapshots()); })
            .WillOnce([](auto cb) {
                cb(std::vector<SnapshotInfo>{
                        {.snapshot_id = "snap_compatible",
                         .status = SnapshotStatus::Compatible,
                         .logical_name = "Compatible Snap",
                         .size = 128ULL * 1024 * 1024},
                });
            });
    EXPECT_CALL(*mController, deleteSnapshot("snap_compatible", _)).Times(0);
    EXPECT_CALL(*mController, deleteSnapshot("snap_incompatible_1", _))
            .WillOnce([](const std::string&, auto cb) {
                cb(absl::OkStatus());
            });
    EXPECT_CALL(*mController, deleteSnapshot("snap_incompatible_2", _))
            .WillOnce([](const std::string&, auto cb) {
                cb(absl::OkStatus());
            });

    mPage->show();

    auto* tree = mPage->findChild<QTreeWidget*>("snapshotDisplay");
    ASSERT_NE(tree, nullptr);
    EXPECT_EQ(tree->topLevelItemCount(), 1);
}

// Regression test for b/562506498: after one incompatible snapshot is deleted
// and slot_snapshotDeleteCompleted() refreshes the list, the remaining
// incompatible snapshots are still preserved and displayed with their size.
TEST_F(SnapshotPageTest,
       RefreshingAfterSingleDeletePreservesRemainingIncompatibleSnapshots) {
    createPageWithSetting(Ui::Settings::DeleteInvalidSnapshots::No);

    EXPECT_CALL(*mController, listSnapshots(_))
            .WillOnce([](auto cb) { cb(sampleSnapshots()); })
            .WillOnce([](auto cb) {
                cb(std::vector<SnapshotInfo>{
                        {.snapshot_id = "snap_compatible",
                         .status = SnapshotStatus::Compatible,
                         .logical_name = "Compatible Snap",
                         .size = 128ULL * 1024 * 1024},
                        {.snapshot_id = "snap_incompatible_2",
                         .status = SnapshotStatus::Incompatible,
                         .logical_name = "Incompatible Snap 2",
                         .size = 512ULL * 1024 * 1024},
                });
            });
    EXPECT_CALL(*mController, deleteSnapshot(_, _)).Times(0);

    mPage->show();

    auto* tree = mPage->findChild<QTreeWidget*>("snapshotDisplay");
    ASSERT_NE(tree, nullptr);
    ASSERT_EQ(tree->topLevelItemCount(), 3);

    // Simulate completion of deleting snap_incompatible_1.
    mPage->slot_snapshotDeleteCompleted();
    EXPECT_EQ(tree->topLevelItemCount(), 2);

    // Selecting the remaining incompatible snapshot should display its size
    // (512 MB) from SnapshotInfo::size rather than 0 MB.
    tree->setCurrentItem(tree->topLevelItem(1));
    auto* info = mPage->findChild<QTextEdit*>("selectionInfo");
    ASSERT_NE(info, nullptr);
    EXPECT_TRUE(info->toPlainText().contains("512 MB"));
}
