/*
 * ESP433RFWeb - 433MHz信号Web管理界面库实现
 */

#include "ESP433RFWeb.h"

ESP433RFWeb::ESP433RFWeb(ESP433RF& rf, SignalManager& signalMgr) 
  : _rf(rf), _signalMgr(signalMgr) {
  #ifdef ESP32
  _server = nullptr;
  _apSSID = "ESP433RF";
  _apPassword = "12345678";
  _apStarted = false;
  _captureCallback = nullptr;
  _bootBoundIndex = -1;  // 初始化为未绑定
  #endif
}
 
 void ESP433RFWeb::begin(const char* ssid, const char* password) {
   #ifdef ESP32
   _apSSID = String(ssid);
   _apPassword = String(password);
   
   // 启动WiFi AP模式
   WiFi.mode(WIFI_AP);
   WiFi.softAP(_apSSID.c_str(), _apPassword.c_str());
   _apStarted = true;
   
   Serial.printf("[WiFi] AP模式已启动\n");
   Serial.printf("[WiFi] SSID: %s\n", _apSSID.c_str());
   Serial.printf("[WiFi] 密码: %s\n", _apPassword.c_str());
   Serial.printf("[WiFi] IP地址: %s\n", WiFi.softAPIP().toString().c_str());
   
   // 创建Web服务器
   if (_server == nullptr) {
     _server = new WebServer(80);
   }
   
   // 注册路由
   _server->on("/", HTTP_GET, [this]() { this->handleRoot(); });
   _server->on("/api", HTTP_GET, [this]() { this->handleAPI(); });
   _server->on("/api", HTTP_POST, [this]() { this->handleAPI(); });
   _server->onNotFound([this]() { this->handleNotFound(); });
   
   _server->begin();
   Serial.println("[Web] Web服务器已启动");
   #endif
 }
 
 void ESP433RFWeb::end() {
   #ifdef ESP32
   if (_server != nullptr) {
     _server->stop();
     delete _server;
     _server = nullptr;
   }
   
   if (_apStarted) {
     WiFi.softAPdisconnect(true);
     _apStarted = false;
   }
   #endif
 }
 
 void ESP433RFWeb::handleClient() {
   #ifdef ESP32
   if (_server != nullptr) {
     _server->handleClient();
   }
   #endif
 }
 
 void ESP433RFWeb::setAPCredentials(const char* ssid, const char* password) {
   #ifdef ESP32
   _apSSID = String(ssid);
   _apPassword = String(password);
   #endif
 }
 
 String ESP433RFWeb::getAPIP() {
   #ifdef ESP32
   if (_apStarted) {
     return WiFi.softAPIP().toString();
   }
   #endif
   return "";
 }
 
 bool ESP433RFWeb::isAPMode() {
   #ifdef ESP32
   return _apStarted;
   #endif
   return false;
 }
 
 void ESP433RFWeb::setCaptureModeCallback(CaptureModeCallback callback) {
   #ifdef ESP32
   _captureCallback = callback;
   #endif
 }
 
