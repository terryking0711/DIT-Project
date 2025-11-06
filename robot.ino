#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>

// ===== pin =====
const int IN1 = 26, IN2 = 25, IN3 = 33, IN4 = 32;
const int ENA = 27, ENB = 14;
const int SERVO_ARM_LEFT  = 13;
const int SERVO_ARM_RIGHT = 12;0
const int SERVO_CLAW      = 23;
const int SERVO_FLIP_BOX  = 22;

// ===== declare servo 和 server  =====
Servo armLeft, armRight, claw, flipBox;
WebServer server(80);

// ===== 全域變數 =====
int speedVal = 200;          // 速度v (可利用滑桿調節)
int turnSensitivity = 50;    // 轉向靈敏度
int armSpeedVal = 30;        // 手臂速度
int currentPWM = 0;          // 用於平滑停止
char activeCmd = 'S';        // 當前運動方向
bool moving = false;         // 是否正在移動
// 手臂漸進控制狀態
int armLeftAngle = 90;
int armRightAngle = 90;
bool armMoving = false;      // 手臂是否在移動
int armMoveDir = 0;          // +1 上升, -1 下降, 0 停
unsigned long lastArmMoveMillis = 0;

// 爪子漸進控制狀態
int clawAngle = 90;          // 爪子當前角度
bool clawMoving = false;     // 爪子是否在移動
int clawMoveDir = 0;         // +1 張開, -1 夾緊, 0 停
unsigned long lastClawMoveMillis = 0;

// 翻箱子開關狀態
bool flipBoxState = false;   // false=原位(90度), true=翻轉(160度)


// ===== 馬達控制函式 =====
void setMotor(char dir, int v) {
  analogWrite(ENA, v);
  analogWrite(ENB, v);
  switch (dir) {
    case 'F':
      digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
      digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
      break;
    case 'B':
      digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
      digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
      break;
    case 'L':
      digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH);
      digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW);
      break;
    case 'R':
      digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW);
      digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH);
      break;
    default:
      digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
      digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
      break;
  }
}

void stopCar() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
  analogWrite(ENA, 0);
  analogWrite(ENB, 0);
}

// ===== 平滑減速 =====
void smoothStop() {
  if (!moving && currentPWM > 0) {
    currentPWM -= 8; // 每次減少一點速度
    if (currentPWM < 0) currentPWM = 0;
    setMotor(activeCmd, currentPWM);
    if (currentPWM == 0) stopCar();
  }
}

// ===== 伺服控制 =====
void openClaw() { claw.write(120); }
void closeClaw() { claw.write(60); }
// 傳統一次性升降（保留以備）
void liftArm() { armLeft.write(60); armRight.write(120); }
void lowerArm() { armLeft.write(120); armRight.write(60); }
void flipBoxForward() { flipBox.write(160); }
void flipBoxHome() { flipBox.write(90); }

// ===== 指令控制 =====
void handleCmd(char c, bool pressed, int speed = 0, int sensitivity = 0) {
  if (pressed) {
    moving = true;
    activeCmd = c;
    
    // 根據指令類型使用對應參數
    if (c == 'L' || c == 'R') {
      currentPWM = map(sensitivity, 10, 100, 100, 255);
    } else {
      currentPWM = map(speed, 10, 100, 100, 255);
    }
    
    setMotor(c, currentPWM);
  } else {
    moving = false;
  }
}

void handleAction(char c) {
  // 保留舊的單次動作支援
  switch (c) {
    case 'X': 
      // 翻箱子開關邏輯
      flipBoxState = !flipBoxState;
      if (flipBoxState) {
        flipBox.write(160);  // 翻轉
        Serial.println("翻箱子: 翻轉到160度");
      } else {
        flipBox.write(90);   // 回原位
        Serial.println("翻箱子: 回到原位90度");
      }
      break;
    case 'H': 
      flipBoxHome(); 
      flipBoxState = false;  // 重置狀態
      Serial.println("翻箱子: 強制回到原位");
      break;
    case 'S': 
      stopCar(); 
      moving = false; 
      // 停止所有漸進動作
      armMoving = false;
      armMoveDir = 0;
      clawMoving = false;
      clawMoveDir = 0;
      Serial.println("全部停止");
      break;
    // O/C/U/D 已改為支援長按，此處保留單擊行為（向後兼容）
    case 'O': openClaw(); break;
    case 'C': closeClaw(); break;
    case 'U': liftArm(); break;
    case 'D': lowerArm(); break;
  }
}// ===== HTML介面 =====
const char* HTML_PAGE = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-TW">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Arduino 遙控車控制台</title>
<style>
* {
    box-sizing: border-box;
    margin: 0;
    padding: 0;
}

