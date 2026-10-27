// Copyright (C) 2026 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#include "android/skin/qt/extended-pages/car-rotary-page.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <QApplication>
#include <QCoreApplication>
#include <QLabel>
#include <QMetaObject>
#include <QPushButton>
#include <atomic>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "android/emulation/control/adb/AdbInterface.h"
#include "android/skin/qt/stylesheet.h"

using android::base::System;
using android::emulation::AdbCommand;
using android::emulation::AdbCommandPtr;
using android::emulation::AdbInterface;
using android::emulation::OptionalAdbCommandResult;
using ::testing::_;
using ::testing::ElementsAre;
using ::testing::InSequence;
using ::testing::MockFunction;
using ::testing::Return;

// Stand-ins for the stylesheet helpers from aemu-ui-qt, which this test doesn't
// link.
namespace Ui {
const QHash<QString, QString>& stylesheetValues(SettingsTheme /*theme*/) {
    static QHash<QString, QString> values = {{"theme_path", "light"}};
    return values;
}
const char THEME_PATH_VAR[] = "theme_path";
}  // namespace Ui

namespace {

constexpr char kServiceCheck[] =
        "ps -A | grep \"com.android.car.rotary\" | awk '{print $9}'";
constexpr char kUpKey[] = "cmd car_service inject-key 280";
constexpr char kClockwise[] = "cmd car_service inject-rotary -c true";

// Behaves like AdbThroughExe: cancel() only sets a flag.
class FakeAdbCommand : public AdbCommand {
public:
    bool inFlight() const override { return mInFlight.load(); }
    void cancel() override { mCancelled.store(true); }
    void start(int /*checkTimeoutMs*/ = 1000) override {}

    void setInFlight(bool inFlight) { mInFlight.store(inFlight); }
    bool wasCancelled() const { return mCancelled.load(); }

private:
    std::atomic<bool> mInFlight{true};
    std::atomic<bool> mCancelled{false};
};

class MockAdbInterface : public AdbInterface {
public:
    MOCK_METHOD(bool, isAdbVersionCurrent, (), (const, override));
    MOCK_METHOD(void, setCustomAdbPath, (const std::string& path), (override));
    MOCK_METHOD(const std::string&, detectedAdbPath, (), (const, override));
    MOCK_METHOD(const std::string&, adbPath, (), (override));
    MOCK_METHOD(void, setSerialNumberPort, (int port), (override));
    MOCK_METHOD(const std::string&, serialString, (), (const, override));
    MOCK_METHOD(AdbCommandPtr,
                runAdbCommand,
                (const std::vector<std::string>& args,
                 ResultCallback result_callback,
                 System::Duration timeout_ms,
                 bool want_output),
                (override));
    MOCK_METHOD(void,
                enqueueCommand,
                (const std::vector<std::string>& args,
                 ResultCallback result_callback),
                (override));
};

// Mock action: stores the command's callback in |callback|, returns |command|.
auto captureCallback(AdbInterface::ResultCallback* callback,
                     std::shared_ptr<FakeAdbCommand> command) {
    return [callback, command](const std::vector<std::string>&,
                               AdbInterface::ResultCallback cb,
                               System::Duration, bool) -> AdbCommandPtr {
        *callback = std::move(cb);
        return command;
    };
}

// Completes |command| the way AdbThroughExe does: runs |callback| on another
// thread, then marks the command as no longer in flight.
void completeOnAdbThread(const AdbInterface::ResultCallback& callback,
                         const std::shared_ptr<FakeAdbCommand>& command,
                         System::ProcessExitCode exitCode,
                         const std::string& output) {
    std::thread adbThread([&]() {
        OptionalAdbCommandResult result;
        result.emplace(exitCode, new std::istringstream(output));
        callback(result);
        command->setInFlight(false);
    });
    adbThread.join();
}

}  // namespace