#ifdef ESP32
void ESP433RFWeb::handleRoot() {
  String html = R"HTML(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
    <meta name="apple-mobile-web-app-capable" content="yes">
    <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
    <title>433MHz信号管理</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'SF Pro Display', 'Segoe UI', Roboto, sans-serif;
            background: #f2f2f7;
            min-height: 100vh;
            padding: 16px;
            padding-bottom: 32px;
        }
        .container {
            max-width: 600px;
            margin: 0 auto;
        }
        .header {
            background: linear-gradient(135deg, #007AFF 0%, #5856D6 100%);
            padding: 24px 20px;
            border-radius: 20px;
            margin-bottom: 16px;
            box-shadow: 0 8px 24px rgba(0,122,255,0.25);
            color: white;
        }
        h1 {
            font-size: 28px;
            font-weight: 700;
            margin-bottom: 8px;
            letter-spacing: -0.5px;
        }
        .status {
            font-size: 14px;
            opacity: 0.9;
            font-weight: 500;
        }
        .card {
            background: white;
            border-radius: 20px;
            padding: 20px;
            margin-bottom: 16px;
            box-shadow: 0 2px 16px rgba(0,0,0,0.08);
        }
        .card h2 {
            font-size: 20px;
            font-weight: 600;
            margin-bottom: 16px;
            color: #1c1c1e;
        }
        .btn-group {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 12px;
        }
        .btn {
            background: #007AFF;
            color: white;
            border: none;
            padding: 14px 20px;
            border-radius: 14px;
            cursor: pointer;
            font-size: 16px;
            font-weight: 600;
            transition: all 0.2s;
            box-shadow: 0 2px 8px rgba(0,122,255,0.3);
        }
        .btn:active { transform: scale(0.96); opacity: 0.8; }
        .btn-danger { background: #FF3B30; box-shadow: 0 2px 8px rgba(255,59,48,0.3); }
        .btn-success { background: #34C759; box-shadow: 0 2px 8px rgba(52,199,89,0.3); }
        .btn-warning { background: #FF9500; box-shadow: 0 2px 8px rgba(255,149,0,0.3); }
        .btn-secondary { background: #8E8E93; box-shadow: 0 2px 8px rgba(142,142,147,0.3); }
        .signal-item {
            background: #f9f9f9;
            border-radius: 16px;
            padding: 16px;
            margin-bottom: 12px;
            transition: all 0.2s;
        }
        .signal-item:active { transform: scale(0.98); }
        .signal-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 12px;
        }
        .signal-name {
            font-size: 17px;
            font-weight: 600;
            color: #1c1c1e;
        }
        .signal-badge {
            background: #007AFF;
            color: white;
            padding: 4px 12px;
            border-radius: 12px;
            font-size: 13px;
            font-weight: 600;
        }
        .signal-badge.boot-bound {
            background: #FF9500;
        }
        .signal-code {
            font-family: 'SF Mono', Monaco, monospace;
            background: #e5e5ea;
            padding: 8px 12px;
            border-radius: 10px;
            font-size: 14px;
            color: #3a3a3c;
            margin-bottom: 12px;
            display: inline-block;
        }
        .signal-actions {
            display: grid;
            grid-template-columns: 1fr 1fr 1fr;
            gap: 8px;
        }
        .btn-small {
            padding: 10px;
            font-size: 14px;
            border-radius: 12px;
        }
        .empty {
            text-align: center;
            padding: 60px 20px;
            color: #8e8e93;
            font-size: 16px;
        }
        .toast {
            position: fixed;
            top: 50%;
            left: 50%;
            transform: translate(-50%, -50%) scale(0.8);
            background: rgba(28, 28, 30, 0.95);
            color: white;
            padding: 16px 24px;
            border-radius: 16px;
            font-size: 15px;
            font-weight: 500;
            box-shadow: 0 8px 32px rgba(0,0,0,0.3);
            opacity: 0;
            pointer-events: none;
            transition: all 0.3s cubic-bezier(0.34, 1.56, 0.64, 1);
            z-index: 1000;
            max-width: 80%;
            text-align: center;
        }
        .toast.show {
            opacity: 1;
            transform: translate(-50%, -50%) scale(1);
        }
        @media (max-width: 480px) {
            body { padding: 12px; }
            .header { padding: 20px 16px; }
            h1 { font-size: 24px; }
            .btn-group { grid-template-columns: 1fr; }
            .signal-actions { grid-template-columns: 1fr; }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>433MHz 信号管理</h1>
            <div class="status">)HTML";
  html += getAPIP();
  html += R"HTML( | 信号: <span id="signalCount">0</span></div>
        </div>
        
        <div style="display:flex;gap:10px;margin-bottom:16px;">
            <button class="btn" style="flex:1;background:linear-gradient(135deg,#f093fb 0%,#f5576c 100%);" onclick="clearAll()">🗑️ 清空所有</button>
        </div>
        
        <div class="card" style="background:linear-gradient(135deg,#fff5f5 0%,#ffe5e5 100%);border-left:4px solid #f5576c;">
            <div style="display:flex;align-items:center;gap:10px;margin-bottom:10px;">
                <span style="font-size:24px;">⚠️</span>
                <h3 style="margin:0;color:#d63031;">使用提示</h3>
            </div>
            <div style="font-size:13px;line-height:1.6;color:#666;">
                <p style="margin:5px 0;"><strong>✅ 允许：</strong>备份自己的遥控器、控制自己的设备</p>
                <p style="margin:5px 0;"><strong>❌ 禁止：</strong>复制他人门禁、未授权访问、非法用途</p>
                <p style="margin:5px 0;color:#d63031;"><strong>⚖️ 责任：</strong>使用者需遵守法律法规，对使用后果自行负责</p>
            </div>
        </div>
         
        <div class="card">
            <h2>快捷操作</h2>
            <div class="btn-group">
                <button class="btn btn-warning" onclick="startCapture()">捕获信号</button>
                <button class="btn btn-success" onclick="refreshList()">刷新列表</button>
            </div>
        </div>
        
        <div class="card">
            <h2>信号列表</h2>
            <div id="signalList">
                <div class="empty">加载中...</div>
            </div>
        </div>
    </div>
    <div id="toast" class="toast"></div>
    
    <script>
        var bootBoundIndex = -1;
        
        function showToast(message) {
            var toast = document.getElementById('toast');
            toast.textContent = message;
            toast.classList.add('show');
            setTimeout(function() {
                toast.classList.remove('show');
            }, 2000);
        }
        
        function refreshList() {
            fetch('/api?action=list')
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    if (data.code === 200) {
                        var signals = data.data;
                        if (typeof signals === 'string') {
                            signals = JSON.parse(signals);
                        }
                        displaySignals(signals);
                    }
                })
                .catch(function(error) {
                    showToast('加载失败');
                });
        }
        
        function displaySignals(signals) {
            var list = document.getElementById('signalList');
            var count = document.getElementById('signalCount');
            count.textContent = signals.length;
            
            if (signals.length === 0) {
                list.innerHTML = '<div class="empty">暂无信号<br>点击"捕获信号"开始</div>';
                return;
            }
            
            // 反转数组，最新的信号显示在最上面
            var reversedSignals = signals.slice().reverse();
            
            var html = '';
            for (var i = 0; i < reversedSignals.length; i++) {
                var sig = reversedSignals[i];
                // 计算原始索引
                var originalIdx = signals.length - 1 - i;
                var isBound = (originalIdx === bootBoundIndex);
                
                html += '<div class="signal-item">';
                html += '<div class="signal-header">';
                html += '<div class="signal-name">' + sig.name + '</div>';
                if (isBound) {
                    html += '<div class="signal-badge boot-bound">Boot绑定</div>';
                } else {
                    html += '<div class="signal-badge">#' + (originalIdx + 1) + '</div>';
                }
                html += '</div>';
                html += '<div class="signal-code">' + sig.address + sig.key + '</div>';
                html += '<div class="signal-actions">';
                html += '<button class="btn btn-success btn-small" onclick="sendSignal(' + originalIdx + ')">发送</button>';
                if (isBound) {
                    html += '<button class="btn btn-secondary btn-small" onclick="unbindBoot()">解绑</button>';
                } else {
                    html += '<button class="btn btn-warning btn-small" onclick="bindBoot(' + originalIdx + ')">绑定</button>';
                }
                html += '<button class="btn btn-danger btn-small" onclick="deleteSignal(' + originalIdx + ')">删除</button>';
                html += '</div>';
                html += '</div>';
            }
            list.innerHTML = html;
        }
        
        function sendSignal(index) {
            fetch('/api?action=send&index=' + index, {method: 'POST'})
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    showToast(data.message);
                });
        }
        
        function deleteSignal(index) {
            fetch('/api?action=delete&index=' + index, {method: 'POST'})
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    showToast(data.message);
                    if (bootBoundIndex === index) {
                        bootBoundIndex = -1;
                    } else if (bootBoundIndex > index) {
                        bootBoundIndex--;
                    }
                    setTimeout(refreshList, 500);
                });
        }
        
        function bindBoot(index) {
            fetch('/api?action=bind_boot&index=' + index, {method: 'POST'})
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    showToast(data.message);
                    bootBoundIndex = index;
                    refreshList();
                });
        }
        
        function unbindBoot() {
            fetch('/api?action=unbind_boot', {method: 'POST'})
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    showToast(data.message);
                    bootBoundIndex = -1;
                    refreshList();
                });
        }
        
        function clearAll() {
            if (!confirm('确定要清空所有信号吗？此操作不可恢复！')) {
                return;
            }
            fetch('/api?action=clear_all', {method: 'POST'})
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    showToast(data.message);
                    bootBoundIndex = -1;
                    refreshList();
                });
        }
        
        function startCapture() {
            fetch('/api?action=capture', {method: 'POST'})
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    showToast(data.message);
                    setTimeout(refreshList, 2000);
                });
        }
        
        window.onload = function() {
            refreshList();
            fetch('/api?action=get_boot_binding')
                .then(function(r) { return r.json(); })
                .then(function(data) {
                    if (data.code === 200 && data.data >= 0) {
                        bootBoundIndex = data.data;
                    }
                });
        };
        setInterval(refreshList, 5000);
    </script>
