#pragma once

#include "ui/LibraryController.h"

#include <QJSEngine>
#include <QQmlEngine>
#include <QtQml/qqmlregistration.h>

namespace KalibreBox
{

/**
 * Exposes the LibraryController instance created in main() to QML as the
 * `LibraryController` singleton. Kept separate from the controller so the core
 * library stays free of QML registration.
 */
struct LibraryControllerQml {
    Q_GADGET
    QML_FOREIGN(KalibreBox::LibraryController)
    QML_NAMED_ELEMENT(LibraryController)
    QML_SINGLETON

public:
    inline static LibraryController *instance = nullptr;

    static LibraryController *create(QQmlEngine *, QJSEngine *)
    {
        QJSEngine::setObjectOwnership(instance, QJSEngine::CppOwnership);
        return instance;
    }
};

// Lets QML tooling resolve the type of LibraryController::books.
struct BookListModelQml {
    Q_GADGET
    QML_FOREIGN(KalibreBox::BookListModel)
    QML_NAMED_ELEMENT(BookListModel)
    QML_UNCREATABLE("BookListModel is provided by LibraryController")
};

}