// Friend of CarRotaryPage: gives the tests access to its internals.
class CarRotaryPageTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char* argv[] = {const_cast<char*>("test")};
            new QApplication(argc, argv);
        }

        mPage = std::make_unique<CarRotaryPage>();
        mPage->setAdbInterface(&mAdb);
        mPage->mBootCompletedCallback = [this]() { return mBootCompleted; };
        mWarningLabel = mPage->findChild<QLabel*>("carrotary_warningMessage");
        ASSERT_NE(mWarningLabel, nullptr);
    }

    void TearDown() override {
        mPage.reset();
        QCoreApplication::processEvents();
    }

    void checkService() { mPage->checkRotaryControllerService(); }
    bool checkTimerActive() const { return mPage->mCheckTimer.isActive(); }
    void fireLongPressTimer() {
        ASSERT_TRUE(
                QMetaObject::invokeMethod(&mPage->mLongPressTimer, "timeout"));
    }
    QPushButton* button(const char* name) const {
        return mPage->findChild<QPushButton*>(name);
    }

    MockAdbInterface mAdb;
    std::unique_ptr<CarRotaryPage> mPage;
    QLabel* mWarningLabel = nullptr;
    bool mBootCompleted = true;
};

TEST_F(CarRotaryPageTest, DoesNotQueryServiceBeforeBootCompleted) {
    mBootCompleted = false;
    EXPECT_CALL(mAdb, runAdbCommand(_, _, _, _)).Times(0);

    checkService();
}

TEST_F(CarRotaryPageTest, ServiceNotRunningShowsWarningOnQtThread) {
    AdbInterface::ResultCallback callback;
    auto command = std::make_shared<FakeAdbCommand>();
    EXPECT_CALL(mAdb,
                runAdbCommand(ElementsAre("shell", kServiceCheck), _, _, true))
            .WillOnce(captureCallback(&callback, command));

    checkService();
    ASSERT_TRUE(static_cast<bool>(callback));
    completeOnAdbThread(callback, command, 0, "");

    // The ADB thread must not touch the UI: the update runs on the Qt thread.
    EXPECT_TRUE(mWarningLabel->isHidden());
    QCoreApplication::processEvents();
    EXPECT_FALSE(mWarningLabel->isHidden());
    // Keeps polling until the service shows up.
    EXPECT_TRUE(checkTimerActive());
}

TEST_F(CarRotaryPageTest,
       ServiceRunningHidesWarningAndStopsCheckTimerOnQtThread) {
    mWarningLabel->setVisible(true);
    ASSERT_TRUE(checkTimerActive());
    AdbInterface::ResultCallback callback;
    auto command = std::make_shared<FakeAdbCommand>();
    EXPECT_CALL(mAdb,
                runAdbCommand(ElementsAre("shell", kServiceCheck), _, _, true))
            .WillOnce(captureCallback(&callback, command));

    checkService();
    ASSERT_TRUE(static_cast<bool>(callback));
    completeOnAdbThread(callback, command, 0, "com.android.car.rotary\n");

    // Nothing changes until the Qt thread handles the result.
    EXPECT_FALSE(mWarningLabel->isHidden());
    EXPECT_TRUE(checkTimerActive());
    QCoreApplication::processEvents();
    EXPECT_TRUE(mWarningLabel->isHidden());
    EXPECT_FALSE(checkTimerActive());
}

TEST_F(CarRotaryPageTest, SkipsServiceCheckUntilPreviousOneIsHandled) {
    AdbInterface::ResultCallback callback;
    auto command = std::make_shared<FakeAdbCommand>();
    MockFunction<void()> previousCheckHandled;
    {
        InSequence sequence;
        EXPECT_CALL(mAdb, runAdbCommand(ElementsAre("shell", kServiceCheck), _,
                                        _, true))
                .WillOnce(captureCallback(&callback, command));
        EXPECT_CALL(previousCheckHandled, Call());
        EXPECT_CALL(mAdb, runAdbCommand(ElementsAre("shell", kServiceCheck), _,
                                        _, true))
                .WillOnce(Return(std::make_shared<FakeAdbCommand>()));
    }

    checkService();
    checkService();  // Still running: skipped.

    // Finished on the ADB thread but not handled on the Qt thread yet: skipped.
    completeOnAdbThread(callback, command, 0, "");
    checkService();

    QCoreApplication::processEvents();
    previousCheckHandled.Call();
    checkService();  // Sent.
}

