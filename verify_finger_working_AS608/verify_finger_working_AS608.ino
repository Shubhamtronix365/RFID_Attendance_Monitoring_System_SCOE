#include <Adafruit_Fingerprint.h>

// Use UART2
HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

void setup() {c:\Users\PC\Downloads\Biometric-Attendance-system-V2.0-master\Biometric-Attendance-system-V2.0-master\ESP32_Fingerprint200301\ESP32_Fingerprint200301.ino
  Serial.begin(115200);
  delay(1000);

  Serial.println("AS608 Fingerprint Verification - ESP32");

  // RX = 16, TX = 17
  mySerial.begin(57600, SERIAL_8N1, 16, 17);
  finger.begin(57600);

  if (finger.verifyPassword()) {
    Serial.println("Fingerprint sensor detected!");
  } else {
    Serial.println("Fingerprint sensor NOT detected :(");
    while (1) delay(1);
  }

  Serial.println("Waiting for valid finger...");
}

void loop() {
  getFingerprintID();
  delay(500);
}

uint8_t getFingerprintID() {
  uint8_t p = finger.getImage();

  if (p == FINGERPRINT_NOFINGER) {
    return p;  // No finger placed
  }

  if (p != FINGERPRINT_OK) {
    Serial.println("Error capturing image");
    return p;
  }

  Serial.println("Image taken");

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) {
    Serial.println("Error converting image");
    return p;
  }

  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    Serial.println("Fingerprint Found!");
    Serial.print("ID: ");
    Serial.println(finger.fingerID);
    Serial.print("Confidence: ");
    Serial.println(finger.confidence);
    Serial.println("-------------------------");
  } 
  else if (p == FINGERPRINT_NOTFOUND) {
    Serial.println("Fingerprint NOT Found");
    Serial.println("-------------------------");
  } 
  else {
    Serial.println("Error searching database");
  }

  delay(2000);
  return p;
}