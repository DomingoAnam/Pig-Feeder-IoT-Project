#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Servo.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// ================= WIFI =================
const char* ssid = "Anam's Private WiFi Nga Hinay";
const char* password = "anamnop@ssword2025";

// ================= TELEGRAM SETUP =================
#define BOT_TOKEN "8705848716:AAHidKN_5YxfYvsUnG4uiW6QUIT6FEOuXCg"  // Get from @BotFather
#define CHAT_ID "6121150683"      // Get from @userinfobot

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// ================= PINS =================
#define TRIGGER_PIN D1
#define ECHO_PIN D2
#define SERVO_PIN D3

// ================= SENSOR SETTINGS =================
const int fullDistance = 5;   // Distance at full (5cm from bottom)
const int emptyDistance = 30; // Distance at empty (30cm from bottom)

// ================= SERVO SETTINGS =================
Servo feedServo;
int servoOpen = 180;
int servoClosed = 0;
float gramsPerSecond = 50; // Calibration: grams per second

// ================= FEED SETTINGS =================
bool dispensing = false;
unsigned long dispenseStartTime = 0;
float targetGrams = 0;
float totalFeedConsumed = 0;
unsigned long lastFeedTime = 0;

// ================= PIG PEN STRUCTURE =================
struct Pen {
  String name;
  int pigs;
  float gramsPerPig;
  float feedLevel;
  String schedules[3];
};

Pen pens[1] = {
  {"Pen 1", 0, 100, 100, {"08:00", "12:00", "17:00"}}
};

// ================= SERVER =================
ESP8266WebServer server(80);

// ================= FUNCTION PROTOTYPES =================
float getDistance();
float getFeedLevelPercentage();
void dispenseFeed(float grams);
void checkSchedules();
void sendTelegramAlert(String message);
void handleUI();
void handleGetData();
void handleAddPig();
void handleRemovePig();
void handleUpdateFeedPerPig();
void handleUpdateSchedule();
void handleGetSchedule();

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  
  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  feedServo.attach(SERVO_PIN);
  feedServo.write(servoClosed);
  
  // Configure secure client for Telegram
  secured_client.setInsecure();
  
  WiFi.begin(ssid, password);

  // Wait for WiFi connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  
  // Print ONLY the IP address
  Serial.println(WiFi.localIP());

  server.on("/", handleUI);
  server.on("/getData", handleGetData);
  server.on("/addPig", handleAddPig);
  server.on("/removePig", handleRemovePig);
  server.on("/updateFeedPerPig", handleUpdateFeedPerPig);
  server.on("/updateSchedule", handleUpdateSchedule);
  server.on("/getSchedule", handleGetSchedule);
  server.begin();
  
  // Send startup notification
  sendTelegramAlert("✅ Smart Pig Feeder is ONLINE!");
}

// ================= LOOP =================
void loop() {
  server.handleClient();
  
  // Check and dispense feed based on schedule
  checkSchedules();
  
  // Handle dispensing process
  if (dispensing) {
    unsigned long elapsed = millis() - dispenseStartTime;
    unsigned long requiredTime = (targetGrams / gramsPerSecond) * 1000;
    
    if (elapsed >= requiredTime) {
      feedServo.write(servoClosed);
      dispensing = false;
      lastFeedTime = millis();
      
      // Send Telegram notification after dispensing
      String msg = "✅ Feeding completed!\n";
      msg += "Dispensed: " + String(targetGrams) + "g\n";
      msg += "Remaining feed: " + String(pens[0].feedLevel, 1) + "%";
      sendTelegramAlert(msg);
    }
  }
  
  // Update feed level every 5 seconds
  static unsigned long lastLevelCheck = 0;
  if (millis() - lastLevelCheck > 5000) {
    pens[0].feedLevel = getFeedLevelPercentage();
    lastLevelCheck = millis();
    
    // Check if feed level is 20% or below
    if (pens[0].feedLevel <= 20) {
      String alertMsg = "⚠️ LOW FEED ALERT!\n\n";
      alertMsg += "Feed Level: " + String(pens[0].feedLevel, 1) + "%\n";
      alertMsg += "Please refill the feeder immediately!";
      sendTelegramAlert(alertMsg);
    }
  }
  
  delay(100);
}

// ================= ULTRASONIC SENSOR FUNCTIONS =================
float getDistance() {
  digitalWrite(TRIGGER_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIGGER_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIGGER_PIN, LOW);
  
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  
  if (duration == 0) {
    return emptyDistance;
  }
  
  float distance = duration * 0.0343 / 2;
  if (distance < 2) distance = 2;
  if (distance > emptyDistance) distance = emptyDistance;
  
  return distance;
}

