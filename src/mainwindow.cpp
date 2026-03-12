// ============================================================
//  OCRtoODT — Main Window
//  File: src/mainwindow.cpp
//
//  Responsibility:
//      UI shell implementation.
//
//      This file contains ONLY:
//          • UI wiring
//          • signal-slot connections
//          • dialog invocation
//
//      No OCR algorithms or processing logic are implemented here.
// ============================================================

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QCloseEvent>
#include <QMessageBox>

// ------------------------------------------------------------
// Controllers
// ------------------------------------------------------------

// Text editing controller
#include "4_edit_lines/EditLinesController.h"

// Data processors
#include "core/processors/InputProcessor.h"
#include "core/processors/RecognitionProcessor.h"
#include "core/ConfigManager.h"
#include "core/ThemeManager.h"
#include "core/RuntimePolicyManager.h"

// Preview rendering
#include "0_input/PreviewController.h"

// ------------------------------------------------------------
// Dialogs
// ------------------------------------------------------------

#include "dialogs/settingsdialog.h"
#include "dialogs/aboutdialog.h"
#include "dialogs/helpdialog.h"
#include "dialogs/export.h"
#include "dialogs/OcrCompletionDialog.h"

// ------------------------------------------------------------
// Core
// ------------------------------------------------------------

#include "core/LanguageManager.h"

// ============================================================
// Constructor / Destructor
// ============================================================

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // --------------------------------------------------------
    // OCR result list configuration (A4-like wrapping)
    // --------------------------------------------------------
    ui->listOcrText->setWordWrap(true);
    ui->listOcrText->setUniformItemSizes(false);

    // --------------------------------------------------------
    // Initial splitter proportions (Left / Center / Right)
    // --------------------------------------------------------
    {
        QList<int> sizes;

        const int total = width() > 0 ? width() : 1500;

        int left   = total * 0.18;
        int center = total * 0.40;
        int right  = total - left - center;

        sizes << left << center << right;
        ui->splitterMain->setSizes(sizes);

        ui->splitterMain->setStretchFactor(0, 0);
        ui->splitterMain->setStretchFactor(1, 1);
        ui->splitterMain->setStretchFactor(2, 2);
    }

    ui->panelLeft->setMinimumWidth(180);
    ui->panelCenter->setMinimumWidth(350);
    ui->panelRight->setMinimumWidth(450);

    // --------------------------------------------------------
    // Progress Manager
    // --------------------------------------------------------
    m_progressManager = new Core::ProgressManager(this);

    connect(m_progressManager,
            &Core::ProgressManager::progressChanged,
            this,
            [this](int value, int max, const QString &text)
            {
                ui->progressTotal->setMaximum(max);
                ui->progressTotal->setValue(value);
                ui->lblStatus->setText(text);
                ui->lblOcrState->setVisible(true);
            });

    // --------------------------------------------------------
    // Language change subscription
    // --------------------------------------------------------
    connect(LanguageManager::instance(),
            &LanguageManager::languageChanged,
            this,
            &MainWindow::retranslate);

    retranslate();

    // --------------------------------------------------------
    // Preview controller (visual only)
    // --------------------------------------------------------
    m_previewController =
        new Input::PreviewController(ui->viewPreview, this);

    connect(ui->btnZoomIn,  &QPushButton::clicked,
            m_previewController, &Input::PreviewController::zoomIn);

    connect(ui->btnZoomOut, &QPushButton::clicked,
            m_previewController, &Input::PreviewController::zoomOut);

    connect(ui->btnZoomFit, &QPushButton::clicked,
            m_previewController, &Input::PreviewController::zoomFit);

    connect(ui->btnZoom100, &QPushButton::clicked,
            m_previewController, &Input::PreviewController::zoom100);

    // --------------------------------------------------------
    // Editable text controller
    // --------------------------------------------------------
    m_editLinesController =
        new Step4::EditLinesController(this);

    m_editLinesController->attachUi(
        ui->listOcrText,
        m_previewController);

    // --------------------------------------------------------
    // Input processor
    // --------------------------------------------------------
    m_inputProcessor = new InputProcessor(this);

    m_inputProcessor->attachUi(
        ui->listFiles,
        m_previewController);

    connect(m_inputProcessor,
            &InputProcessor::pageActivated,
            this,
            &MainWindow::onPageActivated);

    // --------------------------------------------------------
    // OCR processor
    // --------------------------------------------------------
    m_recognitionProcessor = new RecognitionProcessor(this);
    m_recognitionProcessor->setProgressManager(m_progressManager);

    connect(m_recognitionProcessor,
            &RecognitionProcessor::processingStarted,
            this,
            &MainWindow::updateUiState);

    connect(m_recognitionProcessor,
            &RecognitionProcessor::processingFinished,
            this,
            [this]()
            {
                updateUiState();
                attemptShutdown();
            });

    connect(m_recognitionProcessor,
            &RecognitionProcessor::ocrCompleted,
            this,
            &MainWindow::onOcrCompleted);

    connect(m_recognitionProcessor,
            &RecognitionProcessor::runRejected,
            this,
            [this](const QString &reason)
            {
                QMessageBox::warning(
                    this,
                    tr("OCR cannot start"),
                    tr("No OCR language is selected.\n\n"
                       "To fix this:\n"
                       "1. Open Settings → Recognition\n"
                       "2. Select at least one language\n"
                       "3. Try again.")
                    );
            });

    connect(m_progressManager,
            &Core::ProgressManager::pipelineFinished,
            this,
            [this](bool ok, const QString &text)
            {
                ui->progressTotal->setValue(
                    ok ? ui->progressTotal->maximum() : 0);

                ui->lblStatus->setText(
                    text.isEmpty() ? tr("Ready") : text);

                ui->lblOcrState->setVisible(false);
                updateUiState();
            });

    connect(m_inputProcessor,
            &InputProcessor::inputStateChanged,
            this,
            &MainWindow::updateUiState);

    updateUiState();

}

