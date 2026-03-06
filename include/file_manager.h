#pragma once

// ============================================================================
// FileManager — FFat-backed measurement file store
// ============================================================================
// Manages the /measurements directory on the FFat partition. Provides safe
// filename validation (path-traversal protection), sorted file listing,
// storage-capacity checks, and timestamped CSV saving.
//
// Hard limits (adjust if the partition grows):
//   MAX_FILES        = 200  — refuse new saves above this count
//   WARNING_THRESHOLD = 150 — warn the user but still allow saves
//   MAX_STORAGE_USAGE = 80% — refuse new saves above this partition fill level
// ============================================================================

#include <Arduino.h>
#include <vector>

// ----------------------------------------------------------------------------
// FileInfo — metadata returned by listFiles()
// ----------------------------------------------------------------------------
struct FileInfo {
    String        name;       ///< Bare filename (no directory prefix), e.g. "run_1234567.csv"
    size_t        size;       ///< File size in bytes
    unsigned long timestamp;  ///< Unix timestamp extracted from the filename suffix
};

// ----------------------------------------------------------------------------
// SaveResult — returned by saveMeasurement()
// ----------------------------------------------------------------------------
struct SaveResult {
    bool   success       = false; ///< true if the file was written successfully
    bool   warning       = false; ///< true if the file count is approaching WARNING_THRESHOLD
    bool   deletedOldest = false; ///< true if the oldest file was removed to make space
    String filename;              ///< Actual filename written (includes timestamp suffix)
    String message;               ///< Human-readable warning or error text
    int    fileCount     = 0;     ///< Number of files stored after this operation
};

class WebServer; // Forward declaration — avoids pulling in WebServer.h here

// ----------------------------------------------------------------------------
// StorageInfo — snapshot of FFat partition health
// ----------------------------------------------------------------------------
struct StorageInfo {
    size_t totalBytes  = 0;
    size_t usedBytes   = 0;
    size_t freeBytes   = 0;
    float  percentUsed = 0.0f; ///< usedBytes / totalBytes, range [0, 1]
    bool   isHealthy   = false; ///< false when percentUsed >= MAX_STORAGE_USAGE
};

// ============================================================================
// FileManager
// ============================================================================
class FileManager {
public:
    static const int   MAX_FILES         = 200;   ///< Absolute maximum number of stored CSVs
    static const int   WARNING_THRESHOLD  = 150;   ///< Warn the user when file count exceeds this
    static constexpr float MAX_STORAGE_USAGE = 0.80f; ///< Refuse new saves above 80% partition fill

    /** Mount FFat and create the /measurements directory if absent. Call once from setup(). */
    static bool init();

    /** Return the number of CSV files currently in /measurements. */
    static int countFiles();

    /**
     * @brief List all measurement files sorted by timestamp (oldest first).
     * @return Vector of FileInfo structs; empty if the directory cannot be opened.
     */
    static std::vector<FileInfo> listFiles();

    /**
     * @brief Write csvData to a new timestamped file.
     *
     * If the file count is at MAX_FILES, the oldest file is deleted first.
     *
     * @param basename  Base name without extension (e.g. "mosfet_data").
     * @param csvData   Full CSV content to write.
     * @return SaveResult describing the outcome and any warnings.
     */
    static SaveResult saveMeasurement(const String& basename, const String& csvData);

    /**
     * @brief Delete a single file from /measurements.
     * @param filename  Bare filename (validated against isValidFilename before deletion).
     * @return true on success.
     */
    static bool deleteFile(const String& filename);

    /** Delete the chronologically oldest file. Returns false if the directory is empty. */
    static bool deleteOldestFile();

    /** Read and return the full content of a stored file. Returns "" on error. */
    static String readFile(const String& filename);

    /** Stream a stored file directly to a sync WebServer response. */
    static void streamFileToWeb(WebServer& server, const String& filename);

    /**
     * @brief Validate a filename to prevent path-traversal attacks.
     *
     * Accepts only alphanumeric characters, underscores, hyphens, and dots.
     * The filename must end in ".csv" and must not contain "/" or "..".
     *
     * @return true if the filename is safe to use.
     */
    static bool isValidFilename(const String& filename);

    static const char* MEASUREMENTS_DIR; ///< "/measurements"

    /** Snapshot of FFat partition usage. */
    static StorageInfo getStorageInfo();

    /**
     * @brief Check whether there is enough space for a new measurement.
     * @return false if partition fill > 80% or free space < 10 KB.
     */
    static bool checkStorageAvailable();

private:
    /** Extract the Unix timestamp suffix from a filename like "run_1712345678.csv". */
    static unsigned long extractTimestamp(const String& filename);

    /** Build a human-readable warning string for the file-count warning level. */
    static String generateWarningMessage(int count);
};