body {
    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    min-height: 100vh;
    padding: 20px;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: flex-start;
}

.container {
    background: rgba(255, 255, 255, 0.95);
    border-radius: 20px;
    padding: 30px;
    box-shadow: 0 15px 35px rgba(0, 0, 0, 0.1);
    max-width: 500px;
    width: 100%;
    backdrop-filter: blur(10px);
    border: 1px solid rgba(255, 255, 255, 0.2);
}

h1 {
    text-align: center;
    color: #333;
    margin-bottom: 30px;
    font-size: 2.2em;
    font-weight: 600;
    text-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
}

.control-section {
    margin-bottom: 25px;
}

.section-title {
    font-size: 1.1em;
    color: #555;
    margin-bottom: 15px;
    font-weight: 500;
    text-align: center;
}

/* 方向控制區域 */
.direction-grid {
    display: grid;
    grid-template-columns: 80px 80px 80px;
    grid-template-rows: 80px 80px 80px;
    gap: 8px;
    justify-content: center;
    margin-bottom: 20px;
}

.direction-grid button:nth-child(1) { grid-column: 2; grid-row: 1; }
.direction-grid button:nth-child(2) { grid-column: 1; grid-row: 2; }
.direction-grid button:nth-child(3) { grid-column: 2; grid-row: 2; }
.direction-grid button:nth-child(4) { grid-column: 3; grid-row: 2; }
.direction-grid button:nth-child(5) { grid-column: 2; grid-row: 3; }

/* 機械手臂控制區域 */
.arm-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 12px;
    margin-bottom: 20px;
}

/* 特殊功能區域 */
.function-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 12px;
}

button {
    font-size: 16px;
    font-weight: 600;
    padding: 12px;
    border: none;
    border-radius: 12px;
    cursor: pointer;
    transition: all 0.3s ease;
    box-shadow: 0 4px 15px rgba(0, 0, 0, 0.1);
    position: relative;
    overflow: hidden;
}

button::before {
    content: '';
    position: absolute;
    top: 0;
    left: -100%;
    width: 100%;
    height: 100%;
    background: linear-gradient(90deg, transparent, rgba(255,255,255,0.3), transparent);
    transition: left 0.5s;
}

button:hover::before {
    left: 100%;
}