MainWindow::~MainWindow()
{
    delete ui;
}

// ============================================================
// Language handling
// ============================================================

void MainWindow::retranslate()
{
    ui->retranslateUi(this);
}

// ============================================================
// Menu actions
// ============================================================

void MainWindow::on_actionOpen_triggered()
{
    if (m_inputProcessor)
        m_inputProcessor->run(this);
}

void MainWindow::on_actionClear_triggered()
{
    if (m_inputProcessor)
        m_inputProcessor->clearSession();

    if (m_recognitionProcessor)
        m_recognitionProcessor->clearSession();

    if (m_editLinesController)
        m_editLinesController->clear();

    if (m_previewController)
        m_previewController->reset();

    if (m_progressManager)
        m_progressManager->reset();

    ui->progressTotal->setValue(0);
    ui->progressTotal->setMaximum(100);
    ui->lblOcrState->setVisible(false);
    ui->lblStatus->setText(tr("Ready"));

    updateUiState();
}

void MainWindow::on_actionRun_triggered()
{
    if (!m_inputProcessor || !m_recognitionProcessor)
        return;

    if (m_recognitionProcessor->isProcessing())
        return;

    const auto jobs = m_inputProcessor->preprocessJobs();

    if (jobs.isEmpty())
    {
        ui->lblStatus->setText(tr("No input loaded"));
        updateUiState();
        return;
    }

    m_recognitionProcessor->setJobs(jobs);
    m_recognitionProcessor->run();

    updateUiState();
}

void MainWindow::on_actionStop_triggered()
{
    if (!m_recognitionProcessor)
        return;

    if (!m_recognitionProcessor->isProcessing())
    {
        updateUiState();
        return;
    }

    m_recognitionProcessor->cancel();
    ui->lblStatus->setText(tr("Stopping OCR..."));
    updateUiState();
}

void MainWindow::on_actionExport_triggered()
{
    if (!m_recognitionProcessor)
        return;

    auto &pages = m_recognitionProcessor->pagesMutable();

    if (pages.isEmpty())
    {
        ui->lblStatus->setText(tr("Nothing to export"));
        updateUiState();
        return;
    }

    ExportDialog dlg(&pages, this);
    dlg.exec();

    updateUiState();
}

