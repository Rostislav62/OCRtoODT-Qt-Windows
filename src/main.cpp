// ============================================================
//  OCRtoODT — Application Entry Point
//  File: main.cpp
//
//  Responsibility:
//      • Create QApplication
//      • Resolve production-safe config.yaml path
//      • Load configuration via ConfigManager
//      • Configure logging (LogRouter)
//      • Detect system hardware (CPU / RAM)
//      • Initialize runtime policy (RuntimePolicyManager)
//      • Apply theme and language
//      • Create and show MainWindow
//
//  Policy:
//      • main() MUST NOT overwrite user config.yaml
//      • Runtime auto-decisions are applied in-memory only
//      • No debug watchdog / heartbeat code in production
// ============================================================

#include "mainwindow.h"

#include "core/ConfigManager.h"
#include "core/ThemeManager.h"
#include "core/LanguageManager.h"
#include "core/LogRouter.h"
#include "core/CrashHandler.h"
#include "core/RuntimePolicyManager.h"
#include "core/ocr/OcrLanguageManager.h"

#include "systeminfo/systeminfo.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFont>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QFile>
#include <QTextStream>
#include <QStringConverter>

// ============================================================
// Resolve production-safe config.yaml path
//
// Strategy:
//   1) Use user config directory (QStandardPaths)
//   2) If not exists → seed from executable dir or CWD
//   3) If still not exists → create minimal config
// ============================================================
static QString resolvedConfigFilePath()
{
    const QString configDir =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);

    QDir().mkpath(configDir);

    const QString userCfgPath = QDir(configDir).filePath("config.yaml");
    if (QFileInfo::exists(userCfgPath))
        return userCfgPath;

    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString exeCfg = QDir(exeDir).filePath("config.yaml");
    const QString cwdCfg = QDir(QDir::currentPath()).filePath("config.yaml");

    QString seedPath;
    if (QFileInfo::exists(exeCfg))
        seedPath = exeCfg;
    else if (QFileInfo::exists(cwdCfg))
        seedPath = cwdCfg;

    if (!seedPath.isEmpty())
    {
        if (QFile::copy(seedPath, userCfgPath))
            return userCfgPath;
    }

    // Create minimal config if nothing exists
    QFile f(userCfgPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream ts(&f);
        ts.setEncoding(QStringConverter::Utf8);

        ts << "# OCRtoODT auto-generated config\n";
        ts << "config:\n  version: 1\n\n";
        ts << "logging:\n";
        ts << "  enabled: true\n";
        ts << "  level: 3\n";
        ts << "  file_output: false\n";
        ts << "  gui_output: true\n";
        ts << "  console_output: true\n";
        ts << "  file_path: log/ocrtoodt.log\n\n";
        ts << "ui:\n";
        ts << "  theme_mode: dark\n";
        ts << "  app_font_size: 11\n";
        ts << "  notify_on_finish: true\n";
        ts << "  play_sound_on_finish: true\n";
        ts << "  sound_volume: 70\n";
        ts << "  sound_path: sounds/done.wav\n\n";
        ts << "general:\n";
        ts << "  parallel_enabled: true\n";
        ts << "  num_processes: auto\n";
        ts << "  mode: auto\n";
    }

    return userCfgPath;
}

