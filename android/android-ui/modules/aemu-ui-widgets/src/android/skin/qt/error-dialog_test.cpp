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

#include "android/skin/qt/error-dialog.h"

#include <gtest/gtest.h>
#include <QApplication>
#include <QFrame>
#include <QGuiApplication>
#include <QMessageBox>
#include <QTimer>
#include <QWidget>

class ErrorDialogTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!QCoreApplication::instance()) {
            static int argc = 1;
            static char* argv[] = {const_cast<char*>("error-dialog-test")};
            new QApplication(argc, argv);
        }
        deleteErrorDialog();
    }

    void TearDown() override {
        deleteErrorDialog();
    }
};

TEST_F(ErrorDialogTest, EmbeddedModeWithoutVisibleWindowDoesNotShowModalDialog) {
    QWidget hiddenMainWindow;
    initErrorDialog(&hiddenMainWindow, /*qt_hide_window=*/true);

    bool modalShown = false;
    QTimer::singleShot(0, [&]() {
        if (QApplication::activeModalWidget()) {
            modalShown = true;
            QApplication::activeModalWidget()->close();
        }
    });

    showErrorDialog("Failed to parse KML", "Geo Data Parser");
    QCoreApplication::processEvents();
    EXPECT_FALSE(modalShown);
}

TEST_F(ErrorDialogTest,
       EmbeddedModeWithVisibleExtendedControlsShowsDialogWithoutQuittingApp) {
    QWidget hiddenMainWindow;
    initErrorDialog(&hiddenMainWindow, /*qt_hide_window=*/true);

    bool lastWindowClosedFired = false;
    QFrame extendedWindow;
    extendedWindow.setObjectName("ExtendedControls");
#ifdef __linux__
    extendedWindow.setWindowFlags(Qt::SubWindow | Qt::WindowCloseButtonHint);
#else
    extendedWindow.setWindowFlags(Qt::Dialog | Qt::WindowCloseButtonHint);
#endif
    extendedWindow.show();
    QCoreApplication::processEvents();
    ASSERT_TRUE(extendedWindow.isVisible());

    QMetaObject::Connection conn = QObject::connect(
            qGuiApp, &QGuiApplication::lastWindowClosed, &extendedWindow,
            [&]() { lastWindowClosedFired = true; });

    bool dialogInspected = false;
    QString observedTitle;
    QString observedText;
    QWidget* observedParent = nullptr;
    bool observedQuitOnClose = true;

    QTimer::singleShot(0, [&]() {
        auto* box = qobject_cast<QMessageBox*>(
                QApplication::activeModalWidget());
        EXPECT_NE(box, nullptr);
        if (!box) {
            return;
        }
        dialogInspected = true;
        observedTitle = box->windowTitle();
        observedText = box->text();
        observedParent = box->parentWidget();
        observedQuitOnClose = box->testAttribute(Qt::WA_QuitOnClose);
        box->accept();
    });

    showErrorDialog("No valid locations found in the file.", "Geo Data Parser");
    QObject::disconnect(conn);

    EXPECT_TRUE(dialogInspected);
    EXPECT_EQ(observedTitle.toStdString(), "Geo Data Parser");
    EXPECT_EQ(observedText.toStdString(),
              "No valid locations found in the file.");
    EXPECT_EQ(observedParent, &extendedWindow);
    EXPECT_FALSE(observedQuitOnClose);
    EXPECT_FALSE(lastWindowClosedFired);
}

TEST_F(ErrorDialogTest, StandaloneModeShowsDialog) {
    QWidget mainWindow;
    mainWindow.show();
    initErrorDialog(&mainWindow, /*qt_hide_window=*/false);

    bool dialogInspected = false;
    QString observedTitle;
    QString observedText;
    bool observedQuitOnClose = true;

    QTimer::singleShot(0, [&]() {
        auto* box = qobject_cast<QMessageBox*>(
                QApplication::activeModalWidget());
        EXPECT_NE(box, nullptr);
        if (!box) {
            return;
        }
        dialogInspected = true;
        observedTitle = box->windowTitle();
        observedText = box->text();
        observedQuitOnClose = box->testAttribute(Qt::WA_QuitOnClose);
        box->accept();
    });

    showErrorDialog("XML parse error on line 4", "Geo Data Parser");

    EXPECT_TRUE(dialogInspected);
    EXPECT_EQ(observedTitle.toStdString(), "Geo Data Parser");
    EXPECT_EQ(observedText.toStdString(), "XML parse error on line 4");
    EXPECT_FALSE(observedQuitOnClose);
}