void MainWindow::on_actionSettings_triggered()
{
    SettingsDialog dlg(this);

    if (dlg.exec() != QDialog::Accepted)
        return;

    ConfigManager::instance().reload();
    ThemeManager::instance()->applyAllFromConfig();
    RuntimePolicyManager::reapply();
}

void MainWindow::on_actionAbout_triggered()
{
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::on_actionHelp_triggered()
{
    HelpDialog dlg(this);
    dlg.exec();
}

// ============================================================
// OCR lifecycle
// ============================================================

void MainWindow::onOcrCompleted(const QVector<Core::VirtualPage> &pages)
{
    Q_UNUSED(pages);

    updateUiState();

    if (!m_editLinesController || !m_recognitionProcessor)
        return;

    auto &ownedPages = m_recognitionProcessor->pagesMutable();
    if (ownedPages.isEmpty())
        return;

    m_editLinesController->setActivePage(&ownedPages[0]);

    ConfigManager &cfg = ConfigManager::instance();

    if (cfg.get("ui.notify_on_finish", true).toBool())
    {
        OcrCompletionDialog dlg(this);
        dlg.exec();
    }
}

// ============================================================
// UI synchronization
// ============================================================

void MainWindow::onPageActivated(int globalIndex)
{
    if (!m_editLinesController || !m_recognitionProcessor)
        return;

    auto &pages = m_recognitionProcessor->pagesMutable();

    if (globalIndex < 0 || globalIndex >= pages.size())
        return;

    m_editLinesController->setActivePage(&pages[globalIndex]);
}

// ============================================================
// UI state machine
// ============================================================

MainWindow::AppState MainWindow::computeState() const
{
    const bool isRunning =
        m_recognitionProcessor &&
        m_recognitionProcessor->isProcessing();

    const bool hasInput =
        (ui->listFiles &&
         ui->listFiles->model() &&
         ui->listFiles->model()->rowCount() > 0);

    const bool hasResults =
        m_recognitionProcessor &&
        !m_recognitionProcessor->pagesMutable().isEmpty();

    if (isRunning)
        return AppState::Running;

    if (!hasInput)
        return AppState::IdleEmpty;

    if (hasInput && !hasResults)
        return AppState::Loaded;

    return AppState::Completed;
}

void MainWindow::updateUiState()
{
    switch (computeState())
    {
    case AppState::IdleEmpty:
        ui->actionRun->setEnabled(false);
        ui->actionClear->setEnabled(false);
        ui->actionExport->setEnabled(false);
        ui->actionStop->setEnabled(false);
        break;

    case AppState::Loaded:
        ui->actionRun->setEnabled(true);
        ui->actionClear->setEnabled(true);
        ui->actionExport->setEnabled(false);
        ui->actionStop->setEnabled(false);
        break;

    case AppState::Running:
        ui->actionRun->setEnabled(false);
        ui->actionClear->setEnabled(false);
        ui->actionExport->setEnabled(false);
        ui->actionStop->setEnabled(true);
        break;

    case AppState::Completed:
        ui->actionRun->setEnabled(true);
        ui->actionClear->setEnabled(true);
        ui->actionExport->setEnabled(true);
        ui->actionStop->setEnabled(false);
        break;
    }
}

// ============================================================
// Safe shutdown
// ============================================================

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_shutdownRequested)
    {
        event->accept();
        return;
    }

    if (!m_recognitionProcessor ||
        !m_recognitionProcessor->isProcessing())
    {
        event->accept();
        return;
    }

    m_shutdownRequested = true;

    ui->lblStatus->setText(tr("Stopping OCR..."));
    ui->actionStop->setEnabled(false);

    m_recognitionProcessor->cancel();
    event->ignore();
}

void MainWindow::attemptShutdown()
{
    if (!m_shutdownRequested)
        return;

    m_shutdownRequested = false;
    close();
}
