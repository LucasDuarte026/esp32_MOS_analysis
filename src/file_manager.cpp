#include "file_manager.h"
#include "mosfet_controller.h"
#include <FFat.h>
#include <algorithm>
#include <WebServer.h>

const char* FileManager::MEASUREMENTS_DIR = "/measurements";

bool FileManager::init() {
    if (!FFat.begin(true)) {  // true = format on first failure
        LOG_ERROR("FFat mount failed");
        return false;
    }
    LOG_INFO("FFat mounted successfully");
    
    // Create measurements directory if not exists
    if (!FFat.exists(MEASUREMENTS_DIR)) {
        FFat.mkdir(MEASUREMENTS_DIR);
        LOG_INFO("Created %s directory", MEASUREMENTS_DIR);
    }
    
    int fileCount = countFiles();
    LOG_INFO("Current measurements stored: %d", fileCount);
    
    if (fileCount >= WARNING_THRESHOLD) {
        LOG_WARN("File count (%d) approaching limit (%d)", fileCount, MAX_FILES);
    }
    
    return true;
}

StorageInfo FileManager::getStorageInfo() {
    StorageInfo info;
    info.totalBytes = FFat.totalBytes();
    info.usedBytes = FFat.usedBytes();
    info.freeBytes = FFat.freeBytes();
    
    if (info.totalBytes > 0) {
        info.percentUsed = (float)info.usedBytes / (float)info.totalBytes;
        info.isHealthy = (info.percentUsed < MAX_STORAGE_USAGE);
    }
    
    return info;
}

bool FileManager::checkStorageAvailable() {
    StorageInfo info = getStorageInfo();
    
    if (!info.isHealthy) {
        LOG_WARN("Storage limit exceeded: %.1f%% used (limit: %.0f%%)", 
                 info.percentUsed * 100.0f, MAX_STORAGE_USAGE * 100.0f);
        return false;
    }
    
    // Also check minimum free space (at least 10KB for a new measurement)
    if (info.freeBytes < 10240) {
        LOG_WARN("Insufficient free space: %u bytes", (unsigned)info.freeBytes);
        return false;
    }
    
    return true;
}

// Security: Validate filename to prevent path traversal attacks
bool FileManager::isValidFilename(const String& filename) {
    // Reject empty or too long filenames
    if (filename.length() == 0 || filename.length() > 100) {
        return false;
    }
    
    // Reject path traversal attempts
    if (filename.indexOf("..") != -1) return false;
    if (filename.indexOf("/") != -1) return false;
    if (filename.indexOf("\\") != -1) return false;
    
    // Only allow safe characters: alphanumeric, underscore, hyphen, dot
    for (size_t i = 0; i < filename.length(); i++) {
        char c = filename.charAt(i);
        if (!isalnum(c) && c != '_' && c != '-' && c != '.') {
            return false;
        }
    }
    
    // Must end with .csv
    if (!filename.endsWith(".csv")) {
        return false;
    }
    
    return true;
}

int FileManager::countFiles() {
    int count = 0;
    File dir = FFat.open(MEASUREMENTS_DIR);
    if (!dir) return 0;
    
    File file = dir.openNextFile();
    while (file) {
        if (!file.isDirectory()) count++;
        file.close();
        file = dir.openNextFile();
    }
    dir.close();
    return count;
}

std::vector<FileInfo> FileManager::listFiles() {
    std::vector<FileInfo> files;
    File dir = FFat.open(MEASUREMENTS_DIR);
    if (!dir) return files;
    
    File file = dir.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            FileInfo info;
            info.name = String(file.name());
            // Remove directory prefix if present
            if (info.name.startsWith("/measurements/")) {
                info.name = info.name.substring(14);
            }
            info.size = file.size();
            info.timestamp = extractTimestamp(info.name);
            files.push_back(info);
        }
        file.close();
        file = dir.openNextFile();
    }
    dir.close();
    
    // Sort by timestamp (oldest first)
    std::sort(files.begin(), files.end(), [](const FileInfo& a, const FileInfo& b) {
        return a.timestamp < b.timestamp;
    });
    
    return files;
}

unsigned long FileManager::extractTimestamp(const String& filename) {
    // Format: name_timestamp.csv
    int lastUnderscore = filename.lastIndexOf('_');
    int dotIndex = filename.lastIndexOf('.');
    
    if (lastUnderscore != -1 && dotIndex != -1) {
        String tsStr = filename.substring(lastUnderscore + 1, dotIndex);
        return tsStr.toInt();
    }
    return 0;
}

