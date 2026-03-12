// ============================================================
//  OCRtoODT — OCR Pipeline Worker
//  File: src/2_ocr/OcrPipeLineWorker.h
//
//  Responsibility:
//      STEP 2 — OCR execution worker.
//
//  Policy:
//      • Worker must NOT read ConfigManager for language selection.
//      • Language is resolved at Controller level (RUN invariant).
//
// ============================================================

#ifndef OCR_PIPELINE_WORKER_H
#define OCR_PIPELINE_WORKER_H

#include <QObject>
#include <QVector>
#include <QFuture>
#include <atomic>

#include "1_preprocess/PageJob.h"
#include "core/VirtualPage.h"
#include "2_ocr/OcrPageWorker.h"

namespace Ocr {

class OcrPipelineWorker : public QObject
{
    Q_OBJECT

public:
    explicit OcrPipelineWorker(QObject *parent = nullptr);

    // --------------------------------------------------------
    // Start OCR pipeline (STEP 2)
    //
    // Contract:
    //   • languageString is RUN-level invariant (e.g. "eng+rus")
    //   • cancelFlag is owned by Controller (cooperative cancel)
    // --------------------------------------------------------
    void start(const QVector<Ocr::Preprocess::PageJob> &jobs,
               const QString& mode,
               bool debug,
               const QString& languageString,
               const std::atomic_bool *cancelFlag);

    void cancel();
    void waitForFinished();

    void setRunId(uint64_t id) { m_runId = id; }

signals:
    void ocrMessage(QString msg);
    void ocrFinished();
    void ocrCompleted(const QVector<Core::VirtualPage> &pages);
    void ocrProgress(int done, int total);

private:
    QVector<Ocr::Preprocess::PageJob> m_jobs;
    QString m_mode;
    bool m_debugMode = false;

    // RUN invariant parameters
    QString m_languageString;

    const std::atomic_bool *m_cancelFlag = nullptr;

    QFuture<OcrPageResult> m_future;

    uint64_t m_runId = 0;
};

} // namespace Ocr

#endif // OCR_PIPELINE_WORKER_H
