#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// Wi-Fi Access Point Credentials
const char* AP_SSID = "OmniDrive-OS";
const char* AP_PASS = "omnidrive123"; // Min. 8 characters

// Hardware Pins
#define RST_PIN 22
#define SS_PIN 5
#define ENA_PIN 21
#define IN1_PIN 26
#define IN2_PIN 27
#define TRIGGER_PIN 4

MFRC522 mfrc522(SS_PIN, RST_PIN);
WebServer server(80);

// Active motor parameters
String currentTool = "Default";
String activeDirection = "FWD";
int activePowerPercent = 75;
int activePowerPWM = 191;
bool isMotorRunning = false;

// Embedded HTML Dashboard (stored in Flash memory)
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>OmniDrive OS</title>
  <style>
    body { font-family: system-ui, sans-serif; background: #121212; color: #e0e0e0; max-width: 480px; margin: 2rem auto; padding: 0 1rem; }
    h1 { color: #ff9800; font-size: 1.5rem; }
    .card { background: #1e1e1e; padding: 1.2rem; border-radius: 8px; border: 1px solid #333; margin-bottom: 1rem; }
    label { display: block; margin-top: 0.8rem; font-size: 0.9rem; color: #aaa; }
    select, input[type=range], button { width: 100%; margin-top: 0.4rem; padding: 0.6rem; border-radius: 5px; border: 1px solid #444; background: #2a2a2a; color: #fff; font-size: 1rem; }
    button { background: #ff9800; color: #000; font-weight: bold; border: none; cursor: pointer; margin-top: 1.2rem; }
    button:hover { background: #ffa726; }
    #status { margin-top: 0.8rem; font-size: 0.85rem; color: #4caf50; text-align: center; }
  </style>
</head>
<body>
  <h1>OmniDrive OS // Control</h1>
  <div class="card">
    <strong>Active Profile: <span id="profName">Default</span></strong>
    <label for="dir">Direction</label>
    <select id="dir">
      <option value="FWD">Forward (FWD)</option>
      <option value="REV">Reverse (REV)</option>
    </select>
    <label for="power">Motor Power: <span id="powerVal">75</span>%</label>
    <input type="range" id="power" min="0" max="100" value="75" oninput="document.getElementById('powerVal').innerText = this.value">
    <button onclick="saveProfile()">Save to default.json</button>
    <div id="status"></div>
  </div>
  <script>
    async function loadProfile() {
      const res = await fetch('/api/default');
      const data = await res.json();
      document.getElementById('profName').innerText = data.name;
      document.getElementById('dir').value = data.steps[0].dir;
      document.getElementById('power').value = data.steps[0].power;
      document.getElementById('powerVal').innerText = data.steps[0].power;
    }
    async function saveProfile() {
      const payload = {
        name: "Default",
        nfc_uid: "",
        steps: [{
          dir: document.getElementById('dir').value,
          power: parseInt(document.getElementById('power').value),
          duration_ms: 0
        }]
      };
      const res = await fetch('/api/default', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      });
      document.getElementById('status').innerText = res.ok ? "Saved & applied live!" : "Error saving profile.";
    }
    loadProfile();
  </script>
</body>
</html>
)rawliteral";

void createDefaultProfile() {
  Serial.println("WARNING: /default.json missing! Recreating default profile...");
  JsonDocument doc;
  doc["name"] = "Default";
  doc["nfc_uid"] = "";
  JsonArray steps = doc["steps"].to<JsonArray>();
  JsonObject step1 = steps.add<JsonObject>();
  step1["dir"] = "FWD";
  step1["power"] = 75;
  step1["duration_ms"] = 0;

  File file = LittleFS.open("/default.json", "w");
  if (file) {
    serializeJsonPretty(doc, file);
    file.close();
  }
}

void loadProfile(const char* path) {
  File file = LittleFS.open(path, "r");
  if (!file) return;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    if (String(path) == "/default.json") {
      createDefaultProfile();
      loadProfile("/default.json");
    }
    return;
  }

  currentTool = doc["name"] | "Default";
  activeDirection = doc["steps"][0]["dir"] | "FWD";
  activePowerPercent = doc["steps"][0]["power"] | 75;
  activePowerPWM = map(constrain(activePowerPercent, 0, 100), 0, 100, 0, 255);

  Serial.printf("Profile loaded: %s | Dir: %s | Power: %d%% (PWM: %d)\n",
                currentTool.c_str(), activeDirection.c_str(), activePowerPercent, activePowerPWM);
}

void initFileSystem() {
  if (!LittleFS.begin(true)) {
    Serial.println("CRITICAL: LittleFS mount failed!");
    return;
  }
  if (!LittleFS.exists("/default.json")) {
    createDefaultProfile();
  }
  loadProfile("/default.json");
}

void setupWebServer() {
  // Serve HTML UI
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", INDEX_HTML);
  });

  // API: Read default.json
  server.on("/api/default", HTTP_GET, []() {
    File file = LittleFS.open("/default.json", "r");
    if (!file) {
      server.send(404, "application/json", "{\"error\":\"File not found\"}");
      return;
    }
    server.streamFile(file, "application/json");
    file.close();
  });

  // API: Overwrite default.json and reload live
  server.on("/api/default", HTTP_POST, []() {
    if (!server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"Empty body\"}");
      return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    File file = LittleFS.open("/default.json", "w");
    serializeJsonPretty(doc, file);
    file.close();

    loadProfile("/default.json");
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  server.begin();
  Serial.println("WebServer started on http://192.168.4.1");
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

  // Start Wi-Fi Access Point
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("Wi-Fi AP started. SSID: ");
  Serial.print(AP_SSID);
  Serial.print(" | IP: ");
  Serial.println(WiFi.softAPIP());

  setupWebServer();

  SPI.begin();
  mfrc522.PCD_Init();

  pinMode(ENA_PIN, OUTPUT);
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  pinMode(TRIGGER_PIN, INPUT_PULLUP);
  brakeMotor();
}

void loop() {
  server.handleClient();

  bool triggerPressed = (digitalRead(TRIGGER_PIN) == LOW);
  if (triggerPressed && !isMotorRunning) {
    startMotor();
    isMotorRunning = true;
  } else if (!triggerPressed && isMotorRunning) {
    brakeMotor();
    isMotorRunning = false;
  }

  delay(10);
}