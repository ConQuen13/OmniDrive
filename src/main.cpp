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

// Routine & Profile structures
struct RoutineStep {
  String dir;
  int power;
  unsigned long durationMs;
};

const int MAX_STEPS = 10;
RoutineStep activeSteps[MAX_STEPS];
int activeStepCount = 1;

String currentProfilePath = "/default.json";
String currentTool = "Default";
String lastScannedUID = "";
unsigned long lastNfcPoll = 0;

// Sequencer & Runtime tracking
bool isMotorRunning = false;
bool routineFinished = false;
int currentStepIdx = 0;
unsigned long stepStartTime = 0;
unsigned long runStartTime = 0;
const int MAX_LOG_ENTRIES = 20;

// Embedded HTML Dashboard (Offline-ready with CSS Bar Chart, Routine Editor & Import/Export)
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>OmniDrive OS</title>
  <style>
    body { font-family: system-ui, sans-serif; background: #121212; color: #e0e0e0; max-width: 560px; margin: 1.5rem auto; padding: 0 1rem; }
    h1 { color: #ff9800; font-size: 1.4rem; margin-bottom: 1rem; }
    h2 { font-size: 1.05rem; margin: 0 0 0.8rem 0; color: #fff; }
    .card { background: #1e1e1e; padding: 1.2rem; border-radius: 8px; border: 1px solid #333; margin-bottom: 1rem; }
    label { display: block; margin-top: 0.7rem; font-size: 0.8rem; color: #aaa; }
    select, input[type=text], input[type=number], button { width: 100%; margin-top: 0.3rem; padding: 0.55rem; border-radius: 5px; border: 1px solid #444; background: #2a2a2a; color: #fff; font-size: 0.9rem; box-sizing: border-box; }
    button { background: #ff9800; color: #000; font-weight: bold; border: none; cursor: pointer; margin-top: 0.9rem; }
    button:hover { background: #ffa726; }
    .row { display: flex; gap: 0.5rem; align-items: center; }
    .btn-sec { background: #333; color: #e0e0e0; margin-top: 0.3rem; }
    .btn-sec:hover { background: #444; }
    .btn-danger { background: #b71c1c; color: #fff; margin-top: 0.3rem; }
    .step-row { display: grid; grid-template-columns: 1.1fr 0.8fr 1fr 36px; gap: 0.4rem; margin-top: 0.4rem; align-items: center; }
    .step-row select, .step-row input, .step-row button { margin-top: 0; padding: 0.45rem; }
    #status { margin-top: 0.6rem; font-size: 0.85rem; color: #4caf50; text-align: center; min-height: 1.2em; }
    .chart { display: flex; align-items: flex-end; gap: 4px; height: 100px; padding-top: 10px; border-bottom: 1px solid #444; margin-bottom: 0.8rem; }
    .bar-wrap { flex: 1; display: flex; flex-direction: column; align-items: center; height: 100%; justify-content: flex-end; }
    .bar { width: 100%; background: #ff9800; border-radius: 3px 3px 0 0; min-height: 4px; }
    .bar.rev { background: #29b6f6; }
    .log-list { max-height: 140px; overflow-y: auto; font-family: monospace; font-size: 0.8rem; background: #141414; padding: 0.6rem; border-radius: 5px; border: 1px solid #2a2a2a; }
    .log-item { padding: 3px 0; border-bottom: 1px solid #222; }
  </style>
</head>
<body>
  <h1>OmniDrive OS // Routine & Profile Studio</h1>

  <div class="card">
    <h2>Profile Manager</h2>
    <div class="row">
      <select id="profileSelect" style="flex:2" onchange="selectProfile(this.value)"></select>
      <button class="btn-sec" style="flex:1" onclick="newProfile()">+ New</button>
      <button class="btn-danger" id="delBtn" style="flex:0.8" onclick="deleteProfile()">Del</button>
    </div>

    <label>Profile Name</label>
    <input type="text" id="profName" placeholder="e.g. Tap Threading">

    <label>Linked NFC Tag UID (leave empty for manual/default)</label>
    <div class="row">
      <input type="text" id="nfcUid" placeholder="e.g. 7C 7A EA 2F" style="flex:2">
      <button class="btn-sec" style="flex:1" onclick="grabNfc()">Grab Scanned</button>
    </div>

    <label>Routine Steps (Dir | Power % | Duration ms, 0 = hold trigger)</label>
    <div id="stepsContainer"></div>
    <button class="btn-sec" onclick="addStepRow('FWD', 80, 500)">+ Add Step</button>

    <button onclick="saveCurrentProfile()">Save & Activate Profile</button>

    <div class="row" style="margin-top:0.5rem">
      <button class="btn-sec" onclick="exportProfile()">Export JSON</button>
      <button class="btn-sec" onclick="document.getElementById('fileInput').click()">Import JSON</button>
      <input type="file" id="fileInput" accept=".json" style="display:none" onchange="importProfile(event)">
    </div>
    <div id="status"></div>
  </div>

  <div class="card">
    <h2>Usage Stats (Last 20 Runs)</h2>
    <div class="chart" id="chart"></div>
    <div class="log-list" id="logList">No runs recorded yet.</div>
    <div class="row" style="margin-top:0.5rem">
      <button class="btn-sec" onclick="loadLogs()">Refresh</button>
      <button class="btn-danger" onclick="clearLogs()">Clear Logs</button>
    </div>
  </div>

  <script>
    let activePath = '/default.json';

    function setStatus(msg, isErr = false) {
      const el = document.getElementById('status');
      el.style.color = isErr ? '#f44336' : '#4caf50';
      el.innerText = msg;
    }

    function addStepRow(dir = 'FWD', power = 75, duration = 0) {
      const container = document.getElementById('stepsContainer');
      if (container.children.length >= 10) return setStatus('Max 10 steps!', true);
      const div = document.createElement('div');
      div.className = 'step-row';
      div.innerHTML = `
        <select class="s-dir">
          <option value="FWD" ${dir==='FWD'?'selected':''}>FWD</option>
          <option value="REV" ${dir==='REV'?'selected':''}>REV</option>
          <option value="BRAKE" ${dir==='BRAKE'?'selected':''}>BRAKE</option>
        </select>
        <input type="number" class="s-pow" min="0" max="100" value="${power}" title="Power %">
        <input type="number" class="s-dur" min="0" step="50" value="${duration}" title="Duration ms (0=hold)">
        <button class="btn-danger" onclick="this.parentElement.remove()">×</button>
      `;
      container.appendChild(div);
    }

    async function refreshProfileList(selectPath = null) {
      const res = await fetch('/api/profiles');
      const list = await res.json();
      const sel = document.getElementById('profileSelect');
      sel.innerHTML = list.map(p => `<option value="${p.path}">${p.name} ${p.nfc_uid ? '['+p.nfc_uid+']' : ''}</option>`).join('');
      if (selectPath) sel.value = selectPath;
      await selectProfile(sel.value);
    }

    async function selectProfile(path) {
      activePath = path;
      const isDefault = (path === '/default.json');
      document.getElementById('delBtn').disabled = isDefault;
      document.getElementById('profName').disabled = isDefault;

      const res = await fetch('/api/profile?file=' + encodeURIComponent(path));
      const data = await res.json();
      document.getElementById('profName').value = data.name || 'Default';
      document.getElementById('nfcUid').value = data.nfc_uid || '';
      const container = document.getElementById('stepsContainer');
      container.innerHTML = '';
      (data.steps || []).forEach(s => addStepRow(s.dir, s.power, s.duration_ms));
    }

    function newProfile() {
      const rawName = prompt('New profile name (e.g. Drill_High):');
      if (!rawName) return;
      const slug = rawName.trim().toLowerCase().replace(/[^a-z0-9_-]/g, '_');
      if (!slug) return;
      activePath = '/profiles/' + slug + '.json';
      document.getElementById('delBtn').disabled = false;
      document.getElementById('profName').disabled = false;
      document.getElementById('profName').value = rawName.trim();
      document.getElementById('nfcUid').value = '';
      document.getElementById('stepsContainer').innerHTML = '';
      addStepRow('FWD', 100, 0);
      setStatus('New draft ready. Click Save to store.');
    }

    function buildPayload() {
      const rows = document.querySelectorAll('#stepsContainer .step-row');
      const steps = Array.from(rows).map(r => ({
        dir: r.querySelector('.s-dir').value,
        power: parseInt(r.querySelector('.s-pow').value) || 0,
        duration_ms: parseInt(r.querySelector('.s-dur').value) || 0
      }));
      return {
        name: document.getElementById('profName').value.trim() || 'Unnamed',
        nfc_uid: document.getElementById('nfcUid').value.trim().toUpperCase(),
        steps: steps.length ? steps : [{ dir: 'FWD', power: 75, duration_ms: 0 }]
      };
    }

    async function saveCurrentProfile(customPayload = null) {
      const payload = customPayload || buildPayload();
      const res = await fetch('/api/profile?file=' + encodeURIComponent(activePath), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      });
      if (res.ok) {
        setStatus('Saved & activated: ' + payload.name);
        await refreshProfileList(activePath);
      } else {
        setStatus('Failed to save profile!', true);
      }
    }

    async function deleteProfile() {
      if (activePath === '/default.json') return;
      if (!confirm('Delete ' + activePath + '?')) return;
      await fetch('/api/profile?file=' + encodeURIComponent(activePath), { method: 'DELETE' });
      setStatus('Profile deleted.');
      await refreshProfileList('/default.json');
    }

    async function grabNfc() {
      const res = await fetch('/api/nfc/last');
      const data = await res.json();
      if (data.uid) {
        document.getElementById('nfcUid').value = data.uid;
        setStatus('Grabbed UID: ' + data.uid);
      } else {
        setStatus('No tag scanned yet! Hold tag to reader first.', true);
      }
    }

    function exportProfile() {
      const payload = buildPayload();
      const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json' });
      const a = document.createElement('a');
      a.href = URL.createObjectURL(blob);
      a.download = payload.name.toLowerCase().replace(/\s+/g, '_') + '.json';
      a.click();
    }

    function importProfile(event) {
      const file = event.target.files[0];
      if (!file) return;
      const reader = new FileReader();
      reader.onload = async (e) => {
        try {
          const parsed = JSON.parse(e.target.result);
          if (!parsed.steps || !Array.isArray(parsed.steps)) throw new Error();
          const slug = (parsed.name || file.name.replace('.json','')).toLowerCase().replace(/[^a-z0-9_-]/g, '_');
          if (activePath !== '/default.json') activePath = '/profiles/' + slug + '.json';
          await saveCurrentProfile(parsed);
        } catch {
          setStatus('Invalid JSON file!', true);
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
      if (!logs.length) { chartEl.innerHTML = ''; listEl.innerHTML = 'No runs recorded yet.'; return; }
      const maxDur = Math.max(...logs.map(l => l.duration_ms), 500);
      chartEl.innerHTML = logs.map(l => {
        const h = Math.max(8, Math.round((l.duration_ms / maxDur) * 100));
        return `<div class="bar-wrap" title="${l.tool}: ${l.duration_ms}ms @ ${l.power}% (${l.dir})">
                  <div class="bar ${l.dir==='REV'?'rev':''}" style="height:${h}%; opacity:${0.4+(l.power/166)}"></div>
                </div>`;
      }).join('');
      listEl.innerHTML = [...logs].reverse().map(l =>
        `<div class="log-item">[${l.tool}] ${l.dir} | ${l.power}% | ${(l.duration_ms/1000).toFixed(2)}s</div>`
      ).join('');
    }

    async function clearLogs() {
      await fetch('/api/logs', { method: 'DELETE' });
      loadLogs();
    }

    refreshProfileList('/default.json');
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

  currentProfilePath = String(path);
  currentTool = doc["name"] | "Default";
  JsonArray steps = doc["steps"].as<JsonArray>();

  activeStepCount = 0;
  for (JsonObject s : steps) {
    if (activeStepCount >= MAX_STEPS) break;
    activeSteps[activeStepCount].dir = s["dir"] | "FWD";
    activeSteps[activeStepCount].power = constrain((int)(s["power"] | 75), 0, 100);
    activeSteps[activeStepCount].durationMs = s["duration_ms"] | 0;
    activeStepCount++;
  }

  if (activeStepCount == 0) {
    activeSteps[0] = {"FWD", 75, 0};
    activeStepCount = 1;
  }

  Serial.printf("Loaded profile [%s] (%d steps)\n", currentTool.c_str(), activeStepCount);
}

void initFileSystem() {
  if (!LittleFS.begin(true)) {
    Serial.println("CRITICAL: LittleFS mount failed!");
    return;
  }
  if (!LittleFS.exists("/profiles")) {
    LittleFS.mkdir("/profiles");
  }
  if (!LittleFS.exists("/default.json")) {
    createDefaultProfile();
  }
  loadProfile("/default.json");
}

String findProfileByUID(const String& uid) {
  auto checkFile = [&](const String& path) -> bool {
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    JsonDocument doc;
    if (!deserializeJson(doc, f)) {
      String fileUid = doc["nfc_uid"] | "";
      fileUid.trim();
      fileUid.toUpperCase();
      if (fileUid.length() > 0 && fileUid == uid) {
        f.close();
        return true;
      }
    }
    f.close();
    return false;
  };

  if (checkFile("/default.json")) return "/default.json";

  File dir = LittleFS.open("/profiles");
  if (dir && dir.isDirectory()) {
    File file = dir.openNextFile();
    while (file) {
      String p = String(file.name());
      if (!p.startsWith("/")) p = "/profiles/" + p;
      file.close();
      if (checkFile(p)) return p;
      file = dir.openNextFile();
    }
  }
  return "";
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
  while (arr.size() >= MAX_LOG_ENTRIES) arr.remove(0);

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

void setupWebServer() {
  server.on("/", HTTP_GET, []() { server.send(200, "text/html", INDEX_HTML); });

  // List all profiles (default + /profiles/*)
  server.on("/api/profiles", HTTP_GET, []() {
    JsonDocument listDoc;
    JsonArray arr = listDoc.to<JsonArray>();

    auto addMeta = [&](const String& path) {
      File f = LittleFS.open(path, "r");
      if (!f) return;
      JsonDocument doc;
      if (!deserializeJson(doc, f)) {
        JsonObject o = arr.add<JsonObject>();
        o["path"] = path;
        o["name"] = doc["name"] | path;
        o["nfc_uid"] = doc["nfc_uid"] | "";
      }
      f.close();
    };

    addMeta("/default.json");
    File dir = LittleFS.open("/profiles");
    if (dir && dir.isDirectory()) {
      File file = dir.openNextFile();
      while (file) {
        String p = String(file.name());
        if (!p.startsWith("/")) p = "/profiles/" + p;
        file.close();
        addMeta(p);
        file = dir.openNextFile();
      }
    }
    String out;
    serializeJson(listDoc, out);
    server.send(200, "application/json", out);
  });

  // Read single profile
  server.on("/api/profile", HTTP_GET, []() {
    String path = server.arg("file");
    if (!path.startsWith("/") || !LittleFS.exists(path)) {
      server.send(404, "application/json", "{\"error\":\"Not found\"}");
      return;
    }
    File f = LittleFS.open(path, "r");
    server.streamFile(f, "application/json");
    f.close();
  });

  // Save & activate profile
  server.on("/api/profile", HTTP_POST, []() {
    String path = server.arg("file");
    if (!path.startsWith("/") || !server.hasArg("plain")) {
      server.send(400, "application/json", "{\"error\":\"Bad request\"}");
      return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, server.arg("plain"))) {
      server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      return;
    }
    if (path.startsWith("/profiles/") && !LittleFS.exists("/profiles")) {
      LittleFS.mkdir("/profiles");
    }
    File f = LittleFS.open(path, "w");
    serializeJsonPretty(doc, f);
    f.close();

    loadProfile(path.c_str());
    server.send(200, "application/json", "{\"status\":\"ok\"}");
  });

  // Delete custom profile
  server.on("/api/profile", HTTP_DELETE, []() {
    String path = server.arg("file");
    if (path == "/default.json" || !path.startsWith("/profiles/")) {
      server.send(403, "application/json", "{\"error\":\"Protected\"}");
      return;
    }
    LittleFS.remove(path);
    if (currentProfilePath == path) loadProfile("/default.json");
    server.send(200, "application/json", "{\"status\":\"deleted\"}");
  });

  // Return last scanned NFC UID
  server.on("/api/nfc/last", HTTP_GET, []() {
    server.send(200, "application/json", "{\"uid\":\"" + lastScannedUID + "\"}");
  });

  server.on("/api/logs", HTTP_GET, []() {
    if (!LittleFS.exists("/logs.json")) { server.send(200, "application/json", "[]"); return; }
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

void executeStep(int idx) {
  RoutineStep& s = activeSteps[idx];
  int pwm = map(s.power, 0, 100, 0, 255);
  if (s.dir == "FWD") {
    digitalWrite(IN1_PIN, HIGH);
    digitalWrite(IN2_PIN, LOW);
    analogWrite(ENA_PIN, pwm);
  } else if (s.dir == "REV") {
    digitalWrite(IN1_PIN, LOW);
    digitalWrite(IN2_PIN, HIGH);
    analogWrite(ENA_PIN, pwm);
  } else {
    brakeMotor();
  }
}

void pollNFC() {
  if (millis() - lastNfcPoll < 250) return;
  lastNfcPoll = millis();

  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) return;

  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    uid += String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.trim();
  uid.toUpperCase();

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  lastScannedUID = uid;
  Serial.println("NFC Tag scanned: " + uid);

  String matchedPath = findProfileByUID(uid);
  if (matchedPath.length() > 0 && matchedPath != currentProfilePath) {
    loadProfile(matchedPath.c_str());
  }
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
  if (!isMotorRunning) pollNFC();

  bool triggerPressed = (digitalRead(TRIGGER_PIN) == LOW);

  if (triggerPressed) {
    if (!isMotorRunning && !routineFinished) {
      isMotorRunning = true;
      currentStepIdx = 0;
      stepStartTime = millis();
      runStartTime = millis();
      executeStep(currentStepIdx);
    } else if (isMotorRunning) {
      unsigned long dur = activeSteps[currentStepIdx].durationMs;
      if (dur > 0 && (millis() - stepStartTime >= dur)) {
        currentStepIdx++;
        if (currentStepIdx < activeStepCount) {
          stepStartTime = millis();
          executeStep(currentStepIdx);
        } else {
          // End of timed routine reached -> stop until trigger is released
          brakeMotor();
          isMotorRunning = false;
          routineFinished = true;
          unsigned long totalDur = millis() - runStartTime;
          if (totalDur > 50) appendUsageLog(currentTool, activeSteps[0].dir, activeSteps[0].power, totalDur);
        }
      }
    }
  } else {
    if (isMotorRunning) {
      brakeMotor();
      unsigned long totalDur = millis() - runStartTime;
      if (totalDur > 50) {
        appendUsageLog(currentTool, activeSteps[currentStepIdx].dir, activeSteps[currentStepIdx].power, totalDur);
      }
    }
    isMotorRunning = false;
    routineFinished = false;
  }

  delay(10);
}