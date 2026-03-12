// ============================================================
//  OCRtoODT — Centralized Logging Router (Header)
//  File: core/LogRouter.h
//
//  Responsibility:
//      • Provide unified logging entry point for entire app
//      • Route messages to UI / file / console
//      • Enforce canonical log level model (0..4)
//      • Perform size-based log rotation
//      • Guarantee thread-safe operation
//
//  Canonical logging.level model:
//
//      0 — Off
//      1 — Errors only
//      2 — Warnings + Errors
//      3 — Info + Warnings + Errors
//      4 — Verbose (Debug + Performance)
//
//  Thread-Safety Model:
//      • All mutable internal state protected by m_mutex
//      • File writes occur under mutex
//      • UI signal emission ALWAYS outside mutex
//      • No re-entrant locking allowed
// ============================================================

#ifndef OCRTOODT_LOGROUTER_H
#define OCRTOODT_LOGROUTER_H

#include <QObject>
#include <QMutex>
#include <QFile>
#include <QTextStream>

class LogRouter : public QObject
{
    Q_OBJECT

public:
    // =========================================================
    // Log destination presets
    //
    // NOTE:
    //   Currently informational only.
    //   Real routing controlled by configure().
    // =========================================================
    enum class Destination {
        None,
        UiOnly,
        FileOnly,
        UiAndFile,
        ConsoleOnly,
        UiFileConsole
    };
    Q_ENUM(Destination)

    // ---------------------------------------------------------
    // Singleton accessor
    //
    // Returns global LogRouter instance.
    // Lifetime: static storage duration.
    // ---------------------------------------------------------
    static LogRouter& instance();

    // ---------------------------------------------------------
    // Configure routing behavior
    //
    // Parameters:
    //   uiEnabled       — enable emitting UI signal
    //   fileEnabled     — enable file logging
    //   consoleEnabled  — enable console logging
    //   profilerEnabled — allow PERF logs
    //   filePath        — path to log file
    //
    // Thread-safe.
    // ---------------------------------------------------------
    void configure(bool uiEnabled,
                   bool fileEnabled,
                   bool consoleEnabled,
                   bool profilerEnabled,
                   const QString& filePath);

    // ---------------------------------------------------------
    // Routing preset control
    // ---------------------------------------------------------
    void setDestination(Destination dest);
    Destination destination() const;

    // ---------------------------------------------------------
    // Set canonical log level (0..4)
    // ---------------------------------------------------------
    void setLogLevel(int level);

    // =========================================================
    // Public Log Entry Points
    //
    // All application modules must use these methods.
    // =========================================================
    void error(const QString& msg);
    void warning(const QString& msg);
    void info(const QString& msg);
    void perf(const QString& msg);
    void debug(const QString& msg);

    // ---------------------------------------------------------
    // Configure max log file size (MB)
    //
    // Allowed range: 1..100 MB
    // Thread-safe.
    // ---------------------------------------------------------
    void setMaxLogSizeMB(int megabytes);

signals:
    // ---------------------------------------------------------
    // UI log signal
    //
    // Emitted only when UI logging enabled.
    // Delivered asynchronously (QueuedConnection).
    // ---------------------------------------------------------
    void uiMessage(const QString& msg);

private:
    // ---------------------------------------------------------
    // Constructor (private for singleton pattern)
    // ---------------------------------------------------------
    explicit LogRouter(QObject* parent = nullptr);
    Q_DISABLE_COPY(LogRouter)

    // =========================================================
    // Internal Log Level Enum
    //
    // Matches canonical logging.level model.
    // =========================================================
    enum class Level {
        Error   = 1,
        Warning = 2,
        Info    = 3,
        Verbose = 4   // Debug + Performance
    };

    // ---------------------------------------------------------
    // Internal filtering helper
    //
    // Returns true if given level should be logged.
    // ---------------------------------------------------------
    bool shouldShow(Level msgLevel) const;

    // ---------------------------------------------------------
    // Core internal logging implementation
    //
    // Pattern:
    //   • Capture routing flags under mutex
    //   • Write file/console under mutex
    //   • Emit UI signal outside mutex
    //
    // Prevents GUI deadlocks caused by re-entrancy.
    // ---------------------------------------------------------
    void logInternal(Level level,
                     const QString& prefix,
                     const QString& msg);

    // =========================================================
    // File Output & Rotation (internal, mutex required)
    // =========================================================

    // Writes formatted line to file.
    // Must be called under m_mutex.
    void writeToFile(const QString& msg);

    // Checks if rotation required before writing.
    // Must be called under m_mutex.
    void rotateIfNeeded_unlocked(qint64 incomingBytes);

    // Performs log rotation.
    // Must be called under m_mutex.
    void rotateLogs_unlocked();

    // Writes line to console (Qt debug stream).
    // Called under m_mutex.
    void writeToConsole(const QString& msg);

private:
    // =========================================================
    // Internal State (Protected by m_mutex)
    // =========================================================

    mutable QMutex m_mutex;   // Protects all mutable state below

    QFile       m_logFile;    // Active log file handle
    QTextStream m_stream;     // Stream bound to m_logFile

    bool m_uiEnabled       = true;
    bool m_fileEnabled     = false;
    bool m_consoleEnabled  = false;
    bool m_profilerEnabled = true;

    Destination m_destination = Destination::UiOnly;

    int m_logLevel = 3; // Default = Info

    // ---------------------------------------------------------
    // Log rotation threshold (bytes)
    //
    // Default = 5 MB
    // Runtime configurable.
    // ---------------------------------------------------------
    qint64 m_maxLogSizeBytes = 5 * 1024 * 1024;
};

#endif // OCRTOODT_LOGROUTER_H