float getFeedLevelPercentage() {
  float distance = getDistance();
  
  if (distance <= fullDistance) return 100;
  if (distance >= emptyDistance) return 0;
  
  float percentage = 100 - ((distance - fullDistance) / (emptyDistance - fullDistance) * 100);
  percentage = constrain(percentage, 0, 100);
  
  return percentage;
}

// ================= SERVO DISPENSING FUNCTIONS =================
void dispenseFeed(float grams) {
  if (dispensing) {
    return;
  }
  
  if (pens[0].feedLevel <= 0) {
    sendTelegramAlert("❌ Cannot dispense - feed level is empty!");
    return;
  }
  
  targetGrams = grams;
  
  // Send Telegram notification before dispensing
  String msg = "🍽️ Scheduled feeding started!\n";
  msg += "Amount: " + String(grams) + "g";
  sendTelegramAlert(msg);
  
  feedServo.write(servoOpen);
  delay(500);
  
  dispensing = true;
  dispenseStartTime = millis();
  
  totalFeedConsumed += grams;
  
  float feedReduction = (grams / 1000.0) * 100;
  float newFeedLevel = pens[0].feedLevel - feedReduction;
  pens[0].feedLevel = constrain(newFeedLevel, 0, 100);
}

// ================= TELEGRAM ALERT =================
void sendTelegramAlert(String message) {
  static unsigned long lastTelegramTime = 0;
  
  // Rate limiting - don't send more than once per minute
  if (millis() - lastTelegramTime < 60000) {
    return;
  }
  
  // Check if token is configured
  if (String(BOT_TOKEN) == "YOUR_BOT_TOKEN_HERE") {
    return;
  }
  
  // Send the message
  int attempts = 0;
  while (attempts < 3) {
    if (bot.sendMessage(CHAT_ID, message, "")) {
      lastTelegramTime = millis();
      break;
    } else {
      attempts++;
      delay(1000);
    }
  }
}

// ================= SCHEDULE CHECKING =================
void checkSchedules() {
  static unsigned long lastScheduleCheck = 0;
  static int lastCheckedMinute = -1;
  
  if (millis() - lastScheduleCheck >= 60000) {
    lastScheduleCheck = millis();
    
    unsigned long seconds = millis() / 1000;
    int currentHour = (seconds / 3600) % 24;
    int currentMinute = (seconds / 60) % 60;
    
    if (currentMinute != lastCheckedMinute) {
      lastCheckedMinute = currentMinute;
      
      char currentTimeStr[6];
      sprintf(currentTimeStr, "%02d:%02d", currentHour, currentMinute);
      
      for (int i = 0; i < 3; i++) {
        if (pens[0].schedules[i] == String(currentTimeStr)) {
          float feedAmount = pens[0].pigs * pens[0].gramsPerPig;
          if (feedAmount > 0 && pens[0].feedLevel > 0) {
            dispenseFeed(feedAmount);
          }
          break;
        }
      }
    }
  }
}

// ================= WEB HANDLERS =================
void handleUI() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Smart Pig Feeder</title>
<script src="https://cdn.tailwindcss.com"></script>
<style>
body{font-family:Arial,Helvetica,sans-serif}
.page{display:none}
.page.active{display:block}
.loading{text-align:center;padding:20px}
</style>
</head>

<body class="bg-slate-950 text-white min-h-screen">
<div class="max-w-md mx-auto min-h-screen pb-24">

<header class="p-4 border-b border-slate-800 text-center">
<h1 class="text-2xl font-bold">🐷 Smart Pig Feeder</h1>
<p class="text-xs text-slate-500 mt-1">IoT Enabled Automatic Feeding System</p>
</header>

<main class="p-4 space-y-4">

<section id="data" class="page active space-y-4">
<h2 class="text-xl font-semibold">📊 Data</h2>

<div class="bg-slate-900 rounded-2xl p-6 text-center">
<div id="feedCircle" class="mx-auto w-44 h-44 rounded-full flex items-center justify-center text-3xl font-bold"
style="background:conic-gradient(#22c55e 0 72%, #334155 72% 100%);">
<div class="w-28 h-28 rounded-full bg-slate-950 flex items-center justify-center" id="feedLevel">--%</div>
</div>
<p class="mt-4 text-slate-400">Feed Level</p>
</div>