bool FileManager::deleteOldestFile() {
    auto files = listFiles();
    if (files.empty()) return false;
    
    String oldestPath = String(MEASUREMENTS_DIR) + "/" + files[0].name;
    bool success = FFat.remove(oldestPath.c_str());
    
    if (success) {
        LOG_INFO("Deleted oldest file: %s", files[0].name.c_str());
    } else {
        LOG_ERROR("Failed to delete: %s", files[0].name.c_str());
    }
    return success;
}

bool FileManager::deleteFile(const String& filename) {
    // Validate filename to prevent path traversal
    if (!isValidFilename(filename)) {
        LOG_ERROR("Invalid filename rejected: %s", filename.c_str());
        return false;
    }
    
    String fullPath = String(MEASUREMENTS_DIR) + "/" + filename;
    bool success = FFat.remove(fullPath.c_str());
    
    if (success) {
        LOG_INFO("Deleted file: %s", filename.c_str());
    } else {
        LOG_ERROR("Failed to delete: %s", filename.c_str());
    }
    return success;
}

String FileManager::readFile(const String& filename) {
    // Validate filename to prevent path traversal
    if (!isValidFilename(filename)) {
        LOG_ERROR("Invalid filename rejected: %s", filename.c_str());
        return "";
    }
    
    String fullPath = String(MEASUREMENTS_DIR) + "/" + filename;
    File file = FFat.open(fullPath.c_str(), "r");
    
    if (!file) {
        LOG_ERROR("Failed to open file: %s", filename.c_str());
        return "";
    }
    
    String content = file.readString();
    file.close();
    return content;
}

SaveResult FileManager::saveMeasurement(const String& basename, const String& csvData) {
    SaveResult result;
    result.fileCount = countFiles();
    
    // Generate filename with timestamp (seconds since boot)
    unsigned long timestamp = millis() / 1000;
    String filename = basename + "_" + String(timestamp) + ".csv";
    String fullPath = String(MEASUREMENTS_DIR) + "/" + filename;
    
    // Check if we need to delete old files
    if (result.fileCount >= MAX_FILES) {
        if (deleteOldestFile()) {
            result.deletedOldest = true;
            result.fileCount = countFiles();
            LOG_WARN("Deleted oldest file - limit reached");
        }
    }
    
    // Save file
    File file = FFat.open(fullPath.c_str(), "w");
    if (!file) {
        result.success = false;
        result.message = "Failed to create file";
        LOG_ERROR("Failed to create: %s", fullPath.c_str());
        return result;
    }
    
    file.print(csvData);
    file.close();
    
    result.success = true;
    result.filename = filename;
    result.fileCount = countFiles();
    
    // Generate warning message if needed
    if (result.fileCount >= WARNING_THRESHOLD) {
        result.warning = true;
        result.message = generateWarningMessage(result.fileCount);
    }
    
    LOG_INFO("Saved measurement: %s (%d files total)", filename.c_str(), result.fileCount);
    return result;
}

String FileManager::generateWarningMessage(int count) {
    if (count >= MAX_FILES) {
        return String("AVISO: Limite de 200 arquivos atingido! ") +
               "O arquivo mais antigo foi excluído. " +
               "Atualmente: " + String(count) + " arquivos.";
    } else {
        return String("AVISO: Serão permitidos apenas 200 medições no ESP32. ") +
               "Salve ou apague arquivos antigos. " +
               "Atualmente: " + String(count) + "/200 arquivos armazenados.";
    }
}

void FileManager::streamFileToWeb(WebServer& server, const String& filename) {
    if (!isValidFilename(filename)) {
        server.send(400, "text/plain", "Invalid filename");
        return;
    }
    
    String fullPath = String(MEASUREMENTS_DIR) + "/" + filename;
    File file = FFat.open(fullPath.c_str(), "r");
    
    if (!file) {
        server.send(404, "text/plain", "File not found");
        LOG_WARN("File not found: %s", fullPath.c_str());
        return;
    }
    
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
    if (server.streamFile(file, "text/csv") != file.size()) {
       LOG_ERROR("Sent less bytes than expected!");
    }
    file.close();
    LOG_INFO("File streamed: %s", filename.c_str());
}
