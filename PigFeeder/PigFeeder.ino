/*
 * 🐷 AUTOMATED PIG FEEDER - ESP8266 IoT PROJECT (v4.4 - Sensor Fix + All Features)
 * ✅ Firebase connection unchanged - Auth layered on top
 * 🔐 Features: Register/Login/Settings/Logout + Isolated user dashboards
 * 📊 Sensor: Real-time feed level with median filter + hysteresis
 * 📱 Phone access: IP printed clearly in Serial Monitor
 */

#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <FirebaseESP8266.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <Servo.h>
#include <time.h>

// ⚙️ CONFIGURATION
const char* ssid = "Anam's Private WiFi Nga Hinay";
const char* password = "anamnop@ssword2025";
#define BOT_TOKEN "8705848716:AAFoQttRz2ELF_SC8nmDR-FoqYUZnJumtJA"
#define CHAT_ID "6121150683"

// 🔥 FIREBASE CONFIG (UNCHANGED)
const char* FIREBASE_API_KEY = "AIzaSyDFmhsNF9haFb7ZBIeZIs7E05AihpsMtWI";
const char* FIREBASE_DATABASE_URL = "https://smartfeeder-74954-default-rtdb.asia-southeast1.firebasedatabase.app";
#define USER_EMAIL "dom@test.com"
#define USER_PASSWORD "domtest123"

// 🔐 AUTH CONFIG
#define SESSION_TIMEOUT 3600
String currentUser = "";
unsigned long sessionStart = 0;
const char* USERS_PATH = "/users";
const char* USERDATA_PATH = "/userdata";

// 🎛️ HARDWARE
#define SERVO_PIN D4
#define TRIG_PIN D5
#define ECHO_PIN D6

// ⚙️ FEEDING PARAMETERS
Servo myServo;
const int closeAngle = 60;
const int openAngle = 180;
const int gramsPerCycle = 100;
const unsigned long firstCycleTime = 990;
const unsigned long nextCycleTime = 760;
const unsigned long pauseBetween = 3000;

// 📏 SENSOR CONFIG
const int TANK_MAX_CM = 25;
const int TANK_MIN_CM = 5;
const int LOW_FEED_THRESHOLD = 20;
const int SENSOR_OFFSET_CM = 0;  // Calibrate if needed: +2 or -2

// 🕐 TIME CONFIG
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 28800;
const int daylightOffset_sec = 0;

// 📊 STATE
int totalPigs = 0;
long totalGramsConsumed = 0;
int feedLevelPercent = 100;
int cachedDistance = 50;
time_t nextFeedingTime = 0;
bool isDispensing = false;
bool lowFeedAlertSent = false;

// ⏰ SCHEDULE STORAGE (MAX 3 SCHEDULES)
String scheduleTimes[3];
int scheduleGrams[3];
int scheduleCount = 0;
bool scheduleCompleted[3] = {false};
time_t lastResetDay = 0;

// 🌐 SERVER & FIREBASE
ESP8266WebServer server(80);
FirebaseData fb;
FirebaseConfig fbConfig;
FirebaseAuth fbAuth;
unsigned long lastFirebaseSync = 0;
bool firebaseReady = false;

// =====================================================
// 🌐 DASHBOARD HTML (Unchanged)
// =====================================================
const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0,maximum-scale=1.0,user-scalable=no,viewport-fit=cover"><title>🐷 Pig Feeder</title><style>:root{--bg:#0a0a0a;--card:#161616;--card-border:#2a2a2a;--text:#e5e5e5;--text-light:#888;--primary:#10b981;--secondary:#3b82f6;--danger:#ef4444;--nav-bg:#000}*{margin:0;padding:0;box-sizing:border-box;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif}body{background:var(--bg);color:var(--text);padding:12px 12px 60px;max-width:100vw;line-height:1.4}.header{display:flex;justify-content:space-between;align-items:center;margin-bottom:16px;padding:10px 12px;background:var(--card);border:1px solid var(--card-border);border-radius:12px}.header h1{font-size:1.1rem;font-weight:700;color:var(--primary)}.time-box{text-align:right}.time-box .now{font-size:1rem;font-weight:600}.time-box .next{font-size:0.7rem;color:var(--text-light);margin-top:2px}.card{background:var(--card);border:1px solid var(--card-border);border-radius:12px;padding:14px;margin-bottom:12px}.card h3{margin-bottom:12px;font-size:0.9rem;color:var(--text-light);display:flex;align-items:center;gap:6px}.progress-wrap{display:flex;justify-content:center;margin:8px 0}.circle{width:120px;height:120px;position:relative}.circle svg{transform:rotate(-90deg)}.circle-bg{fill:none;stroke:#222;stroke-width:7}.circle-fill{fill:none;stroke:var(--primary);stroke-width:7;stroke-linecap:round;transition:stroke-dashoffset .5s ease,stroke .3s}.circle-text{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);text-align:center}.circle-text .pct{font-size:1.5rem;font-weight:700;color:var(--primary)}.circle-text .lbl{font-size:.65rem;color:var(--text-light);margin-top:1px}.stats{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-top:8px}.stat{background:#1a1a1a;padding:10px;border-radius:10px;text-align:center;border:1px solid var(--card-border)}.stat .val{font-size:1rem;font-weight:600;color:var(--text)}.stat .lbl{font-size:.65rem;color:var(--text-light);margin-top:1px}.status-row{display:flex;justify-content:space-between;align-items:center;padding:8px 0;border-top:1px solid #222;margin-top:10px;font-size:.85rem}.dot{width:7px;height:7px;border-radius:50%;display:inline-block;margin-right:5px;background:var(--primary);box-shadow:0 0 5px var(--primary)}.dot.off{background:var(--danger);box-shadow:0 0 5px var(--danger)}.dot.warn{background:#f59e0b;box-shadow:0 0 5px #f59e0b}.bottom-nav{position:fixed;bottom:0;left:0;width:100%;background:var(--nav-bg);display:flex;justify-content:space-around;padding:6px 0 8px;box-shadow:0 -4px 12px rgba(0,0,0,.6);z-index:100;border-top:1px solid #222}.nav-item{display:flex;flex-direction:column;align-items:center;text-decoration:none;color:var(--text-light);font-size:.6rem;font-weight:500;padding:3px 10px;border-radius:8px;transition:all .2s}.nav-item.active{color:var(--primary)}.nav-item span{font-size:1rem;margin-bottom:0;display:block}@media (max-width:360px){.header h1{font-size:1rem}.circle{width:100px;height:100px}.stat .val{font-size:.9rem}}</style></head><body><div class="header"><h1>🐷 Pig Feeder</h1><div class="time-box"><div class="now" id="currentTime">--:--:--</div><div class="next">Next: <strong id="nextFeed">--:--</strong></div></div></div><div class="card"><h3>📊 Feed Tank Level</h3><div class="progress-wrap"><div class="circle"><svg viewBox="0 0 100 100"><circle class="circle-bg" cx="50" cy="50" r="42"/><circle class="circle-fill" id="feedCircle" cx="50" cy="50" r="42" stroke-dasharray="263.9" stroke-dashoffset="263.9"/></svg><div class="circle-text"><div class="pct" id="feedPercent">100%</div><div class="lbl">Remaining</div></div></div></div></div><div class="card"><h3>📈 Farm Stats</h3><div class="stats"><div class="stat"><div class="val" id="pigCount">0</div><div class="lbl">Pigs</div></div><div class="stat"><div class="val" id="gramsConsumed">0g</div><div class="lbl">Consumed</div></div><div class="stat"><div class="val" id="tankDistance">--</div><div class="lbl">Distance (cm)</div></div><div class="stat"><div class="val" id="wifiSignal">--</div><div class="lbl">Signal (dBm)</div></div></div><div class="status-row"><span>System Status</span><span id="statusText"><span class="dot"></span>Connecting...</span></div></div><nav class="bottom-nav"><a href="/" class="nav-item active"><span>🏠</span>Home</a><a href="/pen" class="nav-item"><span>⚙️</span>Pens</a><a href="/settings" class="nav-item"><span>👤</span>Settings</a></nav><script>const C=263.9;function updateCircle(p){const o=C-(p/100)*C;document.getElementById('feedCircle').style.strokeDashoffset=o;document.getElementById('feedPercent').textContent=p+'%';const col=p<20?'#ef4444':p<40?'#f59e0b':'#10b981';document.getElementById('feedCircle').style.stroke=col;document.getElementById('feedPercent').style.color=col}async function fetchStatus(){try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw new Error('HTTP '+r.status);const d=await r.json();updateCircle(d.feedLevel);document.getElementById('pigCount').textContent=d.pigs;document.getElementById('gramsConsumed').textContent=d.consumed+'g';document.getElementById('currentTime').textContent=d.currentTime;document.getElementById('nextFeed').textContent=d.nextFeeding;document.getElementById('tankDistance').textContent=d.tankDistance;document.getElementById('wifiSignal').textContent=d.rssi;const txt=document.getElementById('statusText');txt.innerHTML=d.online?'<span class="dot"></span>Online':'<span class="dot off"></span>Offline'}catch(e){console.warn('Status fetch failed:',e);document.getElementById('statusText').innerHTML='<span class="dot warn"></span>Syncing...'}}fetchStatus();setInterval(fetchStatus,4000)</script></body></html>
)rawliteral";

