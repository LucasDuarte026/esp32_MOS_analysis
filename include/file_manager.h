#pragma once
#include <Arduino.h>
#include <vector>

// File information structure
struct FileInfo {
    String name;
    size_t size;
    unsigned long timestamp;
};

// Save result structure
struct SaveResult {
    bool success = false;
    bool warning = false;
    bool deletedOldest = false;
    String filename;
    String message;
    int fileCount = 0;
};

class WebServer; // Forward declaration

// File Manager class
class FileManager {
public:
    static const int MAX_FILES = 200;
    static const int WARNING_THRESHOLD = 150;
    
    static bool init();
    static int countFiles();
    static std::vector<FileInfo> listFiles();
    static SaveResult saveMeasurement(const String& basename, const String& csvData);
    static bool deleteFile(const String& filename);
    static bool deleteOldestFile();
    static String readFile(const String& filename);
    static void streamFileToWeb(WebServer& server, const String& filename);
    
    // Security: Validate filename to prevent path traversal
    static bool isValidFilename(const String& filename);
    
    static const char* MEASUREMENTS_DIR;

private:
    static unsigned long extractTimestamp(const String& filename);
    static String generateWarningMessage(int count);
};