// ============================================================
// Main
// ============================================================
int main(int argc, char *argv[])
{
    // --------------------------------------------------------
    // Create Qt application object
    // --------------------------------------------------------
    QApplication app(argc, argv);

    // --------------------------------------------------------
    // Application identity (affects config location)
    // --------------------------------------------------------
    QCoreApplication::setOrganizationName("OCRtoODT");
    QCoreApplication::setApplicationName("OCRtoODT");

    // --------------------------------------------------------
    // Production mode configuration
    // --------------------------------------------------------
    ConfigManager::instance().setMode(ConfigManager::Mode::Production);

    // --------------------------------------------------------
    // Safe fallback application font
    // Final font will be applied by ThemeManager
    // --------------------------------------------------------
    QFont defaultFont("DejaVu Sans", 11);
    defaultFont.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(defaultFont);

    // --------------------------------------------------------
    // Resolve and load config.yaml
    // --------------------------------------------------------
    const QString cfgPath = resolvedConfigFilePath();
    ConfigManager &cfg = ConfigManager::instance();

    // Minimal console logger for early boot
    LogRouter::instance().configure(false, false, true, false, "");
    LogRouter::instance().setLogLevel(4);


    cfg.load(cfgPath);

    if (cfg.validationFailed())
        return EXIT_FAILURE;

    // --------------------------------------------------------
    // Configure logging system
    // --------------------------------------------------------
    {
        const bool loggingEnabled =
            cfg.get("logging.enabled", true).toBool();

        const int logLevel =
            cfg.get("logging.level", 3).toInt();

        const bool fileOutput =
            cfg.get("logging.file_output", false).toBool();

        const bool guiOutput =
            cfg.get("logging.gui_output", true).toBool();

        const bool consoleOutput =
            cfg.get("logging.console_output", true).toBool();

        const QString logFilePath =
            cfg.get("logging.file_path", "log/ocrtoodt.log").toString();

        const bool profilerEnabled = (logLevel >= 4);

        CrashHandler::install();

        LogRouter &log = LogRouter::instance();

        log.configure(
            guiOutput && loggingEnabled,
            fileOutput && loggingEnabled,
            consoleOutput && loggingEnabled,
            profilerEnabled,
            logFilePath
            );

        log.setLogLevel(logLevel);

        const int maxSizeMB =
            cfg.get("logging.max_file_size_mb", 5).toInt();

        log.setMaxLogSizeMB(maxSizeMB);
    }

    LogRouter &log = LogRouter::instance();
    log.info("OCRtoODT starting...");
    log.info(QString("Config loaded from: %1").arg(cfgPath));

    // --------------------------------------------------------
    // Detect hardware
    // --------------------------------------------------------
    const int cpuLogical  = si_cpu_logical_threads();
    const long long ramTotalMB = si_total_ram_mb();
    const long long ramFreeMB  = si_free_ram_mb();

    log.info("System hardware detected:");
    log.info(QString("  CPU brand          : %1").arg(si_cpu_brand_string()));
    log.info(QString("  CPU logical threads: %1").arg(cpuLogical));
    log.info(QString("  RAM total          : %1 MB").arg(ramTotalMB));
    log.info(QString("  RAM free/available : %1 MB").arg(ramFreeMB));

    // --------------------------------------------------------
    // Initialize runtime policy
    // --------------------------------------------------------
    RuntimePolicyManager::initialize(cpuLogical);

    // --------------------------------------------------------
    // Apply theme and language
    // --------------------------------------------------------
    ThemeManager::instance()->applyAllFromConfig();
    LanguageManager::instance()->initialize();

    // --------------------------------------------------------
    // Load OCR language metadata (GoldenDict-style foundation)
    // --------------------------------------------------------
    OcrLanguageManager::instance().loadMetadata();

    // Тесты которые нужно булет потом убрать.
    auto meta = OcrLanguageManager::instance().metaFor("eng");
    LogRouter::instance().info(QString("TEST LANG META: %1").arg(meta.nameEn));


    auto installed = OcrLanguageManager::instance().scanInstalledLanguages();
    LogRouter::instance().info(
        QString("TEST INSTALLED: %1").arg(installed.join(",")));

    auto active = OcrLanguageManager::instance().activeLanguages();
    LogRouter::instance().info(
        QString("TEST ACTIVE: %1").arg(active.join(",")));

    auto finalLang =
        OcrLanguageManager::instance().buildTesseractLanguageString();
    LogRouter::instance().info(
        QString("TEST FINAL: %1").arg(finalLang));

     // Выше Тесты которые нужно булет потом убрать.

    // --------------------------------------------------------
    // Create and show main window
    // --------------------------------------------------------
    MainWindow w;
    w.show();

    log.info("Main window shown. Entering event loop.");

    // --------------------------------------------------------
    // Start Qt event loop
    // --------------------------------------------------------
    return app.exec();
}