// =====================================================
// ⚙️ PEN MANAGEMENT HTML (Unchanged)
// =====================================================
const char PEN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0,maximum-scale=1.0,user-scalable=no,viewport-fit=cover"><title>⚙️ Pen Management</title><style>:root{--bg:#0a0a0a;--card:#161616;--card-border:#2a2a2a;--text:#e5e5e5;--text-light:#888;--primary:#10b981;--secondary:#3b82f6;--danger:#ef4444;--nav-bg:#000}*{margin:0;padding:0;box-sizing:border-box;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif}body{background:var(--bg);color:var(--text);padding:12px 12px 60px;max-width:100vw}.header{display:flex;justify-content:space-between;align-items:center;margin-bottom:16px;padding:10px 12px;background:var(--card);border:1px solid var(--card-border);border-radius:12px}.header h2{font-size:1.1rem;font-weight:700;color:var(--secondary)}.header a{color:var(--text-light);text-decoration:none;font-size:.85rem}.card{background:var(--card);border:1px solid var(--card-border);border-radius:12px;padding:14px;margin-bottom:12px}.card h3{margin-bottom:10px;font-size:.9rem;color:var(--text-light)}.input-row{display:flex;gap:8px;margin-bottom:10px;flex-wrap:wrap}input{flex:1;min-width:0;padding:10px;border:1px solid var(--card-border);border-radius:10px;font-size:.9rem;background:#1a1a1a;color:var(--text)}input:focus{outline:none;border-color:var(--secondary)}button{padding:10px 14px;background:var(--secondary);color:#fff;border:none;border-radius:10px;font-weight:600;cursor:pointer;font-size:.85rem}button:active{transform:scale(.98);opacity:.9}.list{display:flex;flex-direction:column;gap:8px}.item{display:flex;justify-content:space-between;align-items:center;padding:10px;background:#1a1a1a;border-radius:10px;border:1px solid var(--card-border)}.tag{background:#1e3a5f;color:#3b82f6;padding:5px 10px;border-radius:15px;font-weight:500;font-size:.8rem}.badge{background:#164e38;color:#10b981;padding:4px 8px;border-radius:6px;font-size:.7rem;font-weight:600;margin-left:8px}.del{background:#3f1a1a;color:var(--danger);padding:6px 10px;border-radius:8px;font-size:.75rem;border:none}.empty{text-align:center;color:var(--text-light);padding:16px;font-style:italic;font-size:.85rem}.calc-note{font-size:.7rem;color:var(--text-light);margin-top:6px}.bottom-nav{position:fixed;bottom:0;left:0;width:100%;background:var(--nav-bg);display:flex;justify-content:space-around;padding:6px 0 8px;box-shadow:0 -4px 12px rgba(0,0,0,.6);z-index:100;border-top:1px solid #222}.nav-item{display:flex;flex-direction:column;align-items:center;text-decoration:none;color:var(--text-light);font-size:.6rem;font-weight:500;padding:3px 10px}.nav-item.active{color:var(--secondary)}.nav-item span{font-size:1rem;margin-bottom:0;display:block}@media (max-width:360px){.input-row{gap:6px}input{padding:9px}button{padding:9px 12px}}</style></head><body><div class="header"><h2>⚙️ Pen Management</h2><a href="/">← Home</a></div><div class="card"><h3>🐷 Total Pigs</h3><div class="input-row"><input type="number" id="pigCountInput" value="0" min="0" max="200"><button onclick="updatePigs()">Update</button></div><div class="calc-note">Used for automatic feed calculation</div></div><div class="card"><h3>⏰ Feeding Schedule</h3><div class="input-row"><input type="time" id="feedTime" value="08:00" style="flex:1.2"><input type="number" id="feedGrams" value="100" min="50" max="1000" step="50" placeholder="g/pig" style="flex:0.8"><button onclick="addSchedule()">Add</button></div><div class="calc-note">Total dispensed = (Grams × Total Pigs) per schedule</div><div class="list" id="scheduleList" style="margin-top:12px"><div class="empty">No schedules set</div></div></div><nav class="bottom-nav"><a href="/" class="nav-item"><span>🏠</span>Home</a><a href="/pen" class="nav-item active"><span>⚙️</span>Pens</a><a href="/settings" class="nav-item"><span>👤</span>Settings</a></nav><script>
let pigs = 0, schedules = [];
async function loadData(){ 
  try{ 
    const r = await fetch('/api/status'); 
    const d = await r.json(); 
    pigs = d.pigs; 
    document.getElementById('pigCountInput').value = pigs;
    if(d.schedules && Array.isArray(d.schedules)) {
      schedules = d.schedules.map(s => ({time: s.time, grams: s.grams}));
    }
    if(d.scheduleCompleted) window.scheduleStatus = d.scheduleCompleted;
    render(); 
  } catch(e) { console.warn('Load failed:', e); }
}
function render(){
  const list = document.getElementById('scheduleList');
  if(schedules.length === 0){ list.innerHTML = '<div class="empty">No schedules set</div>'; return; }
  list.innerHTML = schedules.map((s,i)=>{
    const total = s.grams * pigs;
    const isDone = window.scheduleStatus && window.scheduleStatus[i] === true;
    const badge = isDone ? '<span class="badge">✓ Completed</span>' : '';
    return `<div class="item"><div><span class="tag">${s.time}</span><br><span style="color:#10b981;font-size:.8rem">${s.grams}g/pig → ${total}g total</span>${badge}</div><button class="del" onclick="removeSchedule(${i})">✕</button></div>`;
  }).join('');
}
async function updatePigs(){ 
  const v = parseInt(document.getElementById('pigCountInput').value); 
  if(isNaN(v)||v<0) return; 
  try{ await fetch('/api/pigs/set?count='+v); pigs = v; render(); } catch(e){} 
}
async function addSchedule(){ 
  const t = document.getElementById('feedTime').value, g = parseInt(document.getElementById('feedGrams').value); 
  if(!t || g < 50) return alert('Invalid time/amount'); 
  try{ 
    const r = await fetch(`/api/schedule/add?time=${encodeURIComponent(t)}&grams=${g}`); 
    const res = await r.json();
    if(res.status === 'scheduled'){ 
      schedules.push({time:t, grams:g}); 
      schedules.sort((a,b)=>a.time.localeCompare(b.time)); 
      render(); 
    } else if(res.error) {
      alert(res.error);
    }
  } catch(e){ alert('Network error'); }
}
async function removeSchedule(i){ 
  if(confirm('Delete '+schedules[i].time+'?')){ 
    await fetch('/api/schedule/remove?time='+encodeURIComponent(schedules[i].time)); 
    schedules.splice(i,1); 
    render(); 
  } 
}
setInterval(async ()=>{
  try{
    const r = await fetch('/api/status',{cache:'no-store'});
    const d = await r.json();
    if(d.scheduleCompleted) {
      window.scheduleStatus = d.scheduleCompleted;
      render();
    }
  } catch(e){}
}, 5000);
document.addEventListener('DOMContentLoaded', loadData);
</script></body></html>
)rawliteral";

