// ============================================================
//  OCRtoODT — Centralized Logging Router (Implementation)
//  File: core/LogRouter.cpp
//
//  Responsibility:
//      • Provide a single logging entry point for the entire app
//      • Route logs to UI, file and/or console
//      • Enforce log level filtering (0..4)
//      • Provide safe log rotation
//      • Guarantee thread-safety
//
//  Thread-safety policy:
//      • All mutable internal state is protected by m_mutex
//      • File writes occur under mutex
//      • UI signal delivery ALWAYS happens outside mutex
//        using Qt::QueuedConnection
//      • No signal emission while holding m_mutex
//
//  Deadlock prevention:
//      UI logging is dispatched asynchronously after
//      releasing m_mutex to avoid re-entrancy lock cycles.
// ============================================================

#include "core/LogRouter.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QDebug>
#include <QMetaObject>

// ============================================================
// Singleton accessor
// ============================================================

LogRouter &LogRouter::instance()
{
    static LogRouter inst;
    return inst;
}

// ============================================================
// Constructor
// ============================================================

LogRouter::LogRouter(QObject *parent)
    : QObject(parent)
{
}

// ============================================================
// Configuration
// ============================================================

void LogRouter::configure(bool uiEnabled,
                          bool fileEnabled,
                          bool consoleEnabled,
                          bool profilerEnabled,
                          const QString &filePath)
{
    QMutexLocker lock(&m_mutex);

    m_uiEnabled       = uiEnabled;
    m_fileEnabled     = fileEnabled;
    m_consoleEnabled  = consoleEnabled;
    m_profilerEnabled = profilerEnabled;

    // Close file if file logging disabled
    if (!fileEnabled)
    {
        if (m_logFile.isOpen())
            m_logFile.close();
        return;
    }

    // Reopen file safely
    if (m_logFile.isOpen())
        m_logFile.close();

    QDir dir;
    const QString folder = QFileInfo(filePath).absolutePath();
    if (!folder.isEmpty() && !dir.exists(folder))
        dir.mkpath(folder);

    m_logFile.setFileName(filePath);

    // Open in append mode (never truncate on startup)
    if (m_logFile.open(QIODevice::Append | QIODevice::Text))
    {
        m_stream.setDevice(&m_logFile);

        m_stream << "\n# ============================================================\n";
        m_stream << "# OCRtoODT Log session start: "
                 << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")
                 << "\n";
        m_stream << "# ============================================================\n";
        m_stream.flush();
    }
}

// ============================================================
// Routing preset
// ============================================================

void LogRouter::setDestination(Destination dest)
{
    QMutexLocker lock(&m_mutex);
    m_destination = dest;
}

LogRouter::Destination LogRouter::destination() const
{
    QMutexLocker lock(&m_mutex);
    return m_destination;
}

// ============================================================
// Log level control
// ============================================================

void LogRouter::setLogLevel(int level)
{
    QMutexLocker lock(&m_mutex);

    if (level < 0) level = 0;
    if (level > 4) level = 4;

    m_logLevel = level;
}

bool LogRouter::shouldShow(Level msgLevel) const
{
    return m_logLevel >= static_cast<int>(msgLevel);
}

// ============================================================
// Internal Safe Logging Core
// ============================================================
//
// Pattern:
//   1) Lock
//   2) Filter by level
//   3) Capture routing flags
//   4) Write file/console under lock
//   5) Unlock
//   6) Emit UI signal via QueuedConnection
//
// This guarantees:
//   • No signal emission under lock
//   • No recursive deadlock via UI logging
// ============================================================

void LogRouter::logInternal(Level level,
                            const QString& prefix,
                            const QString& msg)
{
    QString line;
    bool ui = false;
    bool file = false;
    bool console = false;

    {
        QMutexLocker lock(&m_mutex);

        if (!shouldShow(level))
            return;

        line = prefix + msg;

        ui = m_uiEnabled;
        file = m_fileEnabled;
        console = m_consoleEnabled;

        if (file)
            writeToFile(line);

        if (console)
            writeToConsole(line);
    }

    // IMPORTANT:
    // UI signal delivery is outside the mutex.
    if (ui)
    {
        QMetaObject::invokeMethod(
            this,
            [this, line] { emit uiMessage(line); },
            Qt::QueuedConnection);
    }
}