<div class="bg-slate-900 rounded-2xl p-4 text-center">
<p class="text-slate-400 text-sm">Total Feed Consumed</p>
<p id="feedConsumed" class="text-3xl font-bold">0g</p>
</div>

<div class="bg-slate-900 rounded-2xl p-4 text-center">
<p class="text-slate-400 text-sm">Total Pigs</p>
<p id="totalPigs" class="text-3xl font-bold">0</p>
</div>

<div class="bg-slate-900 rounded-2xl p-4">
<p class="text-slate-400 text-sm mb-2">System Status</p>
<p id="systemStatus" class="text-green-400">● System Online</p>
<p id="telegramStatus" class="text-xs text-slate-500 mt-1">📱 Telegram Alerts Active</p>
<p id="nextFeed" class="text-xs text-slate-500 mt-1">⏰ Next feeding: --</p>
</div>
</section>

<section id="penView" class="page space-y-4">
<div class="bg-slate-900 rounded-2xl p-4" id="penContent">
<div class="loading">Loading...</div>
</div>
</section>

</main>

<nav class="fixed bottom-0 left-0 right-0 max-w-md mx-auto grid grid-cols-2 bg-slate-900 border-t border-slate-800 text-center">
<button class="p-4 hover:bg-slate-800 transition" onclick="showPage('data')">
📊<br><span class="text-xs">Data</span>
</button>
<button class="p-4 hover:bg-slate-800 transition" onclick="showPage('penView')">
🐷<br><span class="text-xs">Pen</span>
</button>
</nav>

</div>

<script>
let pens = [{name:"Pen 1", pigs:0, gramsPerPig:100, schedules:["08:00","12:00","17:00"]}];

function showPage(id){
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  document.getElementById(id).classList.add('active');
  if(id === 'penView') openPen();
}

function openPen(){
  const pen = pens[0];
  document.getElementById('penContent').innerHTML = `
    <h2 class="text-xl font-semibold mb-4">${pen.name}</h2>
    <p class="mb-2">Number of Pigs: <b id="pigCount">${pen.pigs}</b></p>
    <div class="flex gap-2 mb-4">
      <button onclick="addPig()" class="flex-1 bg-blue-600 p-3 rounded-xl hover:bg-blue-700 transition">＋ Add Pig</button>
      <button onclick="removePig()" class="flex-1 bg-red-600 p-3 rounded-xl hover:bg-red-700 transition">－ Remove Pig</button>
    </div>
    <div class="mb-4">
      <label class="text-sm text-slate-400">Feed per pig (grams)</label>
      <input type="number" id="feedPerPigInput" value="${pen.gramsPerPig}" 
        class="w-full mt-2 p-3 bg-slate-800 rounded-xl" 
        onchange="updateFeedPerPig(this.value)">
    </div>
    <div class="mb-4">
      <label class="text-sm text-slate-400">Feeding Schedules</label>
      <div class="mt-2 space-y-2">
        <div class="bg-slate-800 p-3 rounded-xl">
          <span>Morning</span>
          <input type="time" id="schedule0" value="${pen.schedules[0]}" 
            class="float-right bg-slate-700 p-2 rounded-lg"
            onchange="updateSchedule(0, this.value)">
        </div>
        <div class="bg-slate-800 p-3 rounded-xl">
          <span>Afternoon</span>
          <input type="time" id="schedule1" value="${pen.schedules[1]}" 
            class="float-right bg-slate-700 p-2 rounded-lg"
            onchange="updateSchedule(1, this.value)">
        </div>
        <div class="bg-slate-800 p-3 rounded-xl">
          <span>Evening</span>
          <input type="time" id="schedule2" value="${pen.schedules[2]}" 
            class="float-right bg-slate-700 p-2 rounded-lg"
            onchange="updateSchedule(2, this.value)">
        </div>
      </div>
    </div>
    <div class="bg-slate-800 rounded-xl p-3 text-sm">
      <p>Total feed per dispensing: <b id="totalFeed">${pen.pigs * pen.gramsPerPig}g</b></p>
      <p class="mt-2 text-slate-400">⚠️ Feeder will dispense automatically at scheduled times</p>
    </div>
  `;
}

function addPig(){
  pens[0].pigs++;
  updatePigCount();
  updateTotalFeedDisplay();
  fetch('/addPig');
}

function removePig(){
  if(pens[0].pigs > 0) pens[0].pigs--;
  updatePigCount();
  updateTotalFeedDisplay();
  fetch('/removePig');
}

