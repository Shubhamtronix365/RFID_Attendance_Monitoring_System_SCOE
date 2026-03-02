#include <WiFi.h>
#include <HTTPClient.h>
#include <Adafruit_Fingerprint.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <MFRC522.h>

// ================== WIFI SETTINGS ==================
const char* ssid = "Tronix365_4G";
const char* password = "Tronix@365";

String URL = "http://192.168.1.5/biometricattendancev2/getdata.php";
String device_token = "3e82b09943461f08";

// ================== OLED SETTINGS ==================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ================== FINGERPRINT SETTINGS ==================
HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

// ================== RC522 SETTINGS ==================
#define SS_PIN 5
#define RST_PIN 4
MFRC522 mfrc522(SS_PIN, RST_PIN);

// ================== SETUP ==================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\nAS608 Fingerprint Sensor / RFID System");

  // Initialize fingerprint sensor first (as in working code)
  mySerial.begin(57600, SERIAL_8N1, 16, 17); // RX=16 TX=17
  finger.begin(57600);

  if (finger.verifyPassword()) {
    Serial.println("Fingerprint sensor detected!");
    finger.getParameters(); // From working code
  } else {
    Serial.println("Fingerprint sensor NOT detected!");
    // We will show this on OLED later if it fails, but let's continue to init OLED
  }

  // Initialize OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found");
    while (1);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(10,20);
  
  if (!finger.verifyPassword()) {
    display.println("Fingerprint Error!");
    display.display();
    while (1);
  }

  display.println("Connecting WiFi...");
  display.display();

  // Connect WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");
  display.clearDisplay();
  display.setCursor(20,20);
  display.println("WiFi Connected");
  display.display();
  delay(2000);

  // Initialize SPI and RC522
  SPI.begin();
  mfrc522.PCD_Init();

  // Initialize Random for Cache-Busting
  randomSeed(analogRead(0));

  display.clearDisplay();
  display.setCursor(10,10);
  display.println("System Ready");
  display.setCursor(10,30);
  display.println("Scan RFID Card");
  display.display();
}

// ================== GLOBAL STATES ==================
enum SystemState { STATE_READY, STATE_ENROLL_FINGER, STATE_ENROLL_RFID };
SystemState currentState = STATE_READY;
int enrollID = -1;
String lastResponse = ""; // To prevent OLED flickering

// ================== MAIN LOOP ==================
void loop() {
  // 1. Poll server for commands
  checkServerRequests();

  // 2. Handle RFID Scans
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    String card_uid = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      card_uid += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
      card_uid += String(mfrc522.uid.uidByte[i], HEX);
    }
    card_uid.toUpperCase();
    
    if (currentState == STATE_ENROLL_RFID) {
      displayMessage("ENROLLING RFID", "Sending to Server");
      sendToServer(0, card_uid); // dummy finger ID
      currentState = STATE_READY;
    } else {
      processAttendance(card_uid);
    }
    
    mfrc522.PICC_HaltA();
    delay(1000);
  }

  delay(500);
}

