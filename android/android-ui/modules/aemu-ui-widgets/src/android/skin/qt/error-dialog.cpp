// Copyright (C) 2015 The Android Open Source Project
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

#include <qmessagebox.h>  // for QMessageBox::Icon, QMessag...
#include <QApplication>   // for QApplication
#include <QMessageBox>    // for QMessageBox
#include <QPointer>       // for QPointer
#include <QWidget>        // for QWidget

#include "android/utils/debug.h"

class QString;

namespace {

class ErrorDialogHolder {
public:
    explicit ErrorDialogHolder(QWidget* parent) : mParent(parent) {}
    ~ErrorDialogHolder() {
        if (mDialog) {
            mDialog->deleteLater();
        }
    }

    QMessageBox& get() {
        if (!mDialog) {
            mDialog = new QMessageBox(QMessageBox::Warning, "", "",
                                      QMessageBox::Ok, mParent.data());
        }
        return *mDialog;
    }

private:
    QPointer<QWidget> mParent;
    QPointer<QMessageBox> mDialog;
};

static ErrorDialogHolder* sErrorDialog = nullptr;
static bool sErrorDialogInitialized = false;

static QWidget* findVisibleParentWindow() {
    if (!QApplication::instance()) {
        return nullptr;
    }
    if (QWidget* modal = QApplication::activeModalWidget()) {
        if (modal->isVisible()) {
            return modal;
        }
    }
    if (QWidget* active = QApplication::activeWindow()) {
        if (active->isVisible()) {
            return active;
        }
    }
    QWidget* fallback = nullptr;
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (!widget || !widget->isVisible()) {
            continue;
        }
        if (widget->objectName() == "ExtendedControls") {
            return widget;
        }
        if (!fallback) {
            fallback = widget;
        }
    }
    return fallback;
}

}  // namespace

void initErrorDialog(QWidget* parent, bool qt_hide_window) {
    // The underlying QMessageBox is parented to |parent|, and sErrorDialog is
    // cleaned up explicitly by deleteErrorDialog() when the emulator closes.
    sErrorDialogInitialized = true;
    if (!sErrorDialog && !qt_hide_window) {
        sErrorDialog = new ErrorDialogHolder(parent);
    }
}

void showErrorDialog(const QString& message, const QString& title) {
    // b/194152586 Showing error dialog in embedded mode when parented to the
    // hidden main window caused the emulator to exit on close because Qt
    // treated the dialog as the last open window. In embedded mode, only show
    // the dialog when a visible window (such as Extended Controls, b/299481525)
    // is open, and disable WA_QuitOnClose so closing the dialog never quits
    // QApplication.
    if (sErrorDialog) {
        QMessageBox& dialog = sErrorDialog->get();
        dialog.setAttribute(Qt::WA_QuitOnClose, false);
        dialog.setModal(true);
        dialog.setText(message);
        dialog.setWindowTitle(title);
        dialog.exec();
    } else if (sErrorDialogInitialized) {
        derror("%s : %s", title.toUtf8().constData(),
               message.toUtf8().constData());
        if (QWidget* parent = findVisibleParentWindow()) {
            QPointer<QMessageBox> msgBox =
                    new QMessageBox(QMessageBox::Warning, title, message,
                                    QMessageBox::Ok, parent);
            msgBox->setAttribute(Qt::WA_QuitOnClose, false);
            msgBox->setModal(true);
            msgBox->exec();
            delete msgBox.data();
        }
    } else {
        derror("%s : %s", title.toUtf8().constData(),
               message.toUtf8().constData());
    }
}

void deleteErrorDialog() {
    delete sErrorDialog;
    sErrorDialog = nullptr;
    sErrorDialogInitialized = false;
}
