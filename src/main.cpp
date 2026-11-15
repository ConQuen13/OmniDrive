#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

// Wi-Fi Access Point Credentials
const char* AP_SSID = "OmniDrive-OS";
const char* AP_PASS = "omnidrive123";

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

// Runtime tracking
bool isMotorRunning = false;
unsigned long runStartTime = 0;
const int MAX_LOG_ENTRIES = 20;

// Embedded HTML Dashboard (Offline-ready with CSS Bar Chart & Import/Export)
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>OmniDrive OS</title>
  <style>
    body { font-family: system-ui, sans-serif; background: #121212; color: #e0e0e0; max-width: 520px; margin: 1.5rem auto; padding: 0 1rem; }
    h1 { color: #ff9800; font-size: 1.4rem; margin-bottom: 1rem; }
    h2 { font-size: 1.05rem; margin: 0 0 0.8rem 0; color: #fff; }
    .card { background: #1e1e1e; padding: 1.2rem; border-radius: 8px; border: 1px solid #333; margin-bottom: 1rem; }
    label { display: block; margin-top: 0.8rem; font-size: 0.85rem; color: #aaa; }
    select, input[type=range], button { width: 100%; margin-top: 0.4rem; padding: 0.6rem; border-radius: 5px; border: 1px solid #444; background: #2a2a2a; color: #fff; font-size: 0.95rem; box-sizing: border-box; }
    button { background: #ff9800; color: #000; font-weight: bold; border: none; cursor: pointer; margin-top: 1rem; }
    button:hover { background: #ffa726; }
    .btn-row { display: flex; gap: 0.5rem; margin-top: 0.6rem; }
    .btn-sec { background: #333; color: #e0e0e0; margin-top: 0; }
    .btn-sec:hover { background: #444; }
    .btn-danger { background: #b71c1c; color: #fff; margin-top: 0.8rem; }
    #status { margin-top: 0.6rem; font-size: 0.85rem; color: #4caf50; text-align: center; min-height: 1.2em; }
    .chart { display: flex; align-items: flex-end; gap: 4px; height: 110px; padding-top: 10px; border-bottom: 1px solid #444; margin-bottom: 0.8rem; }
    .bar-wrap { flex: 1; display: flex; flex-direction: column; align-items: center; height: 100%; justify-content: flex-end; }
    .bar { width: 100%; background: #ff9800; border-radius: 3px 3px 0 0; min-height: 4px; transition: height 0.2s; }
    .bar.rev { background: #29b6f6; }
    .log-list { max-height: 160px; overflow-y: auto; font-family: monospace; font-size: 0.8rem; background: #141414; padding: 0.6rem; border-radius: 5px; border: 1px solid #2a2a2a; }
    .log-item { padding: 3px 0; border-bottom: 1px solid #222; }
  </style>
</head>
<body>
  <h1>OmniDrive OS // Telemetry & Control</h1>

  <div class="card">
    <h2>Active Profile: <span id="profName">Default</span></h2>
    <label for="dir">Direction</label>
    <select id="dir">
      <option value="FWD">Forward (FWD)</option>
      <option value="REV">Reverse (REV)</option>
    </select>
    <label for="power">Motor Power: <span id="powerVal">75</span>%</label>
    <input type="range" id="power" min="0" max="100" value="75" oninput="document.getElementById('powerVal').innerText = this.value">
    <button onclick="saveProfile()">Save & Apply Live</button>

    <div class="btn-row">
      <button class="btn-sec" onclick="exportProfile()">Export JSON</button>
      <button class="btn-sec" onclick="document.getElementById('fileInput').click()">Import JSON</button>
      <input type="file" id="fileInput" accept=".json" style="display:none" onchange="importProfile(event)">
    </div>
    <div id="status"></div>
  </div>

  <div class="card">
    <h2>Usage Stats (Last 20 Runs - Duration & Power)</h2>
    <div class="chart" id="chart"></div>
    <div class="log-list" id="logList">No runs recorded yet.</div>
    <button class="btn-sec" onclick="loadLogs()">Refresh Telemetry</button>
    <button class="btn-danger" onclick="clearLogs()">Clear Logs</button>
  </div>

  <script>
    let currentJson = {};

    function setStatus(msg, isErr = false) {
      const el = document.getElementById('status');
      el.style.color = isErr ? '#f44336' : '#4caf50';
      el.innerText = msg;
    }

    async function loadProfile() {
      const res = await fetch('/api/default');
      currentJson = await res.json();
      document.getElementById('profName').innerText = currentJson.name || 'Default';
      document.getElementById('dir').value = currentJson.steps[0].dir;
      document.getElementById('power').value = currentJson.steps[0].power;
      document.getElementById('powerVal').innerText = currentJson.steps[0].power;
    }

    async function saveProfile(customPayload = null) {
      const payload = customPayload || {
        name: currentJson.name || "Default",
        nfc_uid: currentJson.nfc_uid || "",
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
      if (res.ok) {
        setStatus("Saved & applied live!");
        loadProfile();
      } else {
        setStatus("Invalid profile format!", true);
      }
    }

    function exportProfile() {
      const blob = new Blob([JSON.stringify(currentJson, null, 2)], { type: 'application/json' });
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = (currentJson.name || 'default').toLowerCase() + '.json';
      a.click();
      setStatus("Profile exported.");
    }

    function importProfile(event) {
      const file = event.target.files[0];
      if (!file) return;
      const reader = new FileReader();
      reader.onload = async (e) => {
        try {
          const parsed = JSON.parse(e.target.result);
          if (!parsed.steps || !Array.isArray(parsed.steps)) throw new Error();
          await saveProfile(parsed);
          setStatus("Imported '" + (parsed.name || file.name) + "'!");
        } catch {
          setStatus("Corrupted or invalid JSON file!", true);
        }
      };
      reader.readAsText(file);
      event.target.value = '';
    }

    async function loadLogs() {
      const res = await fetch('/api/logs');
      const logs = await res.json();
      const chartEl = document.getElementById('chart');
      const listEl = document.getElementById('logList');
      chartEl.innerHTML = '';

      if (!logs.length) {
        listEl.innerHTML = 'No runs recorded yet.';
        return;
      }

      const maxDur = Math.max(...logs.map(l => l.duration_ms), 500);
      chartEl.innerHTML = logs.map(l => {
        const heightPct = Math.max(8, Math.round((l.duration_ms / maxDur) * 100));
        const cls = l.dir === 'REV' ? 'bar rev' : 'bar';
        return `<div class="bar-wrap" title="${l.tool}: ${l.duration_ms}ms @ ${l.power}% (${l.dir})">
                  <div class="${cls}" style="height:${heightPct}%; opacity:${0.4 + (l.power/166)}"></div>
                </div>`;
      }).join('');

      listEl.innerHTML = [...logs].reverse().map(l =>
        `<div class="log-item">[${l.tool}] ${l.dir} | ${l.power}% | ${(l.duration_ms / 1000).toFixed(2)}s</div>`
      ).join('');
    }

    async function clearLogs() {
      await fetch('/api/logs', { method: 'DELETE' });
      loadLogs();
    }

    loadProfile();
    loadLogs();
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

void appendUsageLog(String tool, String dir, int power, unsigned long durationMs) {
  JsonDocument doc;
  if (LittleFS.exists("/logs.json")) {
    File readFile = LittleFS.open("/logs.json", "r");
    if (readFile) {
      deserializeJson(doc, readFile);
      readFile.close();
    }
  }

  JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc.to<JsonArray>();
  while (arr.size() >= MAX_LOG_ENTRIES) {
    arr.remove(0); // Ring buffer: drop oldest entry
  }

  JsonObject entry = arr.add<JsonObject>();
  entry["tool"] = tool;
  entry["dir"] = dir;
  entry["power"] = power;
  entry["duration_ms"] = durationMs;

  File writeFile = LittleFS.open("/logs.json", "w");
  if (writeFile) {
    serializeJson(doc, writeFile);
    writeFile.close();
  }
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
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", INDEX_HTML);
  });

  server.on("/api/default", HTTP_GET, []() {
    File file = LittleFS.open("/default.json", "r");
    if (!file) {
      server.send(404, "application/json", "{\"error\":\"File not found\"}");
      return;
    }
    server.streamFile(file, "application/json");
    file.close();
  });

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

  server.on("/api/logs", HTTP_GET, []() {
    if (!LittleFS.exists("/logs.json")) {
      server.send(200, "application/json", "[]");
      return;
    }
    File file = LittleFS.open("/logs.json", "r");
    server.streamFile(file, "application/json");
    file.close();
  });

  server.on("/api/logs", HTTP_DELETE, []() {
    LittleFS.remove("/logs.json");
    server.send(200, "application/json", "{\"status\":\"cleared\"}");
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
    runStartTime = millis();
    startMotor();
    isMotorRunning = true;
  } else if (!triggerPressed && isMotorRunning) {
    brakeMotor();
    isMotorRunning = false;
    unsigned long duration = millis() - runStartTime;
    if (duration > 50) { // Ignore accidental micro-bounces
      appendUsageLog(currentTool, activeDirection, activePowerPercent, duration);
    }
  }

  delay(10);
}