#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Servo.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>
#include <time.h>

// ================= WIFI =================
const char* ssid = "Anam's Private WiFi Nga Hinay";
const char* password = "anamnop@ssword2025";

// ================= TELEGRAM SETUP =================
#define BOT_TOKEN "8705848716:AAHidKN_5YxfYvsUnG4uiW6QUIT6FEOuXCg"
#define CHAT_ID "6121150683"

WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

// ================= NTP TIME SETUP =================
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 28800; // GMT+8 for Philippines
const int daylightOffset_sec = 0;

// ================= PINS =================
#define TRIGGER_PIN D1
#define ECHO_PIN D2
#define SERVO_PIN D3

// ================= SENSOR SETTINGS =================
const int fullDistance = 5;
const int emptyDistance = 30;

// ================= SERVO SETTINGS =================
Servo feedServo;
int servoOpen = 180;
int servoClosed = 0;
float gramsPerSecond = 50;

// ================= FEED SETTINGS =================
bool dispensing = false;
unsigned long dispenseStartTime = 0;
float targetGrams = 0;
float totalFeedConsumed = 0;
unsigned long lastFeedTime = 0;

// ================= PIG PEN STRUCTURE =================
struct Pen {
  String name;
  int piglets;
  float gramsPerPiglet;
  float feedLevel;
  String schedules[3];
  String lastFedTime[3]; // Track last fed time for each schedule
};

Pen pens[1] = {
  {"Pen", 0, 100, 100, {"08:00", "12:00", "17:00"}, {"", "", ""}}
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
void handleAddPiglet();
void handleRemovePiglet();
void handleUpdateFeedPerPiglet();
void handleUpdateSchedule();
void handleGetSchedule();
void syncTime();
String getCurrentTime();
String getCurrentDate();
int getCurrentHour();
int getCurrentMinute();

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  
  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  feedServo.attach(SERVO_PIN);
  feedServo.write(servoClosed);
  
  secured_client.setInsecure();
  
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  
  // Print IP Address
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  
  // Sync time with NTP server
  syncTime();

  server.on("/", handleUI);
  server.on("/getData", handleGetData);
  server.on("/addPiglet", handleAddPiglet);
  server.on("/removePiglet", handleRemovePiglet);
  server.on("/updateFeedPerPiglet", handleUpdateFeedPerPiglet);
  server.on("/updateSchedule", handleUpdateSchedule);
  server.on("/getSchedule", handleGetSchedule);
  server.begin();
  
  Serial.println("Web server started!");
  Serial.println("Open browser and enter: http://" + WiFi.localIP().toString());
  
  sendTelegramAlert("✅ Smart Pig Feeder is ONLINE!\nIP Address: " + WiFi.localIP().toString());
}

// ================= SYNC TIME WITH NTP =================
void syncTime() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.print("Syncing time with NTP server");
  
  struct tm timeinfo;
  int attempts = 0;
  while (!getLocalTime(&timeinfo) && attempts < 10) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  Serial.println();
  
  if (attempts < 10) {
    char timeBuffer[9];
    strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", &timeinfo);
    Serial.print("Time synchronized! Current time: ");
    Serial.println(timeBuffer);
  } else {
    Serial.println("Failed to sync time! Using millis() based time.");
  }
}

// ================= GET CURRENT TIME STRING =================
String getCurrentTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    unsigned long seconds = millis() / 1000;
    int hour = (seconds / 3600) % 24;
    int minute = (seconds / 60) % 60;
    char timeStr[6];
    sprintf(timeStr, "%02d:%02d", hour, minute);
    return String(timeStr);
  }
  char timeStr[6];
  strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
  return String(timeStr);
}

String getCurrentDate() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return String(millis() / 86400000);
  }
  char dateStr[11];
  strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &timeinfo);
  return String(dateStr);
}

int getCurrentHour() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    unsigned long seconds = millis() / 1000;
    return (seconds / 3600) % 24;
  }
  return timeinfo.tm_hour;
}