// ============================================================
// Public Log Entry Points
// ============================================================

void LogRouter::error(const QString &msg)
{
    logInternal(Level::Error, "[ERROR] ", msg);
}

void LogRouter::warning(const QString &msg)
{
    logInternal(Level::Warning, "[WARN] ", msg);
}

void LogRouter::info(const QString &msg)
{
    logInternal(Level::Info, "[INFO] ", msg);
}

void LogRouter::perf(const QString &msg)
{
    bool allow = false;

    {
        QMutexLocker lock(&m_mutex);
        allow = m_profilerEnabled && shouldShow(Level::Verbose);
    }

    if (!allow)
        return;

    logInternal(Level::Verbose, "[PERF] ", msg);
}

void LogRouter::debug(const QString &msg)
{
#ifdef QT_DEBUG
    logInternal(Level::Verbose, "[DEBUG] ", msg);
#else
    Q_UNUSED(msg);
#endif
}

// ============================================================
// Console Output
// ============================================================
//
// Writes to Qt debug stream.
// Called only under mutex.
// ============================================================

void LogRouter::writeToConsole(const QString &msg)
{
    qDebug().noquote() << msg;
}

// ============================================================
// File Output
// ============================================================
//
// Must be called under mutex.
// Handles rotation automatically.
// ============================================================

void LogRouter::writeToFile(const QString &msg)
{
    if (!m_stream.device())
        return;

    const qint64 incomingBytes = msg.toUtf8().size() + 1;

    rotateIfNeeded_unlocked(incomingBytes);

    if (!m_stream.device())
        return;

    m_stream << msg << "\n";
    m_stream.flush();
}

// ============================================================
// Log Rotation
// ============================================================

void LogRouter::rotateIfNeeded_unlocked(qint64 incomingBytes)
{
    if (!m_fileEnabled)
        return;

    if (!m_logFile.isOpen())
        return;

    const qint64 currentSize = m_logFile.size();
    if (currentSize < 0)
        return;

    if ((currentSize + incomingBytes) <= m_maxLogSizeBytes)
        return;

    rotateLogs_unlocked();
}

void LogRouter::rotateLogs_unlocked()
{
    if (!m_logFile.isOpen())
        return;

    const QString basePath = m_logFile.fileName();
    if (basePath.trimmed().isEmpty())
        return;

    m_stream.flush();
    m_logFile.flush();
    m_logFile.close();

    const QString p1 = basePath + ".1";
    const QString p2 = basePath + ".2";
    const QString p3 = basePath + ".3";

    if (QFile::exists(p3)) QFile::remove(p3);
    if (QFile::exists(p2)) QFile::rename(p2, p3);
    if (QFile::exists(p1)) QFile::rename(p1, p2);
    if (QFile::exists(basePath)) QFile::rename(basePath, p1);

    if (m_logFile.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        m_stream.setDevice(&m_logFile);

        m_stream << "# ============================================================\n";
        m_stream << "# OCRtoODT Log rotated at: "
                 << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")
                 << "\n";
        m_stream << "# Previous file -> " << QFileInfo(p1).fileName() << "\n";
        m_stream << "# ============================================================\n";
        m_stream.flush();
    }
}

// ============================================================
// Max Log Size Configuration
// ============================================================
//
// Range allowed: 1..100 MB
// Thread-safe.
// ============================================================

void LogRouter::setMaxLogSizeMB(int megabytes)
{
    QMutexLocker lock(&m_mutex);

    if (megabytes < 1)
        megabytes = 1;

    if (megabytes > 100)
        megabytes = 100;

    m_maxLogSizeBytes =
        static_cast<qint64>(megabytes) * 1024 * 1024;
}