// =====================================================
// 🔐 LOGIN HTML (Unchanged)
// =====================================================
const char LOGIN_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>🔐 Login</title><style>:root{--bg:#0a0a0a;--card:#161616;--card-border:#2a2a2a;--text:#e5e5e5;--primary:#10b981;--secondary:#3b82f6}*{margin:0;padding:0;box-sizing:border-box;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif}body{background:var(--bg);color:var(--text);display:flex;align-items:center;justify-content:center;min-height:100vh;padding:20px}.card{background:var(--card);border:1px solid var(--card-border);border-radius:16px;padding:24px;width:100%;max-width:360px}.card h2{margin-bottom:20px;text-align:center;color:var(--primary)}.input-group{margin-bottom:16px}.input-group label{display:block;margin-bottom:6px;font-size:.85rem;color:var(--text-light)}.input-group input{width:100%;padding:12px;border:1px solid var(--card-border);border-radius:10px;background:#1a1a1a;color:var(--text);font-size:1rem}.input-group input:focus{outline:none;border-color:var(--secondary)}button{width:100%;padding:12px;background:var(--secondary);color:#fff;border:none;border-radius:10px;font-weight:600;cursor:pointer;font-size:1rem;margin-top:8px}button:active{transform:scale(.98)}.links{margin-top:16px;text-align:center;font-size:.85rem}.links a{color:var(--secondary);text-decoration:none}.error{background:#3f1a1a;color:#ef4444;padding:10px;border-radius:8px;margin-bottom:16px;font-size:.85rem;display:none}</style></head><body><div class="card"><h2>🐷 Pig Feeder</h2><div class="error" id="error"></div><form id="loginForm"><div class="input-group"><label>Username</label><input type="text" id="username" required autocomplete="username"></div><div class="input-group"><label>Password</label><input type="password" id="password" required autocomplete="current-password"></div><button type="submit">Login</button></form><div class="links"><a href="/register">Create new account</a></div></div><script>document.getElementById('loginForm').addEventListener('submit',async e=>{e.preventDefault();const u=document.getElementById('username').value,p=document.getElementById('password').value;try{const r=await fetch('/api/auth/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({username:u,password:p})});const d=await r.json();if(d.status==='ok'){window.location.href='/';}else{document.getElementById('error').textContent=d.error||'Login failed';document.getElementById('error').style.display='block';}}catch(e){document.getElementById('error').textContent='Connection error';document.getElementById('error').style.display='block'}})</script></body></html>
)rawliteral";

// =====================================================
// 📝 REGISTER HTML (Unchanged)
// =====================================================
const char REGISTER_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>📝 Register</title><style>:root{--bg:#0a0a0a;--card:#161616;--card-border:#2a2a2a;--text:#e5e5e5;--primary:#10b981;--secondary:#3b82f6}*{margin:0;padding:0;box-sizing:border-box;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif}body{background:var(--bg);color:var(--text);display:flex;align-items:center;justify-content:center;min-height:100vh;padding:20px}.card{background:var(--card);border:1px solid var(--card-border);border-radius:16px;padding:24px;width:100%;max-width:360px}.card h2{margin-bottom:20px;text-align:center;color:var(--primary)}.input-group{margin-bottom:16px}.input-group label{display:block;margin-bottom:6px;font-size:.85rem;color:var(--text-light)}.input-group input{width:100%;padding:12px;border:1px solid var(--card-border);border-radius:10px;background:#1a1a1a;color:var(--text);font-size:1rem}.input-group input:focus{outline:none;border-color:var(--secondary)}button{width:100%;padding:12px;background:var(--primary);color:#fff;border:none;border-radius:10px;font-weight:600;cursor:pointer;font-size:1rem;margin-top:8px}button:active{transform:scale(.98)}.links{margin-top:16px;text-align:center;font-size:.85rem}.links a{color:var(--secondary);text-decoration:none}.error{background:#3f1a1a;color:#ef4444;padding:10px;border-radius:8px;margin-bottom:16px;font-size:.85rem;display:none}</style></head><body><div class="card"><h2>🐷 Create Account</h2><div class="error" id="error"></div><form id="registerForm"><div class="input-group"><label>Username</label><input type="text" id="username" required minlength="3" maxlength="20" pattern="[a-zA-Z0-9_]+" title="Letters, numbers, underscores only"></div><div class="input-group"><label>Password</label><input type="password" id="password" required minlength="6"></div><div class="input-group"><label>Confirm Password</label><input type="password" id="confirm" required></div><button type="submit">Register</button></form><div class="links"><a href="/login">Already have an account? Login</a></div></div><script>document.getElementById('registerForm').addEventListener('submit',async e=>{e.preventDefault();const u=document.getElementById('username').value,p=document.getElementById('password').value,c=document.getElementById('confirm').value;if(p!==c){document.getElementById('error').textContent='Passwords do not match';document.getElementById('error').style.display='block';return}try{const r=await fetch('/api/auth/register',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({username:u,password:p})});const d=await r.json();if(d.status==='ok'){window.location.href='/login';}else{document.getElementById('error').textContent=d.error||'Registration failed';document.getElementById('error').style.display='block';}}catch(e){document.getElementById('error').textContent='Connection error';document.getElementById('error').style.display='block'}})</script></body></html>
)rawliteral";

