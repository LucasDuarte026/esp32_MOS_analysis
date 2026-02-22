#include "email_manager.h"
#include <FFat.h>
#include "log_buffer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Singleton Access
EmailManager& EmailManager::getInstance() {
    static EmailManager instance;
    return instance;
}

EmailManager::EmailManager() {
    mutex_ = xSemaphoreCreateMutex();
    // Start the background task
    xTaskCreatePinnedToCore(
        emailTask,
        "EmailTask",
        8192, // 8KB Stack (Email library needs ~4KB+ for SSL)
        this,
        1, // Low Priority
        &taskHandle_,
        1  // Core 1 (App Core)
    );
}

void EmailManager::begin() {
    LOG_INFO("EmailManager initialized (dynamic credentials)");
}

bool EmailManager::isBusy() const {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    bool busy = (status_.state != EmailStatus::IDLE && status_.state != EmailStatus::SUCCESS && status_.state != EmailStatus::FAILED);
    xSemaphoreGive(mutex_);
    return busy;
}

EmailStatus EmailManager::getStatus() const {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    EmailStatus s = status_;
    xSemaphoreGive(mutex_);
    return s;
}

void EmailManager::updateStatus(EmailStatus::State state, int progress, const String& msg, const String& file) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    status_.state = state;
    if (progress >= 0) status_.progress = progress;
    if (!msg.isEmpty()) status_.message = msg;
    if (!file.isEmpty()) status_.currentFile = file;
    status_.timestamp = millis();
    xSemaphoreGive(mutex_);
    
    // Log important state changes
    if (state == EmailStatus::FAILED) {
        LOG_ERROR("Email Error: %s", msg.c_str());
    } else if (state == EmailStatus::SUCCESS) {
        LOG_INFO("Email Success: %s", msg.c_str());
    } else {
        LOG_DEBUG("Email Status: %s (%d%%)", msg.c_str(), progress);
    }
}

bool EmailManager::sendEmailAsync(const EmailRequest& req) {
    if (isBusy()) {
        LOG_WARN("EmailManager busy, rejected request to %s", req.to.c_str());
        return false;
    }

    if (req.senderEmail.isEmpty() || req.senderPassword.isEmpty() || req.smtpHost.isEmpty()) {
        LOG_ERROR("Missing email credentials");
        return false;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    currentReq_ = req; // Copy request data
    jobPending_ = true;
    
    // Reset status
    status_.state = EmailStatus::CONNECTING;
    status_.progress = 0;
    status_.message = "Job Queued";
    status_.currentFile = "";
    xSemaphoreGive(mutex_);

    // Notify task
    xTaskNotifyGive(taskHandle_);
    return true;
}

// Static wrapper
void EmailManager::emailTask(void* parameter) {
    EmailManager* mgr = static_cast<EmailManager*>(parameter);
    while (true) {
        // Wait for notification
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        if (mgr->jobPending_) {
            mgr->processSend();
            mgr->jobPending_ = false;
        }
    }
}

// Callback function to get status
void EmailManager::smtpCallback(SMTP_Status status) {
    EmailManager& mgr = EmailManager::getInstance();
    
    // Detailed debug info
    const String& info = status.info();
    if (info.length() > 0) {
        LOG_DEBUG("SMTP Info: %s", info.c_str());
    }

    // Success check
    if (status.success()) {
        LOG_INFO("SMTP: Message sent success");
        mgr.updateStatus(EmailStatus::SUCCESS, 100, "Sent successfully!");
    }
}

void EmailManager::processSend() {
    updateStatus(EmailStatus::CONNECTING, 5, "Initializing SMTP session...");
    
    SMTPSession smtp;
    smtp.callback(smtpCallback);

    Session_Config config;
    config.server.host_name = currentReq_.smtpHost;
    config.server.port = currentReq_.smtpPort;
    config.login.email = currentReq_.senderEmail;
    config.login.password = currentReq_.senderPassword;
    config.login.user_domain = "";
    
    // Timeouts - Critical for large files
    config.time.ntp_server = "pool.ntp.org,time.nist.gov";
    config.time.gmt_offset = -3;
    config.time.day_light_offset = 0;

    SMTP_Message message;
    message.sender.name = currentReq_.senderName.isEmpty() ? "ESP32 MOSFET Analyzer" : currentReq_.senderName;
    message.sender.email = currentReq_.senderEmail;
    message.subject = currentReq_.subject;
    message.text.content = currentReq_.body;

    // Recipients
    if (currentReq_.to.indexOf(',') > 0) {
        // Multi-recipient parsing could go here, but simple addRecipient for single string works if formatted correctly?
        // ESP Mail Client usually wants separate calls. Let's assume comma separated in string works or parse it?
        // For robustness, let's parse simple comma for TO and CC
        int start = 0;
        int end = currentReq_.to.indexOf(',');
        while (end != -1) {
            String email = currentReq_.to.substring(start, end);
            email.trim();
            if (email.length() > 0) message.addRecipient(email, email); // Name same as email
            start = end + 1;
            end = currentReq_.to.indexOf(',', start);
        }
        String lastEmail = currentReq_.to.substring(start);
        lastEmail.trim();
        if (lastEmail.length() > 0) message.addRecipient(lastEmail, lastEmail);

    } else {
        message.addRecipient("User", currentReq_.to);
    }
    
    // CC
    if (!currentReq_.cc.isEmpty()) {
       int start = 0;
        int end = currentReq_.cc.indexOf(',');
        while (end != -1) {
            String email = currentReq_.cc.substring(start, end);
            email.trim();
            if (email.length() > 0) message.addCc(email);
            start = end + 1;
            end = currentReq_.cc.indexOf(',', start);
        }
        String lastEmail = currentReq_.cc.substring(start);
        lastEmail.trim();
        if (lastEmail.length() > 0) message.addCc(lastEmail);
    }

    // Attachments
    updateStatus(EmailStatus::SENDING_ATTACHMENT, 10, "Preparing attachments...");
    
    for (const auto& fname : currentReq_.files) {
        // Build full path and ensure leading slash
        String path = fname.startsWith("/") ? fname : "/" + fname;
        
        if (FFat.exists(path)) {
            LOG_INFO("Attaching file: %s", path.c_str());
            
            // Update status per file
            updateStatus(EmailStatus::SENDING_ATTACHMENT, -1, "Attaching: " + fname, fname);
            
            SMTP_Attachment att;
            att.descr.filename = fname.substring(fname.lastIndexOf('/') + 1);
            att.descr.mime = "text/csv";
            att.file.path = path.c_str();
            att.file.storage_type = esp_mail_file_storage_type_flash;
            message.addAttachment(att);
        } else {
            LOG_WARN("Attachment not found: %s", path.c_str());
        }
    }

    updateStatus(EmailStatus::SENDING_BODY, 20, "Connecting and sending...");

    if (!smtp.connect(&config)) {
        updateStatus(EmailStatus::FAILED, 0, "Connection failed");
        return;
    }

    if (!MailClient.sendMail(&smtp, &message)) {
        String err = smtp.errorReason();
        LOG_ERROR("MailClient send failed: %s", err.c_str());
        updateStatus(EmailStatus::FAILED, 0, "Send failed: " + err);
    } else {
        // Success is handled by callback or here as fallback
        updateStatus(EmailStatus::SUCCESS, 100, "Email sent successfully!");
    }
    
    // Clean up
    message.clearHeader(); 
    message.clearRecipients();
    message.clearAttachments();
}
