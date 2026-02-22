#pragma once

#include <Arduino.h>
#include <vector>
#include <string>
#include <ESP_Mail_Client.h>

struct EmailStatus {
    enum State {
        IDLE,
        CONNECTING,
        SENDING_BODY,
        SENDING_ATTACHMENT,
        SUCCESS,
        FAILED
    };

    State state = IDLE;
    String currentFile = "";
    int progress = 0;      // 0-100%
    String message = "";   // Status message or error description
    uint32_t timestamp = 0;
};

struct EmailRequest {
    String smtpHost;
    int smtpPort;
    String senderEmail;
    String senderName;
    String senderPassword;
    String to;          // Comma separated list for To
    String cc;          // Comma separated list for Cc
    String subject;
    String body;
    std::vector<String> files;
};

class EmailManager {
public:
    static EmailManager& getInstance();

    // Configuration
    void begin();
    
    // Main Action
    bool sendEmailAsync(const EmailRequest& req);

    // Status Access
    EmailStatus getStatus() const;
    bool isBusy() const;

    // Helper to update status safely (Must be public for callback)
    void updateStatus(EmailStatus::State state, int progress, const String& msg, const String& file = "");

    // Task Function (Internal use but must be public/static for FreeRTOS)
    static void emailTask(void* parameter);

private:
    EmailManager();
    ~EmailManager() = default;
    EmailManager(const EmailManager&) = delete;
    EmailManager& operator=(const EmailManager&) = delete;

    // Current Job Data
    EmailRequest currentReq_;
    
    // Internal State
    mutable SemaphoreHandle_t mutex_;
    EmailStatus status_;
    TaskHandle_t taskHandle_ = nullptr;
    bool jobPending_ = false;

    // The actual blocking send logic
    void processSend();
    
    // ESP Mail Client Callback
    static void smtpCallback(SMTP_Status status);
};
