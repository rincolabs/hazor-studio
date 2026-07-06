#pragma once

#include <QString>

// Single source of truth for where the application stores its file-based user
// data (presets, agent configs, swatches). Resolves to the standard Qt
// per-application data location, i.e. "<data>/Rincolabs/Hazor Studio" once the
// organization/application names are set in main().
namespace AppPaths {

// Application data directory, created on first access.
QString dataDir();

// Absolute path to a file directly inside dataDir().
QString dataFile(const QString& fileName);

// Absolute path to a subdirectory inside dataDir(), created if missing.
QString subDir(const QString& name);

// Per-application cache directory (QStandardPaths::CacheLocation), created on
// first access. Distinct from dataDir(): cache is disposable and may be cleared
// by the app or the OS. Installed models live in dataDir(), never here.
QString cacheDir();

// Absolute path to a subdirectory inside cacheDir(), created if missing.
QString cacheSubDir(const QString& name);

// AI cache root, "<cache>/ai", holding embeddings/previews/temp/downloads.
// Never used to store installed models.
QString aiCacheDir();

// Root of the vendored third-party tree ("3rdparty"), holding everything
// distributed with the app: bundled ONNX models, the ONNX Runtime, and the
// RealESRGAN backend. Resolved relative to the executable, covering: next to
// the binary, a Unix <prefix>/bin layout, the AppImage AppDir root
// (usr/bin -> ../../3rdparty), a macOS bundle, and the source tree (dev).
// Returns the canonical path even when it does not yet exist; never created here.
QString thirdPartyDir();

// Read-only directory holding the models distributed with the app
// ("3rdparty/onnx/models"). NOT under the user data dir, so the user's
// downloaded/custom models never mix with the trusted bundled set.
QString bundledModelsDir();

} // namespace AppPaths
