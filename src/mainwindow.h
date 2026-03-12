// ============================================================
//  OCRtoODT — Main Window
//  File: src/mainwindow.h
//
//  Responsibility:
//      Top-level UI shell of the application.
//
//      This class does NOT implement any processing logic.
//      It only:
//          • connects UI actions to controllers and dialogs
//          • coordinates interaction between UI components
//          • reacts to high-level lifecycle events
//
//  Architectural rules:
//      • NO OCR logic
//      • NO TSV parsing
//      • NO pipeline algorithms
//      • NO recognition internals
//      • Controllers own processing state
// ============================================================

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QVector>

#include "core/ProgressManager.h"

// ------------------------------------------------------------
// Forward declarations
// ------------------------------------------------------------
namespace Ui { class MainWindow; }

class InputProcessor;
class RecognitionProcessor;

namespace Core   { struct VirtualPage; }
namespace Input  { class PreviewController; }
namespace Step4  { class EditLinesController; }

// ============================================================
// MainWindow
// ============================================================
class MainWindow : public QMainWindow
{
    Q_OBJECT

protected:
    // --------------------------------------------------------
    // Application shutdown handler
    //
    // Ensures controlled shutdown when:
    //   • User closes the main window
    //   • Processing may still be running
    //
    // No business logic is executed here.
    // --------------------------------------------------------
    void closeEvent(QCloseEvent *event) override;

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

public slots:

    // ========================================================
    // Language handling
    // ========================================================

    // Re-translate all visible UI texts
    void retranslate();

    // ========================================================
    // Menu / Toolbar Actions
    // ========================================================

    void on_actionOpen_triggered();
    void on_actionClear_triggered();
    void on_actionRun_triggered();
    void on_actionStop_triggered();
    void on_actionExport_triggered();
    void on_actionSettings_triggered();
    void on_actionAbout_triggered();
    void on_actionHelp_triggered();

    // ========================================================
    // Processing lifecycle
    // ========================================================

    // Called when OCR pipeline finishes
    void onOcrCompleted(const QVector<Core::VirtualPage> &pages);

    // ========================================================
    // UI synchronization
    // ========================================================

    // Activate selected page in editor
    void onPageActivated(int globalIndex);

private:

    // --------------------------------------------------------
    // UI pointer (owned by Qt parent-child hierarchy)
    // --------------------------------------------------------
    Ui::MainWindow *ui = nullptr;

    // --------------------------------------------------------
    // High-level application state (UI-oriented)
    // --------------------------------------------------------
    enum class AppState
    {
        IdleEmpty,   // No files loaded
        Loaded,      // Files loaded, not running
        Running,     // OCR in progress
        Completed    // OCR completed
    };

    AppState computeState() const;
    void updateUiState();
    void attemptShutdown();

    // Indicates user requested shutdown while processing
    bool m_shutdownRequested = false;

    // --------------------------------------------------------
    // Controllers (logic owned outside MainWindow)
    // --------------------------------------------------------

    // Responsible for preview rendering only
    Input::PreviewController *m_previewController = nullptr;

    // Handles file input and preprocessing
    InputProcessor *m_inputProcessor = nullptr;

    // Controls OCR execution and owns recognized pages
    RecognitionProcessor *m_recognitionProcessor = nullptr;

    // Manages editable OCR text view (Step 4)
    Step4::EditLinesController *m_editLinesController = nullptr;

    // Global progress reporting
    Core::ProgressManager *m_progressManager = nullptr;
};

#endif // MAINWINDOW_H
