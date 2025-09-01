#include <Arduino.h>
#include <WiFi.h>
#include <ESP_Mail_Client.h>
#include <time.h>
#include ".env.h"

// DeepSleep and Touch
#define TouchThreshold 40
#define WakeUpSwitch GPIO_NUM_13

// Battery Monitoring
#define BATTERY_MEASURE_PIN GPIO_NUM_4 // GPIO to activate the battery measurement circuit
#define ADC_BATTERY_PIN GPIO_NUM_34    // ADC pin to read the battery voltage
#define VOLTAGE_DIVIDER_RATIO 1.319    // Voltage divider: 2.2K connected to battery, and (2.2K + 4.7K) to ground
#define MAX_VOLTAGE 4.2                // Maximum LiPo voltage
#define MIN_VOLTAGE 3.2                // Minimum safe LiPo voltage

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

String setupTime() {
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

void smtpCallback(SMTP_Status status) {
  /* Print the current status */
  Serial.println(status.info());

  /* Print the sending result */
  if (status.success()) {
    Serial.println("----------------");
    ESP_MAIL_PRINTF("Message sent success: %d\n", status.completedCount());
    ESP_MAIL_PRINTF("Message sent failed: %d\n", status.failedCount());
    Serial.println("----------------\n");

    for (size_t i = 0; i < smtp.sendingResult.size(); i++) {
      /* Get the result item */
      SMTP_Result result = smtp.sendingResult.getItem(i);
      ESP_MAIL_PRINTF("Message No: %d\n", i + 1);
      ESP_MAIL_PRINTF("Status: %s\n", result.completed ? "success" : "failed");
      ESP_MAIL_PRINTF("Date/Time: %s\n", MailClient.Time.getDateTimeString(result.timestamp, "%B %d, %Y %H:%M:%S").c_str());
      ESP_MAIL_PRINTF("Recipient: %s\n", result.recipients.c_str());
      ESP_MAIL_PRINTF("Subject: %s\n", result.subject.c_str());
    }
    Serial.println("----------------\n");
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

String readBatteryLevel() {
  // Wait for the current to stabilize (longer delay for a more reliable reading)
  delay(50);

  // Set up the ADC
  analogReadResolution(12); // Set resolution to 12 bits (0-4095)
  analogSetAttenuation(ADC_11db); // Set attenuation to 11dB, allowing max 3.9V input

  int adcValue = analogRead(ADC_BATTERY_PIN);
  float voltage = (adcValue / 4095.0) * 3.9; // Convert to voltage based on 11dB attenuation

  // Correct for the voltage divider
  float batteryVoltage = voltage * VOLTAGE_DIVIDER_RATIO;
  
  // Calculate the battery percentage
  float batteryPercentage = ((batteryVoltage - MIN_VOLTAGE) / (MAX_VOLTAGE - MIN_VOLTAGE)) * 100;
  if (batteryPercentage > 100) batteryPercentage = 100;
  if (batteryPercentage < 0) batteryPercentage = 0;

  // Deactivate the battery measurement circuit to save power
  digitalWrite(BATTERY_MEASURE_PIN, LOW);

  Serial.printf("Raw ADC Value: %d\n", adcValue);
  Serial.printf("Measured Voltage: %.2fV\n", batteryVoltage);
  Serial.printf("Battery Percentage: %.2f%%\n", batteryPercentage);

  return String("Battery: ") + String(batteryVoltage, 2) + String("V (") + String(batteryPercentage, 2) + String("%)");
}

void createMessage(String datetime, String batteryLevel) {
  message.sender.name = F("Your Mailbox");
  message.sender.email = AUTHOR_EMAIL;
  message.subject = F("You've got Mail!");
  message.addRecipient(F("Master!"), RECIPIENT_EMAIL);

  String wifiRSSIString = String(WiFi.RSSI());
  Serial.println("RSSI: " + wifiRSSIString);
  Serial.println("Timestamp: " + datetime);

  String textMsg = "You've got Mail!\n" + batteryLevel + "\nWiFi RSSI: " + wifiRSSIString + "\nTimestamp: " + datetime;
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

  // Initialize the pin to activate the battery circuit
  pinMode(BATTERY_MEASURE_PIN, OUTPUT);
  // Explicitly set the ADC pin as an input
  pinMode(ADC_BATTERY_PIN, INPUT);

  // Activate the battery measurement circuit to allow it to stabilize
  digitalWrite(BATTERY_MEASURE_PIN, HIGH);

  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));

  // Prime wake-up
  esp_sleep_enable_ext0_wakeup(WakeUpSwitch, LOW); // change to high for letter box

  // Do stuff
  setupWifi();
  String datetime = setupTime();
  String batteryLevel = readBatteryLevel();
  setupMailClient();
  createMessage(datetime, batteryLevel);
  sendMessage();

  // Go to sleep now
  Serial.println("Going to sleep now");
  esp_deep_sleep_start();
  Serial.println("This will never be printed");
}

void loop() {
  Serial.println("This will never be printed");
}