int getCurrentMinute() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    unsigned long seconds = millis() / 1000;
    return (seconds / 60) % 60;
  }
  return timeinfo.tm_min;
}

// ================= LOOP =================
void loop() {
  server.handleClient();
  
  checkSchedules();
  
  // Handle dispensing process
  if (dispensing) {
    unsigned long elapsed = millis() - dispenseStartTime;
    unsigned long requiredTime = (targetGrams / gramsPerSecond) * 1000;
    
    if (elapsed >= requiredTime) {
      feedServo.write(servoClosed);
      dispensing = false;
      lastFeedTime = millis();
      
      Serial.println("Dispensing complete - Servo closed to 0 degrees");
      
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
    Serial.println("Already dispensing, skipping...");
    return;
  }
  
  if (pens[0].feedLevel <= 0) {
    sendTelegramAlert("❌ Cannot dispense - feed level is empty!");
    return;
  }
  
  if (pens[0].piglets <= 0) {
    sendTelegramAlert("❌ Cannot dispense - No piglets added!");
    return;
  }
  
  targetGrams = grams;
  float dispenseSeconds = grams / gramsPerSecond;
  
  Serial.println("========================================");
  Serial.println("SCHEDULED FEEDING STARTING");
  Serial.print("Current time: ");
  Serial.println(getCurrentTime());
  Serial.print("Target grams: ");
  Serial.print(grams);
  Serial.println("g");
  Serial.print("Dispensing for: ");
  Serial.print(dispenseSeconds);
  Serial.println(" seconds");
  Serial.println("Opening servo to 180 degrees...");
  
  feedServo.write(servoOpen);
  delay(500);
  
  Serial.println("Servo is OPEN at 180 degrees - Dispensing feed");
  
  String msg = "🍽️ Scheduled feeding started!\n";
  msg += "Time: " + getCurrentTime() + "\n";
  msg += "Amount: " + String(grams) + "g\n";
  msg += "Dispensing time: " + String(dispenseSeconds, 1) + " seconds";
  sendTelegramAlert(msg);
  
  dispensing = true;
  dispenseStartTime = millis();
  
  totalFeedConsumed += grams;
  
  float feedReduction = (grams / 1000.0) * 100;
  float newFeedLevel = pens[0].feedLevel - feedReduction;
  pens[0].feedLevel = constrain(newFeedLevel, 0, 100);
  
  Serial.print("New feed level: ");
  Serial.print(pens[0].feedLevel);
  Serial.println("%");
}

// ================= TELEGRAM ALERT =================
void sendTelegramAlert(String message) {
  static unsigned long lastTelegramTime = 0;
  
  if (millis() - lastTelegramTime < 60000) {
    return;
  }
  
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
  static String lastDate = "";
  
  // Get current date to reset tracking if needed
  String currentDate = getCurrentDate();
  if (currentDate != lastDate) {
    lastDate = currentDate;
    // Clear last fed times for new day
    for (int i = 0; i < 3; i++) {
      pens[0].lastFedTime[i] = "";
    }
    Serial.println("New day - Feed tracking reset");
  }
  
  if (millis() - lastScheduleCheck >= 60000) { // Check every minute
    lastScheduleCheck = millis();
    
    int currentHour = getCurrentHour();
    int currentMinute = getCurrentMinute();
    int currentTotalMinutes = currentHour * 60 + currentMinute;
    
    if (currentMinute != lastCheckedMinute) {
      lastCheckedMinute = currentMinute;
      
      char currentTimeStr[6];
      sprintf(currentTimeStr, "%02d:%02d", currentHour, currentMinute);
      
      // Check each schedule time
      for (int i = 0; i < 3; i++) {
        int scheduleHour = pens[0].schedules[i].substring(0, 2).toInt();
        int scheduleMinute = pens[0].schedules[i].substring(3, 5).toInt();
        int scheduleTotalMinutes = scheduleHour * 60 + scheduleMinute;
        
        // Check if current time matches schedule
        if (currentTotalMinutes == scheduleTotalMinutes) {
          // Check if we haven't fed at this exact minute yet
          if (pens[0].lastFedTime[i] != getCurrentTime()) {
            Serial.print("Schedule time matched: ");
            Serial.println(pens[0].schedules[i]);
            
            float feedAmount = pens[0].piglets * pens[0].gramsPerPiglet;
            
            if (feedAmount > 0 && pens[0].feedLevel > 0) {
              Serial.print("Dispensing ");
              Serial.print(feedAmount);
              Serial.println("g at scheduled time");
              dispenseFeed(feedAmount);
              pens[0].lastFedTime[i] = getCurrentTime(); // Mark this time as fed
            } else if (feedAmount <= 0) {
              Serial.println("Cannot dispense - No piglets added!");
              sendTelegramAlert("⚠️ Cannot dispense: No piglets added to the pen!");
            } else if (pens[0].feedLevel <= 0) {
              Serial.println("Cannot dispense - Feed level empty!");
              sendTelegramAlert("⚠️ Cannot dispense: Feed level is empty! Please refill.");
            }
          } else {
            Serial.println("Already fed at this time, skipping duplicate");
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
.time-input{background:#1e293b;border:1px solid #334155;border-radius:12px;padding:10px;color:white;width:120px}
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
<p class="text-slate-400 text-sm">Total Piglets</p>
<p id="totalPiglets" class="text-3xl font-bold">0</p>
</div>

<div class="bg-slate-900 rounded-2xl p-4">
<p class="text-slate-400 text-sm mb-2">System Status</p>
<p id="systemStatus" class="text-green-400">● System Online</p>
<p id="currentTime" class="text-xs text-slate-500 mt-1">🕐 Current time: --</p>
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
let pens = [{name:"Pen", piglets:0, gramsPerPiglet:100, schedules:["08:00","12:00","17:00"]}];

function showPage(id){
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  document.getElementById(id).classList.add('active');
  if(id === 'penView') openPen();
}

function openPen(){
  const pen = pens[0];
  document.getElementById('penContent').innerHTML = `
    <h2 class="text-xl font-semibold mb-4">${pen.name}</h2>
    <p class="mb-2">Number of Piglets: <b id="pigletCount">${pen.piglets}</b></p>
    <div class="flex gap-2 mb-4">
      <button onclick="addPiglet()" class="flex-1 bg-blue-600 p-3 rounded-xl hover:bg-blue-700 transition">＋ Add Piglet</button>
      <button onclick="removePiglet()" class="flex-1 bg-red-600 p-3 rounded-xl hover:bg-red-700 transition">－ Remove Piglet</button>
    </div>
    <div class="mb-4">
      <label class="text-sm text-slate-400">Feed per piglet (grams)</label>
      <input type="number" id="feedPerPigletInput" value="${pen.gramsPerPiglet}" 
        class="w-full mt-2 p-3 bg-slate-800 rounded-xl" 
        onchange="updateFeedPerPiglet(this.value)">
    </div>
    <div class="mb-4">
      <label class="text-sm text-slate-400">Feeding Schedules (24-hour format)</label>
      <div class="mt-2 space-y-2">
        <div class="bg-slate-800 p-3 rounded-xl flex justify-between items-center">
          <span>🌅 Morning Feeding</span>
          <input type="time" id="schedule0" value="${pen.schedules[0]}" 
            class="time-input" onchange="updateSchedule(0, this.value)">
        </div>
        <div class="bg-slate-800 p-3 rounded-xl flex justify-between items-center">
          <span>☀️ Afternoon Feeding</span>
          <input type="time" id="schedule1" value="${pen.schedules[1]}" 
            class="time-input" onchange="updateSchedule(1, this.value)">
        </div>
        <div class="bg-slate-800 p-3 rounded-xl flex justify-between items-center">
          <span>🌙 Evening Feeding</span>
          <input type="time" id="schedule2" value="${pen.schedules[2]}" 
            class="time-input" onchange="updateSchedule(2, this.value)">
        </div>
      </div>
    </div>
    <div class="bg-slate-800 rounded-xl p-3 text-sm">
      <p>Total feed per dispensing: <b id="totalFeed">${pen.piglets * pen.gramsPerPiglet}g</b></p>
      <p class="mt-2 text-yellow-400">⚠️ Feeder will dispense automatically at the scheduled times above</p>
      <p class="mt-1 text-xs text-slate-400">🔄 Servo opens to 180° during dispensing, closes to 0° when done</p>
      <p class="mt-1 text-xs text-blue-400">💡 Tip: Set any time you want, the feeder will follow exactly</p>
    </div>
  `;
}

function addPiglet(){
  pens[0].piglets++;
  updatePigletCount();
  updateTotalFeedDisplay();
  fetch('/addPiglet');
}

function removePiglet(){
  if(pens[0].piglets > 0) pens[0].piglets--;
  updatePigletCount();
  updateTotalFeedDisplay();
  fetch('/removePiglet');
}

function updatePigletCount(){
  const pigletCountElem = document.getElementById('pigletCount');
  if(pigletCountElem) pigletCountElem.textContent = pens[0].piglets;
}

function updateTotalFeedDisplay(){
  const totalFeedElem = document.getElementById('totalFeed');
  if(totalFeedElem){
    totalFeedElem.textContent = (pens[0].piglets * pens[0].gramsPerPiglet) + "g";
  }
}

function updateFeedPerPiglet(value){
  pens[0].gramsPerPiglet = parseInt(value);
  updateTotalFeedDisplay();
  fetch(`/updateFeedPerPiglet?grams=${value}`);
}

function updateSchedule(index, time){
  pens[0].schedules[index] = time;
  fetch(`/updateSchedule?index=${index}&time=${time}`);
  setTimeout(updateStats, 500);
}

function updateStats(){
  fetch('/getData')
    .then(response => response.json())
    .then(data => {
      document.getElementById("totalPiglets").textContent = data.totalPiglets;
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
      
      const now = new Date();
      const currentTimeStr = now.toLocaleTimeString('en-US', { hour: '2-digit', minute: '2-digit', hour12: false });
      document.getElementById("currentTime").innerHTML = `🕐 Current time: ${currentTimeStr}`;
      
      const currentMinutes = now.getHours() * 60 + now.getMinutes();
      let nextTime = null;
      let nextTimeStr = "";
      
      for(let schedule of pens[0].schedules){
        const [hours, minutes] = schedule.split(':');
        const scheduleMinutes = parseInt(hours) * 60 + parseInt(minutes);
        if(scheduleMinutes > currentMinutes){
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
  json += "\"totalPiglets\":" + String(pens[0].piglets) + ",";
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

void handleAddPiglet() {
  pens[0].piglets++;
  server.send(200, "text/plain", "OK");
}

void handleRemovePiglet() {
  if(pens[0].piglets > 0) pens[0].piglets--;
  server.send(200, "text/plain", "OK");
}

void handleUpdateFeedPerPiglet() {
  if(server.hasArg("grams")) {
    pens[0].gramsPerPiglet = server.arg("grams").toFloat();
  }
  server.send(200, "text/plain", "OK");
}

void handleUpdateSchedule() {
  if(server.hasArg("index") && server.hasArg("time")) {
    int index = server.arg("index").toInt();
    String time = server.arg("time");
    if(index >= 0 && index < 3) {
      pens[0].schedules[index] = time;
      Serial.print("Schedule ");
      Serial.print(index);
      Serial.print(" updated to: ");
      Serial.println(time);
    }
  }
  server.send(200, "text/plain", "OK");
}