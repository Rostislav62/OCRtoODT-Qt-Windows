# Windows Release Procedure

This document describes the standard process for creating a Windows release of OCRtoODT.

The goal is to keep releases reproducible and consistent.

---

# 1. Update Version

Update the project version in:

CMakeLists.txt


Example:


set(APP_VERSION 1.0.0)


Commit the change.

---

# 2. Update Changelog

Edit:


CHANGELOG.md


Move changes from **Unreleased** to the new version section.

Example:

[1.0.1] - 2026-03-12

---

# 3. Build Release

Build the project in **Release mode**.

Example:


build/MSVC2022_vcpkg/Release


The release folder must contain:


OCRtoODT.exe
Qt runtime DLLs
Qt plugins
OCR libraries
tessdata/
translations/


Remove temporary runtime folders:


cache/


These files must **not** be included in the final release.

---

# 4. Create Release Package

Create the final distribution directory:


OCRtoODT-vX.Y.Z-windows-x64


Example structure:


OCRtoODT-v1.0.0-windows-x64/

OCRtoODT.exe
Qt6Core.dll
Qt6Gui.dll
Qt6Widgets.dll

platforms/
imageformats/
iconengines/
styles/

tessdata/
translations/


Then create archive:


OCRtoODT-vX.Y.Z-windows-x64.zip


---

# 5. Generate Checksums

Generate SHA256 checksum file.

PowerShell:


Get-FileHash OCRtoODT-vX.Y.Z-windows-x64.zip -Algorithm SHA256


Save result into:


SHA256SUMS


Example:


3F5C... OCRtoODT-v1.0.0-windows-x64.zip


---

# 6. Create Git Tag

Create a version tag:


git tag vX.Y.Z
git push origin vX.Y.Z


---

# 7. Publish GitHub Release

Create a release using the tag.

Attach files:


OCRtoODT-vX.Y.Z-windows-x64.zip
SHA256SUMS


Use the template:


docs/RELEASE_NOTES_TEMPLATE.md


---

# 8. Verify Release

After publishing:

Check:

- download works
- archive extracts correctly
- application starts
- OCR works

---

# Release Checklist

Before publishing a release confirm:
- version updated
- changelog updated
- release built in Release mode
- cache directory removed
- archive created
- checksums generated
- Git tag created
- GitHub release published