// =====================================================
// ⚙️ SETTINGS HTML (Unchanged)
// =====================================================
const char SETTINGS_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="en"><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0"><title>⚙️ Settings</title><style>:root{--bg:#0a0a0a;--card:#161616;--card-border:#2a2a2a;--text:#e5e5e5;--text-light:#888;--primary:#10b981;--secondary:#3b82f6;--danger:#ef4444;--nav-bg:#000}*{margin:0;padding:0;box-sizing:border-box;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif}body{background:var(--bg);color:var(--text);padding:12px 12px 60px;max-width:100vw}.header{display:flex;justify-content:space-between;align-items:center;margin-bottom:16px;padding:10px 12px;background:var(--card);border:1px solid var(--card-border);border-radius:12px}.header h2{font-size:1.1rem;font-weight:700;color:var(--secondary)}.card{background:var(--card);border:1px solid var(--card-border);border-radius:12px;padding:14px;margin-bottom:12px}.card h3{margin-bottom:12px;font-size:.9rem;color:var(--text-light)}.user-info{display:flex;align-items:center;gap:12px}.avatar{width:48px;height:48px;border-radius:50%;background:var(--secondary);display:flex;align-items:center;justify-content:center;font-weight:700;font-size:1.2rem}.info div:first-child{font-weight:600}.info div:last-child{font-size:.8rem;color:var(--text-light)}.btn-logout{margin-top:16px;width:100%;padding:12px;background:var(--danger);color:#fff;border:none;border-radius:10px;font-weight:600;cursor:pointer;font-size:1rem}.btn-logout:active{transform:scale(.98)}.bottom-nav{position:fixed;bottom:0;left:0;width:100%;background:var(--nav-bg);display:flex;justify-content:space-around;padding:6px 0 8px;box-shadow:0 -4px 12px rgba(0,0,0,.6);z-index:100;border-top:1px solid #222}.nav-item{display:flex;flex-direction:column;align-items:center;text-decoration:none;color:var(--text-light);font-size:.6rem;font-weight:500;padding:3px 10px}.nav-item.active{color:var(--secondary)}.nav-item span{font-size:1rem;margin-bottom:0;display:block}</style></head><body><div class="header"><h2>⚙️ Settings</h2><a href="/">← Home</a></div><div class="card"><h3>👤 Account Info</h3><div class="user-info"><div class="avatar" id="avatar">U</div><div class="info"><div id="username">Loading...</div><div id="memberSince">Member since: --</div></div></div></div><button class="btn-logout" onclick="logout()">🚪 Logout</button><nav class="bottom-nav"><a href="/" class="nav-item"><span>🏠</span>Home</a><a href="/pen" class="nav-item"><span>⚙️</span>Pens</a><a href="/settings" class="nav-item active"><span>👤</span>Settings</a></nav><script>async function loadUser(){try{const r=await fetch('/api/auth/me');const d=await r.json();if(d.username){document.getElementById('username').textContent='@'+d.username;document.getElementById('avatar').textContent=d.username.charAt(0).toUpperCase();document.getElementById('memberSince').textContent='Member since: '+new Date(d.createdAt * 1000).toLocaleDateString();}else{window.location.href='/login';}}catch(e){window.location.href='/login';}}async function logout(){try{await fetch('/api/auth/logout');window.location.href='/login';}catch(e){window.location.href='/login';}}document.addEventListener('DOMContentLoaded',loadUser);</script></body></html>
)rawliteral";

// =====================================================
// 🔐 AUTHENTICATION HELPERS (Unchanged)
// =====================================================
String simpleHash(String input) {
  unsigned long hash = 5381;
  for (char c : input) {
    hash = ((hash << 5) + hash) + c;
  }
  return String(hash, HEX);
}

bool isAuthenticated() {
  if (currentUser == "") return false;
  if (millis() - sessionStart > SESSION_TIMEOUT * 1000) {
    currentUser = "";
    return false;
  }
  return true;
}

bool initUserData(String username) {
  FirebaseJson json;
  json.set("pigs", 0);
  json.set("consumed", 0);
  json.set("feedLevel", 100);
  json.set("tankDistance", 50);
  FirebaseJsonArray arr;
  json.set("schedules", arr);
  json.set("createdAt", time(nullptr));
  
  String path = String(USERDATA_PATH) + "/" + username;
  if (Firebase.setJSON(fb, path.c_str(), json)) {
    Serial.printf("✅ Initialized data for user: %s\n", username.c_str());
    return true;
  }
  Serial.printf("❌ Failed to init user  %s\n", fb.errorReason().c_str());
  return false;
}

bool loadUserData(String username) {
  String path = String(USERDATA_PATH) + "/" + username;
  if (Firebase.getJSON(fb, path.c_str())) {
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, fb.jsonString());

    if (doc.containsKey("pigs")) totalPigs = doc["pigs"].as<int>();
    if (doc.containsKey("consumed")) totalGramsConsumed = doc["consumed"].as<long>();
    if (doc.containsKey("feedLevel")) feedLevelPercent = doc["feedLevel"].as<int>();
    if (doc.containsKey("tankDistance")) cachedDistance = doc["tankDistance"].as<int>();

    scheduleCount = 0;
    for (int i = 0; i < 3; i++) {
      scheduleTimes[i] = "";
      scheduleGrams[i] = 0;
      scheduleCompleted[i] = false;
    }

    if (doc.containsKey("schedules")) {
      JsonArray arr = doc["schedules"].as<JsonArray>();
      for (JsonObject s : arr) {
        if (scheduleCount >= 3) break;
        scheduleTimes[scheduleCount] = s["time"].as<String>();
        scheduleGrams[scheduleCount] = s["grams"].as<int>();
        scheduleCompleted[scheduleCount] = false;
        scheduleCount++;
      }
    }
    
    Serial.printf("✅ Loaded data for user: %s (pigs:%d, schedules:%d)\n", username.c_str(), totalPigs, scheduleCount);
    return true;
  }
  Serial.printf("⚠️ Could not load user data: %s\n", fb.errorReason().c_str());
  return false;
}

