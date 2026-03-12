// ============================================================
//  OCRtoODT — ThreadPoolGuard
//  File: src/core/ThreadPoolGuard.cpp
// ============================================================

#include "core/ThreadPoolGuard.h"

#include <QThreadPool>
#include <QtGlobal>
#include <QMetaObject>

#include "core/LogRouter.h"

void ThreadPoolGuard::apply(bool parallelEnabled,
                            const QString& numProcesses,
                            int cpuLogical)
{
    QThreadPool* pool = QThreadPool::globalInstance();

    const int previous = pool->maxThreadCount();

    int newLimit = 1;

    if (!parallelEnabled)
    {
        newLimit = 1;
    }
    else
    {
        if (numProcesses == "auto")
        {
            newLimit = qMax(1, cpuLogical);
        }
        else
        {
            bool ok = false;
            int parsed = numProcesses.toInt(&ok);

            if (ok && parsed > 0)
                newLimit = parsed;
            else
                newLimit = qMax(1, cpuLogical);
        }
    }

    pool->setMaxThreadCount(newLimit);

    const QString msg =
        QString("[ThreadPoolGuard] globalInstance maxThreadCount: %1 → %2")
            .arg(previous)
            .arg(newLimit);

    // Route logging to the LogRouter's thread (GUI thread) to avoid cross-thread issues.
    LogRouter *lr = &LogRouter::instance();
    QMetaObject::invokeMethod(lr, [msg]{
        LogRouter::instance().info(msg);
    }, Qt::QueuedConnection);
}