/* 方向按鍵樣式 */
.direction-btn {
    background: linear-gradient(135deg, #4CAF50, #45a049);
    color: white;
    font-size: 18px;
}

.direction-btn:hover {
    transform: translateY(-2px);
    box-shadow: 0 6px 20px rgba(76, 175, 80, 0.4);
}

.direction-btn:active {
    transform: translateY(0);
}

/* 停止按鍵特殊樣式 */
.stop-btn {
    background: linear-gradient(135deg, #f44336, #d32f2f) !important;
    font-size: 16px;
}

.stop-btn:hover {
    box-shadow: 0 6px 20px rgba(244, 67, 54, 0.4) !important;
}

/* 機械手臂按鍵樣式 */
.arm-btn {
    background: linear-gradient(135deg, #2196F3, #1976D2);
    color: white;
}

.arm-btn:hover {
    transform: translateY(-2px);
    box-shadow: 0 6px 20px rgba(33, 150, 243, 0.4);
}

/* 功能按鍵樣式 */
.function-btn {
    background: linear-gradient(135deg, #9C27B0, #7B1FA2);
    color: white;
}

.function-btn:hover {
    transform: translateY(-2px);
    box-shadow: 0 6px 20px rgba(156, 39, 176, 0.4);
}

/* 滑桿控制區域 */
.slider-grid {
    display: grid;
    grid-template-columns: 1fr;
    gap: 15px;
    margin-bottom: 20px;
}

.slider-item {
    background: rgba(0, 0, 0, 0.02);
    border-radius: 10px;
    padding: 15px;
    border: 1px solid rgba(0, 0, 0, 0.05);
}

.slider-label {
    font-size: 14px;
    font-weight: 500;
    color: #555;
    margin-bottom: 8px;
    display: flex;
    justify-content: space-between;
    align-items: center;
}

.slider-value {
    font-size: 16px;
    font-weight: 600;
    color: #333;
    background: rgba(33, 150, 243, 0.1);
    padding: 2px 8px;
    border-radius: 4px;
    min-width: 40px;
    text-align: center;
}

/* 滑桿樣式 */
.slider {
    -webkit-appearance: none;
    appearance: none;
    width: 100%;
    height: 8px;
    border-radius: 5px;
    background: #ddd;
    outline: none;
    transition: all 0.3s ease;
}

.slider::-webkit-slider-thumb {
    -webkit-appearance: none;
    appearance: none;
    width: 24px;
    height: 24px;
    border-radius: 50%;
    background: linear-gradient(135deg, #2196F3, #1976D2);
    cursor: pointer;
    box-shadow: 0 2px 8px rgba(33, 150, 243, 0.4);
    transition: all 0.3s ease;
}

.slider::-webkit-slider-thumb:hover {
    transform: scale(1.1);
    box-shadow: 0 4px 12px rgba(33, 150, 243, 0.6);
}

.slider::-moz-range-thumb {
    width: 24px;
    height: 24px;
    border-radius: 50%;
    background: linear-gradient(135deg, #2196F3, #1976D2);
    cursor: pointer;
    border: none;
    box-shadow: 0 2px 8px rgba(33, 150, 243, 0.4);
    transition: all 0.3s ease;
}

.slider::-moz-range-thumb:hover {
    transform: scale(1.1);
    box-shadow: 0 4px 12px rgba(33, 150, 243, 0.6);
}

.slider:active::-webkit-slider-thumb {
    transform: scale(0.9);
}

.slider:active::-moz-range-thumb {
    transform: scale(0.9);
}

/* 速度滑桿特殊樣式 */
.speed-slider::-webkit-slider-thumb {
    background: linear-gradient(135deg, #4CAF50, #45a049);
    box-shadow: 0 2px 8px rgba(76, 175, 80, 0.4);
}

.speed-slider::-moz-range-thumb {
    background: linear-gradient(135deg, #4CAF50, #45a049);
    box-shadow: 0 2px 8px rgba(76, 175, 80, 0.4);
}

/* 轉向滑桿特殊樣式 */
.turn-slider::-webkit-slider-thumb {
    background: linear-gradient(135deg, #FF9800, #F57C00);
    box-shadow: 0 2px 8px rgba(255, 152, 0, 0.4);
}

.turn-slider::-moz-range-thumb {
    background: linear-gradient(135deg, #FF9800, #F57C00);
    box-shadow: 0 2px 8px rgba(255, 152, 0, 0.4);
}

/* 響應式設計 */
@media (max-width: 480px) {
    .container {
        padding: 20px;
        margin: 10px;
    }
    
    h1 {
        font-size: 1.8em;
    }
    
    .direction-grid {
        grid-template-columns: 70px 70px 70px;
        grid-template-rows: 70px 70px 70px;
    }
    
    button {
        font-size: 14px;
        padding: 10px;
    }
}

/* 狀態指示 */
.status {
    text-align: center;
    margin-top: 20px;
    padding: 10px;
    background: rgba(0, 0, 0, 0.05);
    border-radius: 8px;
    font-size: 14px;
    color: #666;
}
</style>
</head>
<body>
<div class="container">
    <h1>第四組 Arduino 遙控端</h1>
    
    <div class="control-section">
        <div class="section-title">🎮 方向控制</div>
        <div class="direction-grid">
            <button class="direction-btn" onmousedown="send('F',1)" onmouseup="send('F',0)" ontouchstart="send('F',1)" ontouchend="send('F',0)">
                <div>⬆️</div>
                <div>前進</div>
            </button>
            <button class="direction-btn" onmousedown="send('L',1)" onmouseup="send('L',0)" ontouchstart="send('L',1)" ontouchend="send('L',0)">
                <div>⬅️</div>
                <div>左轉</div>
            </button>
            <button class="direction-btn stop-btn" onclick="sendAct('S')">
                <div>⏹️</div>
                <div>停止</div>
            </button>
            <button class="direction-btn" onmousedown="send('R',1)" onmouseup="send('R',0)" ontouchstart="send('R',1)" ontouchend="send('R',0)">
                <div>➡️</div>
                <div>右轉</div>
            </button>
            <button class="direction-btn" onmousedown="send('B',1)" onmouseup="send('B',0)" ontouchstart="send('B',1)" ontouchend="send('B',0)">
                <div>⬇️</div>
                <div>後退</div>
            </button>
        </div>
    </div>

    <div class="control-section">
        <div class="section-title">🦾 機械手臂</div>
        <div class="arm-grid">
            <button class="arm-btn" onmousedown="sendActPress('U',1)" onmouseup="sendActPress('U',0)" ontouchstart="sendActPress('U',1)" ontouchend="sendActPress('U',0)">
                <div>⬆️</div>
                <div>手臂上升</div>
            </button>
            <button class="arm-btn" onmousedown="sendActPress('D',1)" onmouseup="sendActPress('D',0)" ontouchstart="sendActPress('D',1)" ontouchend="sendActPress('D',0)">
                <div>⬇️</div>
                <div>手臂下降</div>
            </button>
            <button class="arm-btn" onmousedown="sendActPress('O',1)" onmouseup="sendActPress('O',0)" ontouchstart="sendActPress('O',1)" ontouchend="sendActPress('O',0)">
                <div>✋</div>
                <div>張開</div>
            </button>
            <button class="arm-btn" onmousedown="sendActPress('C',1)" onmouseup="sendActPress('C',0)" ontouchstart="sendActPress('C',1)" ontouchend="sendActPress('C',0)">
                <div>✊</div>
                <div>夾緊</div>
            </button>
        </div>
    </div>

    <div class="control-section">
        <div class="section-title">🎚️ 參數控制</div>
        <div class="slider-grid">
            <div class="slider-item">
                <div class="slider-label">
                    <span>移動速度</span>
                    <span class="slider-value" id="speed-value">50</span>
                </div>
                <input type="range" min="10" max="100" value="50" 
                       class="slider speed-slider" id="speed-slider" 
                       oninput="updateSpeed(this.value)">
            </div>
            
            <div class="slider-item">
                <div class="slider-label">
                    <span>轉向靈敏度</span>
                    <span class="slider-value" id="turn-value">50</span>
                </div>
                <input type="range" min="10" max="100" value="50" 
                       class="slider turn-slider" id="turn-slider" 
                       oninput="updateTurnSensitivity(this.value)">
            </div>
            
            <div class="slider-item">
                <div class="slider-label">
                    <span>手臂速度</span>
                    <span class="slider-value" id="arm-value">30</span>
                </div>
                <input type="range" min="5" max="80" value="30" 
                       class="slider" id="arm-slider" 
                       oninput="updateArmSpeed(this.value)">
            </div>
        </div>
    </div>

    <div class="control-section">
        <div class="section-title">⚡ 特殊功能</div>
        <div class="function-grid">
            <button class="function-btn" onclick="sendAct('X')">
                <div>🔄</div>
                <div>翻轉</div>
            </button>
            <button class="function-btn" onclick="sendAct('H')">
                <div>🏠</div>
                <div>回到原點</div>
            </button>
        </div>
    </div>

    <div class="status" id="status">
        準備就緒 🟢
    </div>
</div>
<script>
let isConnected = true;
let currentMoving = false;

// 滑桿參數
let speedValue = 50;
let turnSensitivity = 50;
let armSpeed = 30;

async function send(command, state) {
    const statusElement = document.getElementById('status');
    
    try {
        if (state === 1 && !currentMoving) {
            statusElement.innerHTML = `🚗 ${getCommandName(command)}中...`;
            statusElement.style.color = '#2196f3';
            currentMoving = true;
        } else if (state === 0 && currentMoving) {
            statusElement.innerHTML = '⏹️ 停止移動';
            statusElement.style.color = '#ff9800';
            currentMoving = false;
            setTimeout(() => {
                if (isConnected && !currentMoving) {
                    statusElement.innerHTML = '準備就緒 🟢';
                    statusElement.style.color = '#666';
                }
            }, 1000);
        }
        
        // 根據滑桿參數調整指令
        if (command === 'L' || command === 'R') {
            // 轉向指令加入靈敏度參數
            const response = await fetch(`/move?c=${command}&p=${state}&s=${turnSensitivity}`);
            if (response.ok) {
                isConnected = true;
            } else {
                throw new Error('移動指令執行失敗');
            }
        } else {
            // 前進後退指令加入速度參數
            const response = await fetch(`/move?c=${command}&p=${state}&v=${speedValue}`);
            if (response.ok) {
                isConnected = true;
            } else {
                throw new Error('移動指令執行失敗');
            }
        }
        
    } catch (error) {
        statusElement.innerHTML = '❌ 連線錯誤，請檢查設備';
        statusElement.style.color = '#f44336';
        isConnected = false;
        currentMoving = false;
    }
}

async function sendAct(command) {
    const statusElement = document.getElementById('status');
    
    try {
        statusElement.innerHTML = `執行: ${getCommandName(command)} ⏳`;
        statusElement.style.color = '#ff9800';
        
    // 非長按動作仍呼叫 /act?c=...
    const response = await fetch(`/act?c=${command}`);
        
        if (response.ok) {
            statusElement.innerHTML = `✅ 已執行: ${getCommandName(command)}`;
            statusElement.style.color = '#4caf50';
            isConnected = true;
        } else {
            throw new Error('動作指令執行失敗');
        }
        
        // 2秒後回到準備狀態
        setTimeout(() => {
            if (isConnected && !currentMoving) {
                statusElement.innerHTML = '準備就緒 🟢';
                statusElement.style.color = '#666';
            }
        }, 2000);
        
    } catch (error) {
        statusElement.innerHTML = '❌ 連線錯誤，請檢查設備';
        statusElement.style.color = '#f44336';
        isConnected = false;
    }
}

// 長按手臂控制：發送 p(pressed) 與 a(arm speed)
async function sendActPress(command, pressed) {
    const statusElement = document.getElementById('status');
    try {
        const a = document.getElementById('arm-slider').value;
        const url = `/act?c=${command}&p=${pressed?1:0}&a=${a}`;
        const response = await fetch(url);
        if (response.ok) {
            if (pressed) {
                statusElement.innerHTML = `手臂 ${getCommandName(command)} 按住中`;
                statusElement.style.color = '#2196f3';
            } else {
                statusElement.innerHTML = `手臂 停止`;
                statusElement.style.color = '#ff9800';
                setTimeout(()=>{ if (isConnected) statusElement.innerHTML='準備就緒 🟢'; },1000);
            }
        }
    } catch (e) {
        statusElement.innerHTML = '❌ 連線錯誤，請檢查設備';
        statusElement.style.color = '#f44336';
        isConnected = false;
    }
}

function getCommandName(command) {
    const commands = {
        'F': '前進',
        'B': '後退', 
        'L': '左轉',
        'R': '右轉',
        'S': '停止',
        'U': '手臂上升',
        'D': '手臂下降',
        'O': '張開',
        'C': '夾緊',
        'X': '翻轉',
        'H': '回到原點'
    };
    return commands[command] || command;
}

// 滑桿更新函數
function updateSpeed(value) {
    speedValue = parseInt(value);
    document.getElementById('speed-value').textContent = value;
    
    // 發送速度更新到後端
    fetch(`/config?type=speed&value=${value}`)
        .catch(error => console.log('速度設定更新:', value));
}

function updateTurnSensitivity(value) {
    turnSensitivity = parseInt(value);
    document.getElementById('turn-value').textContent = value;
    
    // 發送轉向靈敏度更新到後端
    fetch(`/config?type=turn&value=${value}`)
        .catch(error => console.log('轉向靈敏度設定更新:', value));
}

function updateArmSpeed(value) {
    armSpeed = parseInt(value);
    document.getElementById('arm-value').textContent = value;
    
    // 發送手臂速度更新到後端
    fetch(`/config?type=arm&value=${value}`)
        .catch(error => console.log('手臂速度設定更新:', value));
}

// 鍵盤控制支援
let keyPressed = {};

document.addEventListener('keydown', function(event) {
    if (keyPressed[event.key]) return; // 防止重複觸發
    
    const moveKeys = {
        'ArrowUp': 'F',
        'ArrowDown': 'B', 
        'ArrowLeft': 'L',
        'ArrowRight': 'R',
        'w': 'F',
        'W': 'F',
        's': 'B',
        'S': 'B',
        'a': 'L',
        'A': 'L',
        'd': 'R',
        'D': 'R'
    };
    
    const actionKeys = {
        ' ': 'S' // 空白鍵停止
    };
    
    if (moveKeys[event.key]) {
        event.preventDefault();
        keyPressed[event.key] = true;
        send(moveKeys[event.key], 1);
        
        // 視覺反饋
        highlightButton(moveKeys[event.key], true);
    } else if (actionKeys[event.key]) {
        event.preventDefault();
        keyPressed[event.key] = true;
        sendAct(actionKeys[event.key]);
        
        // 視覺反饋
        highlightButton(actionKeys[event.key], false);
    }
});

document.addEventListener('keyup', function(event) {
    const moveKeys = {
        'ArrowUp': 'F',
        'ArrowDown': 'B', 
        'ArrowLeft': 'L',
        'ArrowRight': 'R',
        'w': 'F',
        'W': 'F',
        's': 'B',
        'S': 'B',
        'a': 'L',
        'A': 'L',
        'd': 'R',
        'D': 'R'
    };
    
    if (moveKeys[event.key] && keyPressed[event.key]) {
        event.preventDefault();
        keyPressed[event.key] = false;
        send(moveKeys[event.key], 0);
        
        // 恢復視覺效果
        highlightButton(moveKeys[event.key], false);
    }
});

function highlightButton(command, isPressed) {
    const buttons = document.querySelectorAll('button');
    buttons.forEach(btn => {
        const onmousedown = btn.getAttribute('onmousedown');
        const onclick = btn.getAttribute('onclick');
        
        if ((onmousedown && onmousedown.includes(`'${command}'`)) || 
            (onclick && onclick.includes(`'${command}'`))) {
            if (isPressed) {
                btn.style.transform = 'scale(0.95)';
                btn.style.filter = 'brightness(1.2)';
            } else {
                btn.style.transform = '';
                btn.style.filter = '';
            }
        }
    });
}

// 防止頁面刷新
window.addEventListener('beforeunload', function(e) {
    if (!isConnected) {
        e.preventDefault();
        e.returnValue = '';
    }
});

// 頁面載入完成後的初始化
document.addEventListener('DOMContentLoaded', function() {
    // 載入儲存的設定
    const savedSpeed = localStorage.getItem('robotSpeed');
    const savedTurn = localStorage.getItem('robotTurn');
    const savedArm = localStorage.getItem('robotArm');
    
    if (savedSpeed) {
        document.getElementById('speed-slider').value = savedSpeed;
        updateSpeed(savedSpeed);
    }
    if (savedTurn) {
        document.getElementById('turn-slider').value = savedTurn;
        updateTurnSensitivity(savedTurn);
    }
    if (savedArm) {
        document.getElementById('arm-slider').value = savedArm;
        updateArmSpeed(savedArm);
    }
});

// 儲存設定到本地存儲
function saveSettings() {
    localStorage.setItem('robotSpeed', speedValue);
    localStorage.setItem('robotTurn', turnSensitivity);
    localStorage.setItem('robotArm', armSpeed);
}

// 監聽滑桿變化，自動儲存
document.addEventListener('input', function(e) {
    if (e.target.classList.contains('slider')) {
        setTimeout(saveSettings, 500); // 延遲儲存，避免頻繁操作
    }
});

console.log('Arduino遙控車控制台已載入 🚗');
console.log('鍵盤控制: 方向鍵或WASD (按住移動)，空白鍵停止');
console.log('移動API: /move?c={command}&p={state}&v={speed}&s={sensitivity}');
console.log('動作API: /act?c={command}&a={armSpeed}');
console.log('設定API: /config?type={type}&value={value}');
</script>
</body>
</html>
)rawliteral";

// ===== HTTP 處理 =====
void handleRoot() { 
  server.send(200, "text/html", HTML_PAGE);
}

void handleMove() {
  if (!server.hasArg("c") || !server.hasArg("p")) {
    server.send(400, "text/plain", "bad args");
    return;
  }
  
  char c = server.arg("c")[0];
  bool pressed = server.arg("p").toInt() == 1;
  int speed = server.hasArg("v") ? server.arg("v").toInt() : speedVal;
  int sensitivity = server.hasArg("s") ? server.arg("s").toInt() : turnSensitivity;
  
  handleCmd(c, pressed, speed, sensitivity);
  server.send(200, "text/plain", "OK");
}

void handleAct() {
    if (!server.hasArg("c")) {
        server.send(400, "text/plain", "missing c");
        return;
    }

    char c = server.arg("c")[0];

    // 讀取按下狀態 p=1 或 p=0（可選）
    bool pressed = false;
    if (server.hasArg("p")) pressed = server.arg("p").toInt() == 1;

    // 如果有手臂速度參數，更新armSpeedVal
    if (server.hasArg("a")) {
        armSpeedVal = server.arg("a").toInt();
    }

    // 調試信息：顯示收到的參數
    Serial.print("handleAct: c=");
    Serial.print(c);
    if (server.hasArg("p")) {
        Serial.print(", p=");
        Serial.print(pressed ? "1" : "0");
    }
    if (server.hasArg("a")) {
        Serial.print(", a=");
        Serial.print(armSpeedVal);
    }
    Serial.println();

  // 處理手臂長按行為（U/D）- 只有當有p參數時才進入漸進控制
  if ((c == 'U' || c == 'D') && server.hasArg("p")) {
    if (pressed) {
      armMoving = true;
      armMoveDir = (c == 'U') ? +1 : -1;
      lastArmMoveMillis = millis();
      Serial.println("手臂開始" + String(c == 'U' ? "上升" : "下降") + " (漸進模式)");
      server.send(200, "text/plain", "OK");
      return;
    } else {
      // 放開停止
      armMoving = false;
      armMoveDir = 0;
      Serial.println("手臂停止");
      server.send(200, "text/plain", "OK");
      return;
    }
  }

  // 處理爪子長按行為（O/C）- 只有當有p參數時才進入漸進控制
  if ((c == 'O' || c == 'C') && server.hasArg("p")) {
    if (pressed) {
      clawMoving = true;
      clawMoveDir = (c == 'O') ? +1 : -1;  // O=張開(+), C=夾緊(-)
      lastClawMoveMillis = millis();
      Serial.println("爪子開始" + String(c == 'O' ? "張開" : "夾緊") + " (漸進模式)");
      server.send(200, "text/plain", "OK");
      return;
    } else {
      // 放開停止
      clawMoving = false;
      clawMoveDir = 0;
      Serial.println("爪子停止");
      server.send(200, "text/plain", "OK");
      return;
    }
  }    // 其他動作仍然使用原先處理
    handleAction(c);
    server.send(200, "text/plain", "OK");
}

void handleConfig() {
  if (!server.hasArg("type") || !server.hasArg("value")) {
    server.send(400, "text/plain", "missing parameters");
    return;
  }
  
  String type = server.arg("type");
  int value = server.arg("value").toInt();
  
  if (type == "speed") {
    speedVal = constrain(value, 10, 100);
    Serial.println("Speed updated to: " + String(speedVal));
  } else if (type == "turn") {
    turnSensitivity = constrain(value, 10, 100);
    Serial.println("Turn sensitivity updated to: " + String(turnSensitivity));
  } else if (type == "arm") {
    armSpeedVal = constrain(value, 5, 80);
    Serial.println("Arm speed updated to: " + String(armSpeedVal));
  } else {
    server.send(400, "text/plain", "unknown type");
    return;
  }
  
  server.send(200, "text/plain", "Config updated");
}

// ===== Setup =====
void setup() {
  Serial.begin(115200);

  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT); pinMode(ENB, OUTPUT);

  armLeft.attach(SERVO_ARM_LEFT);
  armRight.attach(SERVO_ARM_RIGHT);
  claw.attach(SERVO_CLAW);
  flipBox.attach(SERVO_FLIP_BOX);
  
  // 初始化伺服馬達位置和狀態變數
  armLeftAngle = 90;
  armRightAngle = 90;
  clawAngle = 90;
  armLeft.write(armLeftAngle); 
  armRight.write(armRightAngle); 
  claw.write(clawAngle); 
  flipBox.write(90);
  flipBoxState = false;

  WiFi.softAP("ESP32-Car", "88888888");
  Serial.print("AP IP: "); 
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/move", handleMove);
  server.on("/act", handleAct);
  server.on("/config", handleConfig);
  
  server.begin();
  Serial.println("Web server ready.");
  Serial.println("====================================");
  Serial.println("🚗 Arduino 遙控車控制台已啟動");
  Serial.println("✅ 支援漸進式手臂升降控制");
  Serial.println("✅ 支援漸進式爪子開合控制");
  Serial.println("✅ 支援翻箱子開關模式");
  Serial.println("====================================");
  Serial.print("WiFi 熱點名稱: ESP32-Car");
  Serial.println("  密碼: 88888888");
  Serial.print("控制網頁: http://192.168.4.1");
  Serial.println(WiFi.softAPIP());
  Serial.println("====================================");
}

// ===== Loop =====
void loop() {
  server.handleClient();
  smoothStop();  // 每回圈檢查是否需要減速
    // 手臂漸進控制：按住時逐步改變角度
    if (armMoving && armMoveDir != 0) {
        unsigned long now = millis();
        // 移動頻率取決於 armSpeedVal（值越大越快）
        // 因為每次變化30度，所以延遲時間調整得更長一些
        // 假設 armSpeedVal 範圍 5..80，將對應到 delay 100..500 ms
        int delayMs = map(constrain(armSpeedVal, 5, 80), 5, 80, 500, 100);
        if (now - lastArmMoveMillis >= (unsigned long)delayMs) {
            lastArmMoveMillis = now;
            // 每次改變角度幅度改為30度，移動更明顯
            armLeftAngle += armMoveDir * 30;
            armRightAngle -= armMoveDir * 30; // 反向
            // 限制角度範圍（假設 20..160 安全範圍）
            armLeftAngle = constrain(armLeftAngle, 20, 160);
            armRightAngle = constrain(armRightAngle, 20, 160);
            armLeft.write(armLeftAngle);
            armRight.write(armRightAngle);
        }
    }

    // 爪子漸進控制：按住時逐步改變角度
    if (clawMoving && clawMoveDir != 0) {
        unsigned long now = millis();
        // 爪子使用相同的速度控制，因為每次變化30度所以延遲更長
        int delayMs = map(constrain(armSpeedVal, 5, 80), 5, 80, 500, 100);
        if (now - lastClawMoveMillis >= (unsigned long)delayMs) {
            lastClawMoveMillis = now;
            // 爪子角度變化：張開(+)朝向120度，夾緊(-)朝向60度，每次變化30度
            int oldAngle = clawAngle;
            clawAngle += clawMoveDir * 30;
            // 限制爪子角度範圍（60度夾緊，120度張開）
            clawAngle = constrain(clawAngle, 60, 120);
            claw.write(clawAngle);
            
            // 調試信息：顯示角度變化
            // Serial.print("爪子移動: ");
            // Serial.print(oldAngle);
            // Serial.print(" -> ");
            // Serial.print(clawAngle);
            // Serial.print("度 (方向: ");
            // Serial.print(clawMoveDir > 0 ? "張開" : "夾緊");
            // Serial.println(")");
        }
    }
}