</body>
</html>
)HTML";
   
   _server->send(200, "text/html", html);
 }
 
 void ESP433RFWeb::handleAPI() {
   if (!_server->hasArg("action")) {
     sendJSONResponse(400, "缺少action参数");
     return;
   }
   
   String action = _server->arg("action");
   
   if (action == "list") {
     // 获取信号列表
     String json = getSignalListJSON();
     sendJSONResponse(200, "成功", json);
   }
   else if (action == "send") {
     // 发送信号
     if (!_server->hasArg("index")) {
       sendJSONResponse(400, "缺少index参数");
       return;
     }
     
     uint8_t index = _server->arg("index").toInt();
     if (_signalMgr.sendSignal(index, _rf)) {
       sendJSONResponse(200, "信号已发送");
     } else {
       sendJSONResponse(400, "发送失败：索引无效");
     }
   }
   else if (action == "delete") {
     // 删除信号
     if (!_server->hasArg("index")) {
       sendJSONResponse(400, "缺少index参数");
       return;
     }
     
     uint8_t index = _server->arg("index").toInt();
     if (_signalMgr.removeSignal(index)) {
       sendJSONResponse(200, "信号已删除");
     } else {
       sendJSONResponse(400, "删除失败：索引无效");
     }
   }
   else if (action == "add") {
     // 添加信号
     if (!_server->hasArg("name") || !_server->hasArg("address") || !_server->hasArg("key")) {
       sendJSONResponse(400, "缺少必要参数");
       return;
     }
     
     String name = _server->arg("name");
     String address = _server->arg("address");
     String key = _server->arg("key");
     
     RFSignal signal;
     signal.address = address;
     signal.key = key;
     
     if (_signalMgr.addSignal(name, signal)) {
       sendJSONResponse(200, "信号已添加");
     } else {
       sendJSONResponse(400, "添加失败：可能已达到最大数量");
     }
   }
  else if (action == "capture") {
    // 进入捕获模式
    if (_captureCallback != nullptr) {
      _captureCallback(true);
    }
    _rf.enableCaptureMode();
    sendJSONResponse(200, "已进入捕获模式，请按下遥控器按键");
  }
  else if (action == "bind_boot") {
    // 绑定Boot按钮
    if (!_server->hasArg("index")) {
      sendJSONResponse(400, "缺少index参数");
      return;
    }
    
    uint8_t index = _server->arg("index").toInt();
    if (index < _signalMgr.getCount()) {
      _bootBoundIndex = index;
      Serial.printf("[WEB] Boot按钮已绑定到信号 #%d\n", index);
      sendJSONResponse(200, "Boot按钮已绑定");
    } else {
      sendJSONResponse(400, "绑定失败：索引无效");
    }
  }
  else if (action == "unbind_boot") {
    // 解绑Boot按钮
    _bootBoundIndex = -1;
    Serial.println("[WEB] Boot按钮已解绑");
    sendJSONResponse(200, "Boot按钮已解绑");
  }
  else if (action == "get_boot_binding") {
    // 获取Boot按钮绑定状态
    String data = String(_bootBoundIndex);
    sendJSONResponse(200, "成功", data);
  }
  else if (action == "clear_all") {
    // 一键清空所有信号
    uint8_t count = _signalMgr.getCount();
    for (int i = count - 1; i >= 0; i--) {
      _signalMgr.removeSignal(i);
    }
    _bootBoundIndex = -1;  // 清空绑定
    Serial.println("[WEB] 所有信号已清空");
    sendJSONResponse(200, "所有信号已清空");
  }
  else {
    sendJSONResponse(400, "未知的action: " + action);
  }
}
 
 void ESP433RFWeb::handleNotFound() {
   sendJSONResponse(404, "页面未找到");
 }
 