bool saveUserData(String username) {
  FirebaseJson json;
  json.set("pigs", totalPigs);
  json.set("consumed", totalGramsConsumed);
  json.set("feedLevel", feedLevelPercent);
  json.set("tankDistance", cachedDistance);
  
  FirebaseJsonArray arr;
  for (int i = 0; i < scheduleCount; i++) {
    FirebaseJson sched;
    sched.set("time", scheduleTimes[i]);
    sched.set("grams", scheduleGrams[i]);
    arr.add(sched);
  }
  json.set("schedules", arr);
  json.set("updatedAt", time(nullptr));
  
  String path = String(USERDATA_PATH) + "/" + username;
  if (Firebase.setJSON(fb, path.c_str(), json)) {
    return true;
  }
  Serial.printf("❌ Failed to save user  %s\n", fb.errorReason().c_str());
  return false;
}

// =====================================================
// 🔐 AUTH API HANDLERS (Unchanged)
// =====================================================
void handleLogin() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":\"Method not allowed\"}");
    return;
  }

  String body = server.arg("plain");
  DynamicJsonDocument doc(256);
  deserializeJson(doc, body);

  String username = doc["username"].as<String>();
  String password = doc["password"].as<String>();

  if (username.length() < 3 || password.length() < 6) {
    server.send(400, "application/json", "{\"error\":\"Invalid credentials\"}");
    return;
  }

  String userPath = String(USERS_PATH) + "/" + username;
  if (Firebase.getJSON(fb, userPath.c_str())) {
    DynamicJsonDocument userDoc(512);
    deserializeJson(userDoc, fb.jsonString());

    String storedHash = userDoc["passwordHash"].as<String>();
    if (storedHash == simpleHash(password)) {
      currentUser = username;
      sessionStart = millis();
      loadUserData(username);
      server.send(200, "application/json", "{\"status\":\"ok\",\"username\":\"" + username + "\"}");
      Serial.printf("✅ User logged in: %s\n", username.c_str());
      return;
    }
  }
  server.send(401, "application/json", "{\"error\":\"Invalid username or password\"}");
}

void handleRegister() {
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":\"Method not allowed\"}");
    return;
  }

  if (!firebaseReady) {
    Serial.println("❌ Register failed: Firebase not ready");
    server.send(503, "application/json", "{\"error\":\"Service unavailable - Firebase not connected\"}");
    return;
  }

  String body = server.arg("plain");
  Serial.println("📝 Registration body: " + body);

  DynamicJsonDocument doc(256);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.println("❌ JSON parse error: " + String(err.c_str()));
    server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
    return;
  }

  String username = doc["username"].as<String>();
  String password = doc["password"].as<String>();

  Serial.println("👤 Attempting to register: " + username);

  if (username.length() < 3 || username.length() > 20) {
    server.send(400, "application/json", "{\"error\":\"Username must be 3-20 chars\"}");
    return;
  }
  for(char c : username) {
    if(!isalnum(c) && c != '_') {
      server.send(400, "application/json", "{\"error\":\"Username: letters/numbers/underscores only\"}");
      return;
    }
  }
  if (password.length() < 6) {
    server.send(400, "application/json", "{\"error\":\"Password must be at least 6 characters\"}");
    return;
  }

  String userPath = String(USERS_PATH) + "/" + username;
  Serial.println("🔍 Checking if user exists at: " + userPath);
  
  if (Firebase.getJSON(fb, userPath.c_str())) {
    Serial.println("❌ User already exists: " + username);
    server.send(409, "application/json", "{\"error\":\"Username already exists\"}");
    return;
  }

  Serial.println(" Creating user account...");
  FirebaseJson json;
  json.set("username", username);
  json.set("passwordHash", simpleHash(password));
  json.set("createdAt", time(nullptr));

  if (!Firebase.setJSON(fb, userPath.c_str(), json)) {
    Serial.println("❌ Failed to create user: " + fb.errorReason());
    server.send(500, "application/json", "{\"error\":\"Failed to create account: " + fb.errorReason() + "\"}");
    return;
  }

  Serial.println("📊 Initializing user data...");
  if (!initUserData(username)) {
    Serial.println("❌ Failed to init user  " + fb.errorReason());
    server.send(500, "application/json", "{\"error\":\"Account created but data init failed\"}");
    return;
  }

  Serial.println("✅ User registered successfully: " + username);
  server.send(201, "application/json", "{\"status\":\"ok\"}");
}

void handleLogout() {
  currentUser = "";
  sessionStart = 0;
  server.sendHeader("Location", "/login");
  server.send(302, "text/plain", "Logged out");
  Serial.println("🚪 User logged out");
}

void handleGetMe() {
  if (!isAuthenticated()) {
    server.send(401, "application/json", "{\"error\":\"Not authenticated\"}");
    return;
  }

  String userPath = String(USERS_PATH) + "/" + currentUser;
  if (Firebase.getJSON(fb, userPath.c_str())) {
    DynamicJsonDocument userDoc(256);
    deserializeJson(userDoc, fb.jsonString());

    String resp = "{\"username\":\"" + userDoc["username"].as<String>() + "\",\"createdAt\":" + String(userDoc["createdAt"].as<int>()) + "}";
    server.send(200, "application/json", resp);
    return;
  }
  server.send(500, "application/json", "{\"error\":\"Failed to fetch user\"}");
}

// =====================================================
// 🔄 API HANDLERS (With Auth + Per-User Data)
// =====================================================
void handleStatus() {
  if (!isAuthenticated()) {
    server.sendHeader("Location", "/login");
    server.send(302, "text/plain", "Please login");
    return;
  }
  
  static unsigned long lastSync = 0;
  if (millis() - lastSync > 5000 && firebaseReady) {
    lastSync = millis();
    loadUserData(currentUser);
  }
  
  String json = F("{\"feedLevel\":");
  json += feedLevelPercent;
  json += F(",\"tankDistance\":");
  json += cachedDistance;
  json += F(",\"pigs\":");
  json += totalPigs;
  json += F(",\"consumed\":");
  json += totalGramsConsumed;
  json += F(",\"currentTime\":\"");
  json += getCurrentTime();
  json += F("\",\"nextFeeding\":\"");
  json += getNextFeedingTimeDisplay();
  json += F("\",\"online\":");
  json += (WiFi.status() == WL_CONNECTED ? "true" : "false");
  json += F(",\"dispensing\":");
  json += (isDispensing ? "true" : "false");
  json += F(",\"rssi\":");
  json += WiFi.RSSI();
  json += F(",\"firebaseConnected\":");
  json += (firebaseReady ? "true" : "false");
  
  json += F(",\"scheduleCompleted\":[");
  for(int i = 0; i < scheduleCount; i++) {
    if(i > 0) json += ',';
    json += scheduleCompleted[i] ? "true" : "false";
  }
  json += F("]");
  
  json += F(",\"schedules\":[");
  for(int i = 0; i < scheduleCount; i++) {
    if(i > 0) json += ',';
    json += F("{\"time\":\"");
    json += scheduleTimes[i];
    json += F("\",\"grams\":");
    json += scheduleGrams[i];
    json += F("}");
  }
  json += F("]}");
  
  server.send(200, "application/json", json);
}

