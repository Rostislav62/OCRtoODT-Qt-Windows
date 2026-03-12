// ============================================================
//  OCRtoODT — OCR Pipeline Controller
//  File: src/2_ocr/OcrPipeLineController.cpp
// ============================================================

#include "2_ocr/OcrPipeLineController.h"

#include <QThread>
#include <QMetaObject>

#include "core/ConfigManager.h"
#include "core/LogRouter.h"
#include "core/RuntimePolicyManager.h"
#include "core/ocr/OcrLanguageManager.h"

using namespace Ocr;

OcrPipelineController* OcrPipelineController::s_instance = nullptr;

// ============================================================
// Constructor
// ============================================================
OcrPipelineController::OcrPipelineController(QObject *parent)
    : QObject(parent)
{
    Q_ASSERT(!s_instance);
    s_instance = this;

    m_worker = new OcrPipelineWorker();
    m_worker->moveToThread(QThread::currentThread());

    connect(m_worker, &OcrPipelineWorker::ocrFinished,
            this, &OcrPipelineController::notifyIdleOnce);

    connect(m_worker, &OcrPipelineWorker::ocrCompleted,
            this, &OcrPipelineController::ocrCompleted);

    connect(m_worker, &OcrPipelineWorker::ocrProgress,
            this, &OcrPipelineController::ocrProgress);

    connect(m_worker, &OcrPipelineWorker::ocrMessage,
            this, &OcrPipelineController::ocrMessage);
}

// ============================================================
// Destructor
// ============================================================
OcrPipelineController::~OcrPipelineController()
{
    shutdownAndWait();
    s_instance = nullptr;
}

// ============================================================
// Singleton
// ============================================================
OcrPipelineController* OcrPipelineController::instance()
{
    return s_instance;
}

// ============================================================
// Start OCR pipeline
//
// Contract:
//   • Called only when idle
//   • Language resolved ONCE per RUN
// ============================================================
void OcrPipelineController::start(
    const QVector<Ocr::Preprocess::PageJob> &jobs)
{
    if (m_isRunning.load())
    {
        LogRouter::instance().warning(
            "[OcrPipelineController] start() called while already running.");
        return;
    }

    if (jobs.isEmpty())
    {
        LogRouter::instance().warning(
            "[OcrPipelineController] start() ignored: jobs empty.");
        return;
    }

    // Reset idle barrier
    m_idleNotified.store(false);

    // Re-evaluate runtime policy BEFORE RUN
    RuntimePolicyManager::requestReapply(false);

    ConfigManager &cfg = ConfigManager::instance();

    const QString mode =
        cfg.get("general.mode", "ram_only").toString();

    const bool debugMode =
        cfg.get("general.debug_mode", false).toBool();

    // --------------------------------------------------------
    // Resolve language string ONCE per RUN
    // --------------------------------------------------------
    const QString languageString =
        OcrLanguageManager::instance()
            .buildTesseractLanguageString();

    LogRouter::instance().info(
        QString("[OcrPipelineController] Starting OCR (jobs=%1 mode=%2 debug=%3 lang=%4)")
            .arg(jobs.size())
            .arg(mode)
            .arg(debugMode)
            .arg(languageString));

    m_cancelRequested.store(false);
    m_isRunning.store(true);

    // --------------------------------------------------------
    // Asynchronous start in worker
    // --------------------------------------------------------
    QMetaObject::invokeMethod(
        m_worker,
        [this, jobs, mode, debugMode, languageString]()
        {
            m_worker->setRunId(m_runId);

            m_worker->start(
                jobs,
                mode,
                debugMode,
                languageString,
                &m_cancelRequested);
        },
        Qt::QueuedConnection
        );
}

// ============================================================
// Cancel
// ============================================================
void OcrPipelineController::cancel()
{
    LogRouter::instance().info(
        QString("[STATE] run=%1 CTRL event=CANCEL_ENTER isRunning=%2")
            .arg(m_runId)
            .arg(m_isRunning.load()));

    // Даже если уже не running — мы обязаны гарантировать idle
    if (!m_isRunning.load())
    {
        notifyIdleOnce();
        emit ocrFinished();
        return;
    }

    LogRouter::instance().warning(
        "[OcrPipelineController] Cancel requested.");

    m_cancelRequested.store(true);

    shutdownAndWait();

    // Гарантируем сигнал завершения
    emit ocrFinished();
}

// ============================================================
// Running state
// ============================================================
bool OcrPipelineController::isRunning() const
{
    return m_isRunning.load();
}

// ============================================================
// Safe shutdown
// ============================================================
void OcrPipelineController::shutdownAndWait()
{
    LogRouter::instance().info(
        QString("[STATE] run=%1 CTRL event=SHUTDOWN_BEGIN")
            .arg(m_runId));

    if (!m_worker)
        return;

    // Ждём завершения future (cooperative cancel)
    m_worker->waitForFinished();

    m_isRunning.store(false);

    // EXACTLY-ONCE idle barrier
    notifyIdleOnce();

    LogRouter::instance().info(
        QString("[STATE] run=%1 CTRL event=SHUTDOWN_DONE")
            .arg(m_runId));

    // Финальный сигнал о завершении этапа
    emit ocrFinished();
}

// ============================================================
// EXACTLY-ONCE idle notification
// ============================================================
void OcrPipelineController::notifyIdleOnce()
{
    if (m_idleNotified.exchange(true))
        return;

    m_isRunning.store(false);

    LogRouter::instance().info(
        "[OcrPipelineController] OCR finished -> pipeline idle.");

    RuntimePolicyManager::requestReapply(false);
}