function updatePigCount(){
  const pigCountElem = document.getElementById('pigCount');
  if(pigCountElem) pigCountElem.textContent = pens[0].pigs;
}

function updateTotalFeedDisplay(){
  const totalFeedElem = document.getElementById('totalFeed');
  if(totalFeedElem){
    totalFeedElem.textContent = (pens[0].pigs * pens[0].gramsPerPig) + "g";
  }
}

function updateFeedPerPig(value){
  pens[0].gramsPerPig = parseInt(value);
  updateTotalFeedDisplay();
  fetch(`/updateFeedPerPig?grams=${value}`);
}

function updateSchedule(index, time){
  pens[0].schedules[index] = time;
  fetch(`/updateSchedule?index=${index}&time=${time}`);
}

function updateStats(){
  fetch('/getData')
    .then(response => response.json())
    .then(data => {
      document.getElementById("totalPigs").textContent = data.totalPigs;
      document.getElementById("feedConsumed").textContent = data.totalConsumed + "g";
      let level = data.feedLevel;
      document.getElementById("feedLevel").textContent = level + "%";
      let gradient = `conic-gradient(#22c55e 0 ${level}%, #334155 ${level}% 100%)`;
      document.getElementById("feedCircle").style.background = gradient;
      if(level <= 20){
        document.getElementById("systemStatus").innerHTML = "⚠️ Low Feed! Refill needed";
        document.getElementById("systemStatus").className = "text-red-400";
      } else {
        document.getElementById("systemStatus").innerHTML = "● System Online";
        document.getElementById("systemStatus").className = "text-green-400";
      }
      
      // Update next feeding time
      const now = new Date();
      const currentTime = now.getHours() * 60 + now.getMinutes();
      let nextTime = null;
      let nextTimeStr = "";
      
      for(let schedule of pens[0].schedules){
        const [hours, minutes] = schedule.split(':');
        const scheduleMinutes = parseInt(hours) * 60 + parseInt(minutes);
        if(scheduleMinutes > currentTime){
          if(nextTime === null || scheduleMinutes < nextTime){
            nextTime = scheduleMinutes;
            nextTimeStr = schedule;
          }
        }
      }
      
      if(nextTimeStr){
        document.getElementById("nextFeed").innerHTML = `⏰ Next feeding: ${nextTimeStr}`;
      } else if(pens[0].schedules.length > 0){
        document.getElementById("nextFeed").innerHTML = `⏰ Next feeding: Tomorrow ${pens[0].schedules[0]}`;
      }
    });
}

// Load initial schedule values
function loadSchedule(){
  fetch('/getSchedule')
    .then(response => response.json())
    .then(data => {
      pens[0].schedules = data.schedules;
      if(document.getElementById('schedule0')){
        document.getElementById('schedule0').value = pens[0].schedules[0];
        document.getElementById('schedule1').value = pens[0].schedules[1];
        document.getElementById('schedule2').value = pens[0].schedules[2];
      }
    });
}

updateStats();
setInterval(updateStats, 5000);
loadSchedule();
</script>

</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleGetData() {
  String json = "{";
  json += "\"feedLevel\":" + String(pens[0].feedLevel) + ",";
  json += "\"totalPigs\":" + String(pens[0].pigs) + ",";
  json += "\"totalConsumed\":" + String(totalFeedConsumed);
  json += "}";
  server.send(200, "application/json", json);
}

void handleGetSchedule() {
  String json = "{";
  json += "\"schedules\":[";
  json += "\"" + pens[0].schedules[0] + "\",";
  json += "\"" + pens[0].schedules[1] + "\",";
  json += "\"" + pens[0].schedules[2] + "\"";
  json += "]";
  json += "}";
  server.send(200, "application/json", json);
}

void handleAddPig() {
  pens[0].pigs++;
  server.send(200, "text/plain", "OK");
}

void handleRemovePig() {
  if(pens[0].pigs > 0) pens[0].pigs--;
  server.send(200, "text/plain", "OK");
}

void handleUpdateFeedPerPig() {
  if(server.hasArg("grams")) {
    pens[0].gramsPerPig = server.arg("grams").toFloat();
  }
  server.send(200, "text/plain", "OK");
}

void handleUpdateSchedule() {
  if(server.hasArg("index") && server.hasArg("time")) {
    int index = server.arg("index").toInt();
    String time = server.arg("time");
    if(index >= 0 && index < 3) {
      pens[0].schedules[index] = time;
    }
  }
  server.send(200, "text/plain", "OK");
}