void handleSetPigs() {
  if (!isAuthenticated()) { server.send(401, "application/json", "{\"error\":\"Not authenticated\"}"); return; }
  int c = server.arg("count").toInt();
  totalPigs = max(0, c);
  if (firebaseReady) saveUserData(currentUser);
  Serial.printf("🐷 Pigs updated: %d (user: %s)\n", totalPigs, currentUser.c_str());
  server.send(200, "application/json", "{\"status\":\"updated\",\"pigs\":" + String(totalPigs) + "}");
}

void handleSetSchedule() {
  if (!isAuthenticated()) { server.send(401, "application/json", "{\"error\":\"Not authenticated\"}"); return; }
  String t = server.arg("time");
  int g = server.arg("grams").toInt();
  
  if (t.length() != 5 || g < 50 || g > 1000) { 
    server.send(400, "application/json", "{\"error\":\"Invalid time or amount (50-1000g)\"}"); 
    return; 
  }
  
  for (int i = 0; i < scheduleCount; i++) {
    if (scheduleTimes[i] == t) { 
      scheduleGrams[i] = g; 
      if (firebaseReady) saveUserData(currentUser);
      server.send(200, "application/json", "{\"status\":\"updated\"}"); 
      return; 
    }
  }
  
  if (scheduleCount >= 3) { 
    server.send(507, "application/json", "{\"error\":\"Maximum 3 schedules allowed\"}"); 
    return; 
  }
  
  scheduleTimes[scheduleCount] = t;
  scheduleGrams[scheduleCount] = g;
  scheduleCount++;
  
  if (firebaseReady) saveUserData(currentUser);
  Serial.printf("⏰ Added: %s | %dg/pig (user: %s)\n", t.c_str(), g, currentUser.c_str());
  server.send(200, "application/json", "{\"status\":\"scheduled\"}");
}

void handleRemoveSchedule() {
  if (!isAuthenticated()) { server.send(401, "application/json", "{\"error\":\"Not authenticated\"}"); return; }
  String t = server.arg("time");
  for (int i = 0; i < scheduleCount; i++) {
    if (scheduleTimes[i] == t) {
      for (int j = i; j < scheduleCount - 1; j++) { 
        scheduleTimes[j] = scheduleTimes[j+1]; 
        scheduleGrams[j] = scheduleGrams[j+1];
        scheduleCompleted[j] = scheduleCompleted[j+1];
      }
      scheduleCount--;
      if (firebaseReady) saveUserData(currentUser);
      server.send(200, "application/json", "{\"status\":\"removed\"}"); 
      return;
    }
  }
  server.send(404, "application/json", "{\"error\":\"Not found\"}");
}

// =====================================================
// 🔧 SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n🐷 Pig Feeder v4.4 Starting (Sensor Fix + All Features)...");
  
  myServo.attach(SERVO_PIN);
  myServo.write(closeAngle);
  delay(500);  // ✅ Give servo time to initialize
  
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  connectWiFi();
  initNTP();
  setupFirebase();
  
  Serial.println("⏳ Waiting for Firebase connection...");
  unsigned long start = millis();
  while (!firebaseReady && millis() - start < 10000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(firebaseReady ? "\n✅ Firebase ready!" : "\n❌ Firebase timeout");
  
  nextFeedingTime = time(nullptr) + (6 * 3600);
  Serial.printf("⏰ Next feeding: %s\n", getNextFeedingTimeDisplay().c_str());
  
  // 🔐 AUTH ROUTES
  server.on("/login", HTTP_GET, []() { 
    if (isAuthenticated()) { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); }
    else { server.send_P(200, "text/html", LOGIN_HTML); }
  });
  server.on("/register", HTTP_GET, []() { 
    if (isAuthenticated()) { server.sendHeader("Location", "/"); server.send(302, "text/plain", ""); }
    else { server.send_P(200, "text/html", REGISTER_HTML); }
  });
  server.on("/settings", HTTP_GET, []() { 
    if (!isAuthenticated()) { server.sendHeader("Location", "/login"); server.send(302, "text/plain", ""); }
    else { server.send_P(200, "text/html", SETTINGS_HTML); }
  });
  
  server.on("/api/auth/login", HTTP_POST, handleLogin);
  server.on("/api/auth/register", HTTP_POST, handleRegister);
  server.on("/api/auth/logout", HTTP_GET, handleLogout);
  server.on("/api/auth/me", HTTP_GET, handleGetMe);
  
  // 🏠 MAIN ROUTES
  server.on("/", HTTP_GET, []() { 
    if (!isAuthenticated()) { server.sendHeader("Location", "/login"); server.send(302, "text/plain", ""); }
    else { server.send_P(200, "text/html", DASHBOARD_HTML); }
  });
  server.on("/pen", HTTP_GET, []() { 
    if (!isAuthenticated()) { server.sendHeader("Location", "/login"); server.send(302, "text/plain", ""); }
    else { server.send_P(200, "text/html", PEN_HTML); }
  });
  
  // 📡 API ROUTES
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/pigs/set", HTTP_GET, handleSetPigs);
  server.on("/api/schedule/add", HTTP_GET, handleSetSchedule);
  server.on("/api/schedule/remove", HTTP_GET, handleRemoveSchedule);
  
  server.begin();
  
  // 📱 PRINT IP FOR PHONE ACCESS
  String ip = WiFi.localIP().toString();
  Serial.println("\n📱 PHONE ACCESS INSTRUCTIONS:");
  Serial.println("================================");
  Serial.println("1. Connect phone to SAME WiFi: " + String(ssid));
  Serial.println("2. Open browser on phone");
  Serial.println("3. Go to: http://" + ip);
  Serial.println("4. If not working, try: http://" + ip + ":80");
  Serial.println("5. Disable mobile data temporarily");
  Serial.println("================================\n");
  
  Serial.println("✅ Server: http://" + ip);
  
  sendTelegramAlert("✅ *Pig Feeder ONLINE* v4.4\n📍 IP: `" + ip + "`\n🔐 Auth enabled\n📊 Sensor: Real-time updates");
  
  Serial.println("🎉 Ready with all features!");
}