void ESP433RFWeb::sendJSONResponse(int code, const String& message, const String& data) {
  String json = "{\"code\":" + String(code) + ",\"message\":\"" + message + "\"";
  if (data.length() > 0) {
    // data已经是JSON字符串，直接嵌入（不要加引号）
    json += ",\"data\":" + data;
  }
  json += "}";
  
  // 添加调试输出
  Serial.printf("[API] Response: %s\n", json.c_str());
  
  _server->send(code, "application/json", json);
}

String ESP433RFWeb::getSignalListJSON() {
  uint8_t count = _signalMgr.getCount();
  Serial.printf("[API] getSignalListJSON: count=%d\n", count);
  
  if (count == 0) {
    return "[]";
  }
  
  SignalItem* items = new SignalItem[count];
  if (!_signalMgr.getAllSignals(items, count)) {
    Serial.println("[API] getAllSignals failed");
    delete[] items;
    return "[]";
  }
  
  String json = "[";
  for (uint8_t i = 0; i < count; i++) {
    if (i > 0) json += ",";
    json += "{";
    json += "\"name\":\"" + items[i].name + "\",";
    json += "\"address\":\"" + items[i].signal.address + "\",";
    json += "\"key\":\"" + items[i].signal.key + "\"";
    json += "}";
    Serial.printf("[API] Signal %d: %s (%s%s)\n", i, items[i].name.c_str(), 
                 items[i].signal.address.c_str(), items[i].signal.key.c_str());
  }
  json += "]";
  
  delete[] items;
  Serial.printf("[API] JSON: %s\n", json.c_str());
  return json;
}
#endif