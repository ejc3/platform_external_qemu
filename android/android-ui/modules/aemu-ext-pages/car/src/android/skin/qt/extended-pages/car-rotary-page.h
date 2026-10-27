// Copyright (C) 2020 The Android Open Source Project
//
// This software is licensed under the terms of the GNU General Public
// License version 2, as published by the Free Software Foundation, and
// may be copied, distributed, and modified under those terms.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#pragma once

#include <qobjectdefs.h>  // for Q_OBJECT

#include <QElapsedTimer>  // for QElapsedTimer
#include <QTime>          // for QTime
#include <QTimer>         // for QTimer
#include <QWidget>        // for QWidget
#include <cstdint>        // for uint64_t
#include <functional>     // for function
#include <memory>         // for shared_ptr, unique_ptr
#include <string>         // for string
#include <unordered_map>  // for unordered_map

#include "android/emulation/control/adb/AdbInterface.h"  // for AdbCommandPtr, AdbInterface
#include "ui_car-rotary-page.h"                          // for CarRotaryPage

class EmulatorQtWindow;
class QEvent;
class QObject;
class QPushButton;
class QWidget;

class CarRotaryPage : public QWidget {
    Q_OBJECT

public:
    explicit CarRotaryPage(QWidget* parent = 0);
    ~CarRotaryPage() override;
    void setAdbInterface(android::emulation::AdbInterface* adb);
    void setEmulatorWindow(EmulatorQtWindow* eW);

private:
    friend class CarRotaryPageTest;

    void toggleButtonPressed(QPushButton* button,
                             const std::string& cmd,
                             const bool useActionArg,
                             const std::string& label);
    void toggleButtonReleased(QPushButton* button,
                              const std::string& cmd,
                              const bool useActionArg,
                              const std::string& label);
    void toggleIconTheme(QPushButton* button, const bool pressed);
    void executeLastPushButtonCmd();
    void executeCmd(const std::string& cmd, const std::string& arg);
    void remaskButtons();
    bool eventFilter(QObject*, QEvent*) override;
    void checkRotaryControllerServiceTimer();
    void checkRotaryControllerService();
    void startSupportsActionTimer();
    void determineWhetherInjectKeySupportsActionArg();
    bool isBootCompleted();

    std::unique_ptr<Ui::CarRotaryPage> mUi;
    EmulatorQtWindow* mEmulatorWindow;
    android::emulation::AdbInterface* mAdb;
    QElapsedTimer mAdbExecuteTime;
    QTimer mLongPressTimer;
    QTimer mCheckTimer;
    QTimer mSupportsActionTimer;
    std::string mLastPushButtonCmd;
    bool mAdbExecuteIsActive;
    bool mSupportsActionArg;
    // The service check whose result hasn't been handled on the Qt thread yet.
    android::emulation::AdbCommandPtr mCheckCommand;
    // Button commands whose results haven't been handled on the Qt thread yet.
    std::unordered_map<uint64_t, android::emulation::AdbCommandPtr>
            mExecuteCommands;
    uint64_t mNextExecuteCommandId = 0;
    // Reset by the destructor. ADB callbacks hold a weak_ptr to it and do
    // nothing once it has expired.
    std::shared_ptr<bool> mLifeToken = std::make_shared<bool>(true);
    // Replaces bootCompleted() in tests.
    std::function<bool()> mBootCompletedCallback;
};