// =====================================================
// 🔄 MAIN LOOP
// =====================================================
void loop() {
  server.handleClient();
  yield();

  static unsigned long lastScheduleCheck = 0;
  if (millis() - lastScheduleCheck >= 2000) {
    lastScheduleCheck = millis();
    checkScheduledFeeding();
  }

  // ✅ SENSOR: Update every 2 seconds (was 10s) for real-time feed level
  static unsigned long lastSensorUpdate = 0;
  if (millis() - lastSensorUpdate >= 2000) {
    lastSensorUpdate = millis();
    updateFeedLevel();  // monitorFeedLevel() is now inside updateFeedLevel()
  }

  if (firebaseReady && millis() - lastFirebaseSync >= 30000) {
    lastFirebaseSync = millis();
    syncStatusToFirebase();
  }
  
  static unsigned long lastDayCheck = 0;
  if (millis() - lastDayCheck >= 60000) {
    lastDayCheck = millis();
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    struct tm todayStart = *t;
    todayStart.tm_hour = 0; todayStart.tm_min = 0; todayStart.tm_sec = 0;
    time_t todayMidnight = mktime(&todayStart);
    
    if (todayMidnight != lastResetDay) {
      for(int j = 0; j < 3; j++) scheduleCompleted[j] = false;
      lastResetDay = todayMidnight;
      Serial.println("🌅 New day: schedule completions reset");
    }
  }

  static unsigned long lastDataSave = 0;
  if (isAuthenticated() && firebaseReady && millis() - lastDataSave >= 60000) {
    lastDataSave = millis();
    saveUserData(currentUser);
    Serial.printf("💾 Auto-saved data for: %s\n", currentUser.c_str());
  }

  static unsigned long lastConnCheck = 0;
  if (millis() - lastConnCheck >= 15000) {
    lastConnCheck = millis();
    
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("🔄 WiFi lost, reconnecting...");
      WiFi.disconnect();
      delay(100);
      WiFi.begin(ssid, password);
      unsigned long start = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(500);
        Serial.print(".");
      }
      Serial.println(WiFi.status() == WL_CONNECTED ? "\n✅ Reconnected" : "\n❌ Reconnect failed");
    }
    
    if (firebaseReady && !Firebase.ready()) {
      Serial.println("🔄 Firebase lost, re-initializing...");
      Firebase.begin(&fbConfig, &fbAuth);
      delay(3000);
      firebaseReady = Firebase.ready();
    }
  }
}

// =====================================================
// 🔥 FIREBASE FUNCTIONS (UNCHANGED)
// =====================================================
void setupFirebase() {
  Serial.print("🔥 Connecting to Firebase...");
  Serial.print("\n   🔑 API Key Length: "); Serial.println(strlen(FIREBASE_API_KEY));
  Serial.print("   🔗 DB URL: "); Serial.println(FIREBASE_DATABASE_URL);
  
  if (strlen(FIREBASE_API_KEY) < 10 || strlen(FIREBASE_DATABASE_URL) < 15) {
    Serial.println("\n❌ Credentials too short!");
    firebaseReady = false;
    return;
  }

  fbConfig.api_key = FIREBASE_API_KEY;
  fbConfig.database_url = FIREBASE_DATABASE_URL;
  fbAuth.user.email = USER_EMAIL;
  fbAuth.user.password = USER_PASSWORD;

  fbConfig.token_status_callback = [](TokenInfo info) {
    if (info.status == token_status_ready) {
      firebaseReady = true;
      Serial.println("✅ Firebase Auth Ready");
    }
  };

  Firebase.begin(&fbConfig, &fbAuth);
  Firebase.setReadTimeout(fb, 60000);
  Firebase.setwriteSizeLimit(fb, "tiny");
  delay(3000);

  if (Firebase.ready()) {
    firebaseReady = true;
    Serial.println("✅ Firebase Connected");
  } else {
    Serial.printf("\n⚠️ Firebase Error: %s\n", fb.errorReason().c_str());
  }
}

void syncStatusToFirebase() {
  FirebaseJson json;
  json.set("pigs", totalPigs);
  json.set("feedLevel", feedLevelPercent);
  json.set("consumed", totalGramsConsumed);
  json.set("tankDistance", cachedDistance);
  json.set("rssi", WiFi.RSSI());
  json.set("lastUpdate", getCurrentTime());
  
  if (Firebase.setJSON(fb, "/status/current", json)) {
    Serial.println("✅ Firebase status synced");
  } else {
    Serial.printf("❌ Firebase sync failed: %s\n", fb.errorReason().c_str());
  }
}

void logFeedingToFirebase(String feedTime, int pigs, int grams) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi disconnected, skipping Firebase log");
    return;
  }
  
  if (!Firebase.ready()) {
    Serial.println("⚠️ Firebase not ready, attempting re-init...");
    Firebase.begin(&fbConfig, &fbAuth);
    delay(1000);
    if (!Firebase.ready()) {
      Serial.println("❌ Firebase log failed: still not ready");
      return;
    }
  }
  
  FirebaseJson json;
  json.set("time", feedTime);
  json.set("pigs", pigs);
  json.set("grams", grams);
  json.set("status", "completed");
  json.set("timestamp", millis());
  
  String path = "/logs/feeding/" + String(millis());
  if (Firebase.pushJSON(fb, path.c_str(), json)) {
    Serial.println("✅ Feeding log pushed to Firebase");
  } else {
    Serial.printf("❌ Firebase log failed: %s\n", fb.errorReason().c_str());
  }
}

// =====================================================
// 📡 WiFi & NTP (UNCHANGED)
// =====================================================
void connectWiFi() {
  Serial.print("📡 Connecting to WiFi...");
  WiFi.begin(ssid, password);
  WiFi.mode(WIFI_STA);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) { delay(500); Serial.print("."); }
  if (WiFi.status() == WL_CONNECTED) Serial.printf("\n✅ IP: %s | RSSI: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  else Serial.println("\n❌ WiFi Failed");
}

void initNTP() {
  Serial.print("🕐 Syncing NTP...");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  struct tm timeInfo;
  unsigned long start = millis();
  while (!getLocalTime(&timeInfo) && millis() - start < 15000) { delay(500); Serial.print("."); }
  Serial.println(getLocalTime(&timeInfo) ? "\n✅ Time synced" : "\n⚠️ Time sync failed");
}

String getCurrentTime() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char b[9]; strftime(b, sizeof(b), "%H:%M:%S", t); return String(b);
}

String getNextFeedingTimeDisplay() {
  struct tm* t = localtime(&nextFeedingTime);
  char b[6]; strftime(b, sizeof(b), "%H:%M", t); return String(b);
}

