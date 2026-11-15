#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

#define RST_PIN 22
#define SS_PIN 5
#define ENA_PIN 21
#define IN1_PIN 26
#define IN2_PIN 27
#define TRIGGER_PIN 4

MFRC522 mfrc522(SS_PIN, RST_PIN);

// Active motor parameters (loaded from JSON)
String currentTool = "Default";
String activeDirection = "FWD";
int activePowerPWM = 191; // ~75% of 255

void createDefaultProfile() {
  Serial.println("WARNING: /default.json missing! Recreating default profile...");
  
  JsonDocument doc;
  doc["name"] = "Default";
  doc["nfc_uid"] = "";
  
  JsonArray steps = doc["steps"].to<JsonArray>();
  JsonObject step1 = steps.add<JsonObject>();
  step1["dir"] = "FWD";
  step1["power"] = 75;       // Percentage (0-100)
  step1["duration_ms"] = 0; // 0 = continuous run while trigger is pressed

  File file = LittleFS.open("/default.json", "w");
  if (!file) {
    Serial.println("ERROR: Failed to write /default.json!");
    return;
  }
  serializeJsonPretty(doc, file);
  file.close();
  Serial.println("SUCCESS: /default.json created.");
}

void loadProfile(const char* path) {
  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.println("ERROR: Failed to open profile file.");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.print("JSON Error (corrupted file?): ");
    Serial.println(error.c_str());
    // Auto-heal if default.json is corrupted
    if (String(path) == "/default.json") {
      createDefaultProfile();
      loadProfile("/default.json");
    }
    return;
  }

  currentTool = doc["name"] | "Default";
  activeDirection = doc["steps"][0]["dir"] | "FWD";
  int powerPercent = doc["steps"][0]["power"] | 75;
  
  // Map percentage (0-100) to PWM value (0-255)
  activePowerPWM = map(constrain(powerPercent, 0, 100), 0, 100, 0, 255);

  Serial.printf("Profile loaded: %s | Direction: %s | Power: %d%% (PWM: %d)\n", 
                currentTool.c_str(), activeDirection.c_str(), powerPercent, activePowerPWM);
}

void initFileSystem() {
  // 'true' formats the flash automatically on first mount
  if (!LittleFS.begin(true)) {
    Serial.println("CRITICAL: LittleFS mount failed!");
    return;
  }

  // Auto-Heal Check
  if (!LittleFS.exists("/default.json")) {
    createDefaultProfile();
  }
  loadProfile("/default.json");
}

void brakeMotor() {
  digitalWrite(IN1_PIN, HIGH);
  digitalWrite(IN2_PIN, HIGH);
  analogWrite(ENA_PIN, 0);
}

void startMotor() {
  if (activeDirection == "FWD") {
    digitalWrite(IN1_PIN, HIGH);
    digitalWrite(IN2_PIN, LOW);
  } else if (activeDirection == "REV") {
    digitalWrite(IN1_PIN, LOW);
    digitalWrite(IN2_PIN, HIGH);
  } else {
    brakeMotor();
    return;
  }
  analogWrite(ENA_PIN, activePowerPWM);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n--- OmniDrive OS Booting ---");
  
  initFileSystem();

  SPI.begin();
  mfrc522.PCD_Init();
  
  pinMode(ENA_PIN, OUTPUT);
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  brakeMotor();
}

bool isMotorRunning = false;

void loop() {
  bool triggerPressed = (digitalRead(TRIGGER_PIN) == LOW);

  if (triggerPressed && !isMotorRunning) {
    Serial.println("Trigger pressed -> Starting motor");
    startMotor();
    isMotorRunning = true;
  } 
  else if (!triggerPressed && isMotorRunning) {
    Serial.println("Trigger released -> Braking motor");
    brakeMotor();
    isMotorRunning = false;
  }

  delay(10); // Entprellt den Taster und entlastet die CPU
}