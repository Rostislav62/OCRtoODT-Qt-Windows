// ============================================================
//  OCRtoODT — DOCX Exporter (STEP 5.6)
//  File: src/5_export/docx_export/DocxExporter.cpp
//
//  Responsibility:
//      Export Step5::DocumentModel to DOCX file.
//
//  Extended from MVP:
//      - paragraphs
//      - page breaks
//      - UTF-8
//      - layout-driven formatting
//      - structural normalization
//      - cross-platform ZIP via QuaZip
//
//  Architecture (DOCX):
//      - document.xml contains structure only
//      - styles.xml contains formatting
//      - [Content_Types].xml declares parts
//      - _rels/.rels binds package → word/document.xml
//
//  NOTE:
//      This implementation is fully cross-platform.
//      No external system zip tool is used.
// ============================================================

#include "5_export/docx_export/DocxExporter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextStream>
#include <QStringConverter>
#include <functional>


#include "5_export/ExportTextNormalizer.h"
#include "core/LogRouter.h"
#include <QuaZip-Qt6-1.5/quazip/quazip.h>
#include <QuaZip-Qt6-1.5/quazip/quazipfile.h>

namespace {

// ============================================================
// XML helper — escape reserved XML characters
// ============================================================
QString xmlEscape(const QString &s)
{
    QString out;
    out.reserve(s.size());

    for (QChar c : s)
    {
        switch (c.unicode())
        {
        case '&':  out += "&amp;";  break;
        case '<':  out += "&lt;";   break;
        case '>':  out += "&gt;";   break;
        case '"':  out += "&quot;"; break;
        case '\'': out += "&apos;"; break;
        default:   out += c;        break;
        }
    }
    return out;
}

// ============================================================
// Unit conversion helpers (Layout → DOCX)
// ============================================================

static int ptToTwips(double pt) { return static_cast<int>(pt * 20.0); }
static int mmToTwips(double mm) { return static_cast<int>(mm * 56.7); }
static int ptToHalfPoints(int pt) { return pt * 2; }

// ============================================================
// Alignment mapping (Qt → DOCX)
// ============================================================
static QString alignmentToDocx(Qt::Alignment a)
{
    if (a == Qt::AlignLeft)   return "left";
    if (a == Qt::AlignCenter) return "center";
    if (a == Qt::AlignRight)  return "right";
    return "both";
}

// ============================================================
// DOCX Style Factory
// Generates word/styles.xml based on layout model
// ============================================================
class DocxStyleFactory
{
public:
    static QString buildStylesXml(const OdtLayoutModel &layout)
    {
        const double linePt =
            static_cast<double>(layout.fontSizePt()) *
            static_cast<double>(layout.lineHeightPercent()) / 100.0;

        const int lineTwips = ptToTwips(linePt);

        QString xml;
        QTextStream out(&xml);

        out <<
            R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:styles xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
  <w:style w:type="paragraph" w:default="1" w:styleId="Normal">
    <w:name w:val="Normal"/>
    <w:pPr>
      <w:jc w:val=")" << alignmentToDocx(layout.alignment()) << R"("/>
      <w:ind w:firstLine=")" << mmToTwips(layout.firstLineIndentMM()) << R"("/>
      <w:spacing
          w:after=")" << ptToTwips(layout.paragraphSpacingAfterPt()) << R"("
          w:line=")"  << lineTwips << R"("
          w:lineRule="auto"/>
    </w:pPr>
    <w:rPr>
      <w:rFonts
          w:ascii=")" << layout.fontName() << R"("
          w:hAnsi=")" << layout.fontName() << R"("/>
      <w:sz w:val=")" << ptToHalfPoints(layout.fontSizePt()) << R"("/>
    </w:rPr>
  </w:style>
</w:styles>
)";
        return xml;
    }
};

// ============================================================
// Helper: Write UTF-8 file
// ============================================================
bool writeUtf8File(const QString &path, const QString &content)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    out << content;
    return true;
}

} // anonymous namespace

// ============================================================
// Public API
// ============================================================