// =====================================================
// ⏰ SCHEDULE CHECKER (✅ FIXED: Save consumed immediately)
// =====================================================
void checkScheduledFeeding() {
  if (isDispensing || scheduleCount == 0) return;
  
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char nowStr[6];
  strftime(nowStr, sizeof(nowStr), "%H:%M", t);
  String current = String(nowStr);
  
  static String lastTrigger = "";
  for (int i = 0; i < scheduleCount; i++) {
    if (scheduleTimes[i] == current && lastTrigger != current) {
      int pigsToFeed = max(1, totalPigs);
      int totalGrams = scheduleGrams[i] * pigsToFeed;
      
      Serial.printf("⏰ TRIGGERED: %s | %dg/pig × %d pigs = %dg total\n", scheduleTimes[i].c_str(), scheduleGrams[i], totalPigs, totalGrams);
      
      dispense(totalGrams);
      totalGramsConsumed += totalGrams;
      
      // ✅ CRITICAL FIX: Save to Firebase IMMEDIATELY after updating consumed
      if (firebaseReady && currentUser != "") {
        saveUserData(currentUser);
        Serial.printf("💾 Saved consumed: %ldg (user: %s)\n", totalGramsConsumed, currentUser.c_str());
      }
      
      delay(1000);
      yield();
      
      String telegramMsg = "✅ *FEEDING COMPLETE*\n";
      telegramMsg += "📅 *Schedule:* " + scheduleTimes[i] + "\n";
      telegramMsg += "🐷 *Pigs Fed:* " + String(totalPigs) + "\n";
      telegramMsg += "⚖️ *Total Dispensed:* " + String(totalGrams) + "g\n";
      telegramMsg += "📊 *Per Pig:* " + String(scheduleGrams[i]) + "g\n";
      telegramMsg += "⏱️ *Time:* " + getCurrentTime();
      
      bool sent = false;
      for(int retry = 0; retry < 3 && !sent; retry++) {
        if(retry > 0) {
          Serial.printf("🔄 Telegram retry %d/3...\n", retry+1);
          delay(2000);
          if(WiFi.status() != WL_CONNECTED) WiFi.reconnect();
          delay(1000);
        }
        sent = sendTelegramAlertWithRetry(telegramMsg);
      }
      
      if(sent) {
        logFeedingToFirebase(scheduleTimes[i], totalPigs, totalGrams);
        delay(300);
      }
      
      scheduleCompleted[i] = true;
      lastTrigger = current;
      nextFeedingTime = now + (24 * 3600);
      return;
    }
  }
}

// =====================================================
// 📊 SENSOR FUNCTIONS (✅ FIXED: Real-time + Filtered)
// =====================================================

// ✅ NEW: Median filter for noise reduction
int readFilteredDistance(int samples = 5) {
  int readings[samples];
  
  // Take multiple readings
  for (int i = 0; i < samples; i++) {
    readings[i] = readUltrasonicDistance();
    delay(30);  // Small delay between readings
  }
  
  // Simple bubble sort for median
  for (int i = 0; i < samples - 1; i++) {
    for (int j = 0; j < samples - i - 1; j++) {
      if (readings[j] > readings[j + 1]) {
        int temp = readings[j];
        readings[j] = readings[j + 1];
        readings[j + 1] = temp;
      }
    }
  }
  
  // Return median value (middle of sorted array)
  return readings[samples / 2];
}

// ✅ NEW: Smoothed feed level update with hysteresis
void updateFeedLevel() {
  int rawDistance = readFilteredDistance(5);  // Take 5 samples, use median
  
  // Ignore obviously bad readings (outliers)
  if (rawDistance < TANK_MIN_CM - 2 || rawDistance > TANK_MAX_CM + 2) {
    Serial.printf("⚠️ Sensor outlier: %d cm (ignored)\n", rawDistance);
    return;  // Keep previous cached value
  }
  
  // Update cached distance with optional calibration offset
  cachedDistance = rawDistance + SENSOR_OFFSET_CM;
  
  // Map distance to percentage (inverted: less distance = more feed)
  int newPercent = constrain(map(cachedDistance, TANK_MAX_CM, TANK_MIN_CM, 0, 100), 0, 100);
  
  // ✅ HYSTERESIS: Only update if change is significant (prevents flickering)
  if (abs(newPercent - feedLevelPercent) >= 2) {
    feedLevelPercent = newPercent;
    Serial.printf("📊 Feed level: %d%% (distance: %d cm)\n", feedLevelPercent, cachedDistance);
  }
  
  // Low feed alert logic
  if (feedLevelPercent < LOW_FEED_THRESHOLD && !lowFeedAlertSent) {
    sendTelegramAlert("⚠️ *LOW FEED*\n📉 Level: *" + String(feedLevelPercent) + "%*\n🔄 Refill soon!");
    lowFeedAlertSent = true;
  } else if (feedLevelPercent >= LOW_FEED_THRESHOLD + 10 && lowFeedAlertSent) {
    lowFeedAlertSent = false;
  }
}

// Original ultrasonic reading function (unchanged)
int readUltrasonicDistance() {
  digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long dur = pulseIn(ECHO_PIN, HIGH, 30000);
  return dur == 0 ? TANK_MAX_CM : constrain(dur * 0.034 / 2, TANK_MIN_CM, TANK_MAX_CM);
}

// =====================================================
// 📤 TELEGRAM ALERT (UNCHANGED)
// =====================================================
bool sendTelegramAlertWithRetry(String msg) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ Telegram: WiFi not connected");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();
  
  String url = "/bot" + String(BOT_TOKEN) + "/sendMessage";
  
  msg.replace("\\", "\\\\");
  msg.replace("\"", "\\\"");
  msg.replace("\n", "\\n");
  
  String payload = "{\"chat_id\":\"" + String(CHAT_ID) + "\",\"text\":\"" + msg + "\",\"parse_mode\":\"Markdown\"}";
  
  int limit = (msg.length() > 40) ? 40 : msg.length();
  Serial.printf("📤 Telegram: %s\n", msg.substring(0, limit).c_str());
  
  if (!client.connect("api.telegram.org", 443)) {
    Serial.println("❌ Telegram: Connection failed");
    return false;
  }
  
  client.println("POST " + url + " HTTP/1.1");
  client.println("Host: api.telegram.org");
  client.println("Content-Type: application/json");
  client.println("Content-Length: " + String(payload.length()));
  client.println("Connection: close\r\n");
  client.print(payload);
  
  unsigned long timeout = millis();
  while (client.available() == 0 && millis() - timeout < 10000) delay(10);
  
  if (client.available()) {
    String response = client.readStringUntil('\n');
    client.stop();
    if (response.startsWith("HTTP/1.1 200")) {
      Serial.println("✅ Telegram sent successfully");
      return true;
    } else {
      Serial.printf("⚠️ Telegram response: %s\n", response.c_str());
      return false;
    }
  }
  
  client.stop();
  Serial.println("❌ Telegram: Timeout");
  return false;
}

void sendTelegramAlert(String msg) {
  sendTelegramAlertWithRetry(msg);
}

// =====================================================
// 🔄 DISPENSING (UNCHANGED)
// =====================================================
void dispense(int grams) {
  if (isDispensing || grams < 50) { isDispensing = false; return; }
  isDispensing = true;
  Serial.printf("🔄 Dispensing %dg...\n", grams);
  
  int cycles = grams / gramsPerCycle;
  if (cycles == 0) cycles = 1;
  
  for (int i = 0; i < cycles; i++) {
    unsigned long t = (i == 0) ? firstCycleTime : nextCycleTime;
    Serial.printf("  🎯 Cycle %d/%d: %lu ms\n", i+1, cycles, t);
    
    myServo.write(openAngle); delay(400);
    delay(t);
    myServo.write(closeAngle); delay(400);
    
    if (i < cycles - 1) delay(pauseBetween);
    
    server.handleClient();
    yield();
    delay(1);
  }
  
  Serial.println("✅ Dispense complete");
  isDispensing = false;
  updateFeedLevel();
}