// ================== SERVER POLLING ==================
void checkServerRequests() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String requestURL = URL + "?Get_Fingerid=get_id&device_token=" + device_token + "&r=" + String(random(10000));
  
  http.begin(requestURL);
  int httpResponseCode = http.GET();
  
  if (httpResponseCode == 200) {
    String response = http.getString();
    
    // Skip display update if response is same as last (reduces flicker)
    if (response == lastResponse && currentState == STATE_READY) {
      http.end();
      return;
    }
    lastResponse = response;

    // 1. Handle Fingerprint Enrollment
    if (response.indexOf("add-id") >= 0) {
      int colonIdx = response.indexOf(":");
      String name = (colonIdx > 0) ? response.substring(colonIdx + 1) : "User";
      enrollID = response.substring(6, colonIdx).toInt();
      
      if (currentState != STATE_ENROLL_FINGER) {
        currentState = STATE_ENROLL_FINGER;
        displayMessage("ENROLL FINGER", name);
        delay(1500);
        enrollFingerprint(enrollID);
        currentState = STATE_READY;
      }
    } 
    // 2. Handle RFID Enrollment Request
    else if (response.indexOf("enroll-rfid") >= 0) {
      int colonIdx = response.indexOf(":");
      String name = (colonIdx > 0) ? response.substring(colonIdx + 1) : "User";
      if (currentState != STATE_ENROLL_RFID) {
        currentState = STATE_ENROLL_RFID;
        displayMessage("ENROLL RFID", "Scan for: " + name);
      }
    }
    // 3. Handle RFID Success
    else if (response.indexOf("rfid-ok") >= 0) {
      displayMessage("RFID SCANNED!", "Next: Prepare Finger");
      currentState = STATE_READY;
    }
    // 4. Handle User Selection
    else if (response.indexOf("selected") >= 0) {
      int colonIdx = response.indexOf(":");
      String name = (colonIdx > 0) ? response.substring(colonIdx + 1) : "Selected";
      displayMessage("USER SELECTED", name);
      currentState = STATE_READY;
    }
    // 5. Handle Management Mode (Page heartbeat)
    else if (response.indexOf("manage-mode") >= 0) {
      displayMessage("MANAGE USERS", "Ready to Enroll");
      currentState = STATE_READY;
    }
    // 6. Default Ready State
    else {
      if (currentState != STATE_READY || response != lastResponse) { 
        currentState = STATE_READY;
        displayWelcome();
      }
    }
  }
  http.end();
}
void enrollFingerprint(int id) {
  int p = -1;
  unsigned long enrollStartTime = millis();
  
  // Step 1: Capture first image
  while (true) {
    if (millis() - enrollStartTime > 60000) { // 60s Global Timeout
      Serial.println("Enrollment Global Timeout!");
      displayMessage("TIMEOUT", "Returning...");
      delay(2000);
      return;
    }

    displayMessage("ENROLL (1/2)", "Place Finger");
    p = -1;
    while (p != FINGERPRINT_OK) {
      p = finger.getImage();
      if (p == FINGERPRINT_OK) {
        displayMessage("IMAGE TAKEN", "Keep Still...");
        delay(500);
      } else if (p != FINGERPRINT_NOFINGER) {
        displayMessage("SENSOR ERROR", "Try Again");
        delay(500);
      }
      delay(100);
      if (checkCancel()) { Serial.println("Enrollment Cancelled (Step 1 Wait)"); return; }
      if (millis() - enrollStartTime > 60000) return;
    }

    p = finger.image2Tz(1);
    if (p == FINGERPRINT_OK) {
      displayMessage("1st STEP OK", "Remove Finger");
      delay(2000); // Robust delay for sensor reset
      while (finger.getImage() != FINGERPRINT_NOFINGER) {
        delay(100);
        if (checkCancel()) { Serial.println("Enrollment Cancelled (Step 1 Remove)"); return; }
        if (millis() - enrollStartTime > 60000) return;
      }
      break; 
    } else {
      handleEnrollError(p);
      delay(2000);
      displayMessage("LIFT FINGER", "Start Again");
      while (finger.getImage() != FINGERPRINT_NOFINGER) {
        delay(100);
        if (checkCancel()) { Serial.println("Enrollment Cancelled (Step 1 Retry)"); return; }
        if (millis() - enrollStartTime > 60000) return;
      }
    }
  }

  // Step 2: Capture second image
  while (true) {
    if (millis() - enrollStartTime > 60000) return;

    displayMessage("ENROLL (2/2)", "Place Again");
    p = -1;
    while (p != FINGERPRINT_OK) {
      p = finger.getImage();
      if (p == FINGERPRINT_OK) {
        displayMessage("IMAGE TAKEN", "Creating Model");
        delay(500);
      } else if (p != FINGERPRINT_NOFINGER) {
        displayMessage("ERROR", "");
      }
      delay(100);
      if (checkCancel()) { Serial.println("Enrollment Cancelled (Step 2 Wait)"); return; }
      if (millis() - enrollStartTime > 60000) return;
    }

    p = finger.image2Tz(2);
    Serial.print("Step 2 Image2Tz: "); Serial.println(p);
    if (p == FINGERPRINT_OK) {
      p = finger.createModel();
      Serial.print("CreateModel Result: "); Serial.println(p);
      if (p == FINGERPRINT_OK) {
        p = finger.storeModel(id);
        Serial.print("StoreModel Result: "); Serial.println(p);
        if (p == FINGERPRINT_OK) {
          displayMessage("SUCCESS", "Finger Saved");
          confirmEnrollment(id);
          delay(2000);
          return;
        } else {
          displayMessage("SAVE ERROR", "Code: " + String(p));
        }
      } else if (p == FINGERPRINT_ENROLLMISMATCH) {
        displayMessage("MISMATCH!", "Try Step 2 Again");
      } else {
        displayMessage("MODEL ERROR", "Code: " + String(p));
      }
      
      delay(2000);
      displayMessage("LIFT FINGER", "Reset Position");
      while (finger.getImage() != FINGERPRINT_NOFINGER) {
        delay(100);
        if (checkCancel()) return;
      }
    } else {
      handleEnrollError(p);
      delay(2000);
      displayMessage("LIFT FINGER", "Start Again");
      while (finger.getImage() != FINGERPRINT_NOFINGER) {
        delay(100);
        if (checkCancel()) return;
      }
    }
  }
}