namespace Export {

bool DocxExporter::writeDocxFile(
    const Step5::DocumentModel &document,
    const OdtLayoutModel       &layout,
    const QString              &outputPath)
{
    // --------------------------------------------------------
    // Validate document
    // --------------------------------------------------------
    if (document.isEmpty())
    {
        LogRouter::instance().warning(
            "[DocxExporter] Document is empty — nothing to export");
        return false;
    }

    // --------------------------------------------------------
    // Create temporary working directory
    // --------------------------------------------------------
    QTemporaryDir tempDir;
    if (!tempDir.isValid())
    {
        LogRouter::instance().warning(
            "[DocxExporter] Failed to create temporary directory");
        return false;
    }

    const QString root = tempDir.path();

    QDir().mkpath(root + "/word");
    QDir().mkpath(root + "/_rels");

    // --------------------------------------------------------
    // Write required DOCX XML parts
    // --------------------------------------------------------
    if (!writeUtf8File(root + "/[Content_Types].xml",
                       R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
  <Default Extension="xml"  ContentType="application/xml"/>
  <Override PartName="/word/document.xml"
            ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
  <Override PartName="/word/styles.xml"
            ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>
</Types>)") ||

        !writeUtf8File(root + "/_rels/.rels",
                       R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
  <Relationship Id="rId1"
                Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument"
                Target="word/document.xml"/>
</Relationships>)") ||

        !writeUtf8File(root + "/word/styles.xml",
                       DocxStyleFactory::buildStylesXml(layout)))
    {
        LogRouter::instance().warning(
            "[DocxExporter] Failed to write XML structure");
        return false;
    }

    // --------------------------------------------------------
    // Write document.xml
    // --------------------------------------------------------
    QString documentXml;
    QTextStream docOut(&documentXml);

    docOut <<
        R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">
<w:body>
)";

    Step5::DocumentModel normalized =
        ExportTextNormalizer::normalize(
            document,
            layout.maxEmptyLines());

    int lastPageIndex = -1;

    for (const auto &block : normalized.blocks)
    {
        if (layout.pageBreakEnabled() &&
            lastPageIndex != -1 &&
            block.pageIndex != lastPageIndex)
        {
            docOut <<
                R"(  <w:p>
    <w:r><w:br w:type="page"/></w:r>
  </w:p>
)";
        }

        lastPageIndex = block.pageIndex;

        docOut <<
            R"(  <w:p>
    <w:pPr><w:pStyle w:val="Normal"/></w:pPr>
    <w:r><w:t xml:space="preserve">)"
               << xmlEscape(block.text) <<
            R"(</w:t></w:r>
  </w:p>
)";
    }

    docOut <<
        R"(</w:body>
</w:document>)";

    if (!writeUtf8File(root + "/word/document.xml", documentXml))
    {
        LogRouter::instance().warning(
            "[DocxExporter] Failed to write document.xml");
        return false;
    }

    // --------------------------------------------------------
    // Create DOCX archive using QuaZip (cross-platform)
    // --------------------------------------------------------
    QuaZip zip(outputPath);

    if (!zip.open(QuaZip::mdCreate))
    {
        LogRouter::instance().warning(
            "[DocxExporter] Cannot create DOCX archive");
        return false;
    }

    // Recursive directory packaging
    std::function<bool(const QString&, const QString&)> addDir;
    addDir = [&](const QString &dirPath,
                 const QString &zipBase) -> bool
    {
        QDir dir(dirPath);
        QFileInfoList entries = dir.entryInfoList(
            QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

        for (const QFileInfo &fi : entries)
        {
            QString absPath = fi.absoluteFilePath();
            QString zipPath = zipBase + fi.fileName();

            if (fi.isDir())
            {
                if (!addDir(absPath, zipPath + "/"))
                    return false;
            }
            else
            {
                QFile inFile(absPath);
                if (!inFile.open(QIODevice::ReadOnly))
                    return false;

                QuaZipFile outFile(&zip);
                QuaZipNewInfo info(zipPath);

                if (!outFile.open(QIODevice::WriteOnly, info))
                    return false;

                outFile.write(inFile.readAll());
                outFile.close();
                inFile.close();
            }
        }
        return true;
    };

    if (!addDir(root, ""))
    {
        zip.close();
        LogRouter::instance().warning(
            "[DocxExporter] Failed while adding files to archive");
        return false;
    }

    zip.close();

    LogRouter::instance().info(
        QString("[DocxExporter] DOCX written: %1").arg(outputPath));

    return true;
}

} // namespace Export