TEST_F(CarRotaryPageTest,
       DestroyingPageCancelsCommandsAndIgnoresLateCallbacks) {
    AdbInterface::ResultCallback checkCallback;
    AdbInterface::ResultCallback upCallback;
    auto checkCommand = std::make_shared<FakeAdbCommand>();
    auto upCommand = std::make_shared<FakeAdbCommand>();
    EXPECT_CALL(mAdb,
                runAdbCommand(ElementsAre("shell", kServiceCheck), _, _, true))
            .WillOnce(captureCallback(&checkCallback, checkCommand));
    EXPECT_CALL(mAdb, runAdbCommand(ElementsAre("shell", kUpKey, "-a down"), _,
                                    _, true))
            .WillOnce(captureCallback(&upCallback, upCommand));

    checkService();
    QPushButton* upButton = button("carrotary_upButton");
    ASSERT_NE(upButton, nullptr);
    QMetaObject::invokeMethod(upButton, "pressed");
    ASSERT_TRUE(static_cast<bool>(checkCallback));
    ASSERT_TRUE(static_cast<bool>(upCallback));

    // Destroy the page while both commands are still running (b/574217516).
    mPage.reset();
    EXPECT_TRUE(checkCommand->wasCancelled());
    EXPECT_TRUE(upCommand->wasCancelled());

    // cancel() doesn't guarantee that the callback won't run, so late callbacks
    // must not touch the destroyed page (ASan catches it if they do).
    completeOnAdbThread(checkCallback, checkCommand, 0,
                        "com.android.car.rotary\n");
    completeOnAdbThread(upCallback, upCommand, 0, "");
    QCoreApplication::processEvents();
}

TEST_F(CarRotaryPageTest, DestroyingPageBeforeQueuedUpdateRunsIsSafe) {
    AdbInterface::ResultCallback callback;
    auto command = std::make_shared<FakeAdbCommand>();
    EXPECT_CALL(mAdb,
                runAdbCommand(ElementsAre("shell", kServiceCheck), _, _, true))
            .WillOnce(captureCallback(&callback, command));

    checkService();
    ASSERT_TRUE(static_cast<bool>(callback));
    // The callback queues the UI update on the Qt thread...
    completeOnAdbThread(callback, command, 0, "");
    // ...and the page is destroyed before that update runs.
    mPage.reset();
    QCoreApplication::processEvents();
}

TEST_F(CarRotaryPageTest, FinishedCommandDoesNotDropOtherPendingCommand) {
    AdbInterface::ResultCallback downCallback;
    AdbInterface::ResultCallback upCallback;
    auto downCommand = std::make_shared<FakeAdbCommand>();
    auto upCommand = std::make_shared<FakeAdbCommand>();
    EXPECT_CALL(mAdb, runAdbCommand(ElementsAre("shell", kUpKey, "-a down"), _,
                                    _, true))
            .WillOnce(captureCallback(&downCallback, downCommand));
    EXPECT_CALL(mAdb, runAdbCommand(ElementsAre("shell", kUpKey, "-a up"), _, _,
                                    true))
            .WillOnce(captureCallback(&upCallback, upCommand));

    QPushButton* upButton = button("carrotary_upButton");
    ASSERT_NE(upButton, nullptr);
    QMetaObject::invokeMethod(upButton, "pressed");
    QMetaObject::invokeMethod(upButton, "released");

    // "-a down" finishes while "-a up" is still running.
    completeOnAdbThread(downCallback, downCommand, 0, "");
    QCoreApplication::processEvents();

    mPage.reset();
    EXPECT_FALSE(downCommand->wasCancelled());
    EXPECT_TRUE(upCommand->wasCancelled());
}

TEST_F(CarRotaryPageTest, LongPressTickSendsOneCommandAfterRepeatedPresses) {
    // First release, one long-press tick, second release.
    EXPECT_CALL(mAdb,
                runAdbCommand(ElementsAre("shell", kClockwise), _, _, true))
            .Times(3)
            .WillRepeatedly(Return(std::make_shared<FakeAdbCommand>()));

    QPushButton* clockwise = button("carrotary_clockwiseButton");
    ASSERT_NE(clockwise, nullptr);
    QMetaObject::invokeMethod(clockwise, "pressed");
    QMetaObject::invokeMethod(clockwise, "released");
    QMetaObject::invokeMethod(clockwise, "pressed");
    fireLongPressTimer();
    QMetaObject::invokeMethod(clockwise, "released");
}