void handleEnrollError(int p) {
  Serial.print("Conversion Error: "); Serial.println(p);
  switch (p) {
    case FINGERPRINT_IMAGEMESS:
      displayMessage("CONV ERROR", "Image Too Messy");
      break;
    case FINGERPRINT_FEATUREFAIL:
      displayMessage("CONV ERROR", "No Features Found");
      break;
    case FINGERPRINT_INVALIDIMAGE:
      displayMessage("CONV ERROR", "Invalid Image");
      break;
    default:
      displayMessage("CONV ERROR", "Unknown Error");
      break;
  }
}

bool checkCancel() {
  static unsigned long lastCancelCheck = 0;
  if (millis() - lastCancelCheck < 1000) return false; // Reduced to 1s
  lastCancelCheck = millis();

  HTTPClient http;
  String requestURL = URL + "?Get_Fingerid=get_id&device_token=" + device_token;
  http.begin(requestURL);
  int httpResponseCode = http.GET();
  bool cancelled = false;
  if (httpResponseCode == 200) {
    String response = http.getString();
    if (response.indexOf("add-id") < 0 && response.indexOf("enroll-rfid") < 0) {
      cancelled = true;
    }
  }
  http.end();
  
  if (cancelled) {
    displayWelcome();
    currentState = STATE_READY;
  }
  return cancelled; 
}

void confirmEnrollment(int id) {
  HTTPClient http;
  String confirmURL = URL + "?confirm_id=" + String(id) + "&device_token=" + device_token;
  http.begin(confirmURL);
  http.GET();
  http.end();
}

// ================== ATTENDANCE FLOW ==================
void processAttendance(String card_uid) {
  displayMessage("RFID OK", "Place Finger");
  
  long startTime = millis();
  int fingerID = -1;
  while (millis() - startTime < 10000) {
    fingerID = getFingerprintID();
    if (fingerID > 0) break;
    delay(100);
  }

  if (fingerID > 0) {
    displayMessage("VERIFYING", "");
    sendToServer(fingerID, card_uid);
  } else {
    displayMessage("TIMEOUT", "");
    delay(2000);
  }
}

// ================== HELPER FUNCTIONS ==================
void displayMessage(String line1, String line2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,10);
  display.println(line1);
  display.setCursor(0,35);
  display.println(line2);
  display.display();
}

void displayWelcome() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,10);
  display.println("WELCOME TO");
  display.setCursor(0,30);
  display.println("ATTENDANCE SYSTEM");
  display.setCursor(0,55);
  display.println("Ready to scan...");
  display.display();
}

void displayReady() {
  displayWelcome();
}

int getFingerprintID() {
  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return -1;
  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return -1;
  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) return finger.fingerID;
  return -1;
}

void sendToServer(int fingerID, String card_uid) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String postData = "FingerID=" + String(fingerID) + "&card_uid=" + card_uid + "&device_token=" + device_token;
    http.begin(URL);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    int responseCode = http.POST(postData);
    
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0,20);
    if (responseCode > 0) {
      String response = http.getString();
      if (response.indexOf("login") >= 0 || response.indexOf("logout") >= 0 || response.indexOf("RFID_Enrolled") >= 0) {
        display.println("SUCCESS");
      } else if (response.indexOf("RFID_ALREADY") >= 0) {
        display.println("ALREADY REG");
      } else {
        display.println("DENIED");
      }
    } else {
      display.println("SRV ERR");
    }
    display.display();
    delay(2000);
    http.end();
  }
}
