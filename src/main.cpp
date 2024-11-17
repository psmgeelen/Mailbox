// Sources:
// https://randomnerdtutorials.com/esp32-send-email-smtp-server-arduino-ide/
// https://lastminuteengineers.com/esp32-deep-sleep-wakeup-sources/
// https://github.com/G6EJD/LiPo_Battery_Capacity_Estimator/blob/master/ReadBatteryCapacity_LIPO.ino

#include <Arduino.h>
#include <WiFi.h>
#include <ESP_Mail_Client.h>
#include <time.h>
#include ".env.h"

// DeepSleep and Touch
#define TouchThreshold 40
#define WakeUpSwitch GPIO_NUM_13

// Value that is stored in RTC
RTC_DATA_ATTR int bootCount = 0;

SMTPSession smtp;
Session_Config config;
SMTP_Message message;


void setupWifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println();
  Serial.print("Connected with IP: ");
  Serial.println(WiFi.localIP());
  Serial.println();
}

String setupTime( ){
  // Set up NTP
  configTime(2, 0, "pool.ntp.org", "time.nist.gov"); // 2 offset for Berlin, adjust as needed
  // Wait for time to be set
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
      Serial.println("Failed to obtain time");
      return String("Failed to obtain time");
  }

  char buffer[100];

  // Format the timeinfo into the buffer
  strftime(buffer, sizeof(buffer), "%A, %B %d %Y %H:%M:%S", &timeinfo);

  // Convert to String object
  String dateTimeString = String(buffer);

  // Print the current time
  Serial.println(dateTimeString);
  return dateTimeString;
}

void smtpCallback(SMTP_Status status){
  /* Print the current status */
  Serial.println(status.info());

  /* Print the sending result */
  if (status.success()){

    Serial.println("----------------");
    ESP_MAIL_PRINTF("Message sent success: %d\n", status.completedCount());
    ESP_MAIL_PRINTF("Message sent failed: %d\n", status.failedCount());
    Serial.println("----------------\n");

    for (size_t i = 0; i < smtp.sendingResult.size(); i++)
    {
      /* Get the result item */
      SMTP_Result result = smtp.sendingResult.getItem(i);

      ESP_MAIL_PRINTF("Message No: %d\n", i + 1);
      ESP_MAIL_PRINTF("Status: %s\n", result.completed ? "success" : "failed");
      ESP_MAIL_PRINTF("Date/Time: %s\n", MailClient.Time.getDateTimeString(result.timestamp, "%B %d, %Y %H:%M:%S").c_str());
      ESP_MAIL_PRINTF("Recipient: %s\n", result.recipients.c_str());
      ESP_MAIL_PRINTF("Subject: %s\n", result.subject.c_str());
    }
    Serial.println("----------------\n");

    // You need to clear sending result as the memory usage will grow up.
    smtp.sendingResult.clear();
  }
};

void setupMailClient() {
  MailClient.networkReconnect(true);
  smtp.debug(1);
  smtp.callback(smtpCallback);

  // Setup SMTP session configuration
  config.server.host_name = SMTP_HOST;
  config.server.port = SMTP_PORT;
  config.login.email = AUTHOR_EMAIL;
  config.login.password = AUTHOR_PASSWORD;
  config.login.user_domain = "";

}

void createMessage(String datetime) {
  message.sender.name = F("Your Mailbox");
  message.sender.email = AUTHOR_EMAIL;
  message.subject = F("You've got Mail!");
  message.addRecipient(F("Master!"), RECIPIENT_EMAIL);

  String wifiRSSIString = String(WiFi.RSSI());
  Serial.println("RSSI: " + wifiRSSIString);
  Serial.println("Timestamp: " + datetime);

  String textMsg = "You've got Mail!\nWiFi RSSI: " + wifiRSSIString + "\nTimestamp: " + datetime;
  // String textMsg = "You've got Mail!\nWiFi";
  message.text.content = textMsg.c_str();
  message.text.charSet = "us-ascii";
  message.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;
  message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_low;
  message.response.notify = esp_mail_smtp_notify_success | esp_mail_smtp_notify_failure | esp_mail_smtp_notify_delay;
}

void sendMessage() {
  if (!smtp.connect(&config)) {
    ESP_MAIL_PRINTF("Connection error, Status Code: %d, Error Code: %d, Reason: %s", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
    return;
  }

  if (smtp.isLoggedIn()) {
    Serial.println("\nSuccessfully logged in.");
  } else {
    Serial.println("\nNot yet logged in.");
  }

  if (!MailClient.sendMail(&smtp, &message)) {
    ESP_MAIL_PRINTF("Error, Status Code: %d, Error Code: %d, Reason: %s", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
  }
}


void setup() {
  Serial.begin(9600);
  Serial.println();

  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));

  // Prime wake-up
  esp_sleep_enable_ext0_wakeup(WakeUpSwitch, HIGH); // change to high for letter box
  // // Do stuff
  setupWifi();
  String datetime = setupTime();
  setupMailClient();
  createMessage(datetime);
  sendMessage();

  // //Go to sleep now
  Serial.println("Going to sleep now");
  esp_deep_sleep_start();
  Serial.println("This will never be printed");
}

void loop() {
    Serial.println("This will never be printed");
}
