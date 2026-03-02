#include <Adafruit_Fingerprint.h>

// Create UART2
HardwareSerial mySerial(2);

// Create fingerprint object
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

uint8_t id;

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\nAS608 Fingerprint Sensor Enrollment (ESP32)");

  // Initialize UART2 (RX=16, TX=17)
  mySerial.begin(57600, SERIAL_8N1, 16, 17);

  finger.begin(57600);

  if (finger.verifyPassword()) {
    Serial.println("Found fingerprint sensor!");
  } else {
    Serial.println("Did not find fingerprint sensor :(");
    while (1) delay(1);
  }

  Serial.println("Reading sensor parameters...");
  finger.getParameters();

  Serial.print("Capacity: ");
  Serial.println(finger.capacity);

  Serial.println("Ready to enroll.");
}

uint8_t readnumber(void) {
  uint8_t num = 0;
  while (num == 0) {
    while (!Serial.available());
    num = Serial.parseInt();
  }
  return num;
}

void loop()
{
  Serial.println("\nEnter ID (1-127): ");
  id = readnumber();

  if (id == 0) return;

  Serial.print("Enrolling ID #");
  Serial.println(id);

  while (!getFingerprintEnroll());
}

uint8_t getFingerprintEnroll() {

  int p = -1;
  Serial.print("Place finger for ID #"); 
  Serial.println(id);

  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p == FINGERPRINT_OK) Serial.println("Image taken");
    else if (p == FINGERPRINT_NOFINGER) Serial.print(".");
    else if (p == FINGERPRINT_PACKETRECIEVEERR) Serial.println("Communication error");
    else if (p == FINGERPRINT_IMAGEFAIL) Serial.println("Imaging error");
    else Serial.println("Unknown error");
  }

  p = finger.image2Tz(1);
  if (p != FINGERPRINT_OK) {
    Serial.println("Error converting image");
    return p;
  }

  Serial.println("Remove finger");
  delay(2000);
  while (finger.getImage() != FINGERPRINT_NOFINGER);

  Serial.println("Place same finger again");
  p = -1;
  while (p != FINGERPRINT_OK) {
    p = finger.getImage();
    if (p == FINGERPRINT_OK) Serial.println("Image taken");
    else if (p == FINGERPRINT_NOFINGER) Serial.print(".");
    else Serial.println("Error");
  }

  p = finger.image2Tz(2);
  if (p != FINGERPRINT_OK) {
    Serial.println("Error converting second image");
    return p;
  }

  Serial.println("Creating model...");
  p = finger.createModel();
  if (p != FINGERPRINT_OK) {
    Serial.println("Fingerprints did not match");
    return p;
  }

  Serial.print("Storing ID #");
  Serial.println(id);

  p = finger.storeModel(id);
  if (p == FINGERPRINT_OK) {
    Serial.println("Stored successfully!");
  } else {
    Serial.println("Error storing fingerprint");
    return p;
  }

  return true;
}