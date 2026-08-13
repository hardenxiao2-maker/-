#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>

// ESP32 作为 WiFi 热点和网页服务器。
// 接线：MSPM0 UART0 TX(PA10) -> ESP32 RX2(GPIO16)
//       MSPM0 UART0 RX(PA11) <- ESP32 TX2(GPIO17)
//       MSPM0 GND            -- ESP32 GND
static const char *AP_SSID = "PingPongCtrl";
static const char *AP_PASS = "12345678";
static const uint32_t UART_BAUD = 115200;
static const int ESP32_RX2_PIN = 16;
static const int ESP32_TX2_PIN = 17;

WebServer server(80);
WebSocketsServer webSocket(81);

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>乒乓球位置控制实时监测</title>
<style>
:root{font-family:Arial,"Microsoft YaHei",sans-serif;color:#17202a;background:#f4f7fb}
*{box-sizing:border-box}body{margin:0}.app{min-height:100vh;display:grid;grid-template-rows:auto 1fr}
header{background:#0f766e;color:white;padding:18px 24px;display:flex;justify-content:space-between;gap:16px;align-items:center}
h1{font-size:22px;line-height:1.2;margin:0;font-weight:700}.status{font-size:14px;padding:6px 10px;border:1px solid rgba(255,255,255,.45);border-radius:6px}
main{padding:20px;display:grid;grid-template-columns:280px 1fr;gap:18px}
.panel{background:white;border:1px solid #d9e2ec;border-radius:8px;padding:16px;box-shadow:0 2px 10px rgba(15,23,42,.06)}
.cards{display:grid;grid-template-columns:1fr;gap:12px}.card{border:1px solid #e2e8f0;border-radius:8px;padding:14px;background:#fbfdff}
.label{font-size:13px;color:#52616f;margin-bottom:8px}.value{font-size:28px;font-weight:700;color:#102a43}.unit{font-size:14px;color:#627d98;margin-left:4px}
.controls{display:grid;gap:10px;margin-top:14px}.row{display:grid;grid-template-columns:1fr 1fr;gap:10px}
button,input{height:42px;border-radius:6px;border:1px solid #bcccdc;background:white;font-size:15px}
button{cursor:pointer;font-weight:700;color:#243b53}button.primary{background:#0f766e;border-color:#0f766e;color:white}button.danger{background:#b42318;border-color:#b42318;color:white}
button:active{transform:translateY(1px)}input{width:100%;padding:0 10px}
.chartHead{display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;color:#334e68}
canvas{width:100%;height:360px;border:1px solid #d9e2ec;border-radius:8px;background:linear-gradient(#fff,#f8fafc)}
.log{height:150px;overflow:auto;font-family:Consolas,monospace;font-size:12px;line-height:1.45;background:#102a43;color:#d9e2ec;border-radius:8px;padding:10px;margin-top:12px}
@media(max-width:760px){main{grid-template-columns:1fr;padding:12px}header{align-items:flex-start;flex-direction:column}canvas{height:280px}}
</style>
</head>
<body>
<div class="app">
<header><h1>乒乓球位置控制实时监测</h1><div id="linkState" class="status">未连接</div></header>
<main>
<section class="panel">
<div class="cards">
<div class="card"><div class="label">运行状态</div><div id="state" class="value">--</div></div>
<div class="card"><div class="label">目标距离</div><div><span id="target" class="value">--</span><span class="unit">cm</span></div></div>
<div class="card"><div class="label">实测距离</div><div><span id="actual" class="value">--</span><span class="unit">cm</span></div></div>
<div class="card"><div class="label">风机 PWM</div><div><span id="pwm" class="value">--</span><span class="unit">/1000</span></div></div>
</div>
<div class="controls">
<div class="row"><button class="primary" onclick="sendCmd('RUN')">启动</button><button class="danger" onclick="sendCmd('STOP')">停止</button></div>
<div class="row"><button onclick="sendCmd('DOWN')">-5 cm</button><button onclick="sendCmd('UP')">+5 cm</button></div>
<div class="row"><input id="setValue" type="number" value="50" min="30" max="70" step="0.5"><button onclick="setTarget()">设定</button></div>
<button onclick="sendCmd('GET')">立即刷新</button>
</div>
</section>
<section class="panel">
<div class="chartHead"><strong>距离曲线</strong><span id="lastTime">--</span></div>
<canvas id="chart" width="960" height="360"></canvas>
<div id="log" class="log"></div>
</section>
</main>
</div>
<script>
let ws, targetData=[], actualData=[], maxPoints=160;
const $=id=>document.getElementById(id);
function log(s){const el=$('log');el.textContent=(new Date()).toLocaleTimeString()+"  "+s+"\n"+el.textContent.slice(0,3500);}
function connect(){ws=new WebSocket(`ws://${location.hostname}:81/`);
ws.onopen=()=>{ $('linkState').textContent='已连接'; $('linkState').style.background='rgba(255,255,255,.18)'; sendCmd('GET'); };
ws.onclose=()=>{ $('linkState').textContent='断开，重连中'; setTimeout(connect,1000); };
ws.onerror=()=>{ $('linkState').textContent='连接错误'; };
ws.onmessage=e=>handleLine(String(e.data).trim());}
function sendCmd(cmd){if(ws&&ws.readyState===1){ws.send(cmd);log('TX '+cmd);}}
function setTarget(){sendCmd('SET '+$('setValue').value);}
function handleLine(line){if(!line)return;log('RX '+line);const p=line.split(',');
if(p[0]==='T'&&p.length>=6){const ms=Number(p[1]), active=p[2]==='1', target=Number(p[3]), actual=Number(p[4]), pwm=Number(p[5]);
$('state').textContent=active?'ACTIVE':'STANDBY';$('target').textContent=target.toFixed(1);$('actual').textContent=actual>=0?actual.toFixed(1):'ERR';$('pwm').textContent=String(pwm);$('lastTime').textContent=(ms/1000).toFixed(1)+' s';
targetData.push(target);actualData.push(actual>=0?actual:null);if(targetData.length>maxPoints){targetData.shift();actualData.shift();}draw();}}
function draw(){const c=$('chart'),ctx=c.getContext('2d'),w=c.width,h=c.height,pad=42;ctx.clearRect(0,0,w,h);
ctx.strokeStyle='#d9e2ec';ctx.lineWidth=1;ctx.font='13px Arial';ctx.fillStyle='#52616f';
for(let y=30;y<=80;y+=10){const py=h-pad-(y-30)/(80-30)*(h-pad*2);ctx.beginPath();ctx.moveTo(pad,py);ctx.lineTo(w-16,py);ctx.stroke();ctx.fillText(y+'cm',6,py+4);}
drawLine(targetData,'#b42318');drawLine(actualData,'#0f766e');
ctx.fillStyle='#b42318';ctx.fillRect(w-205,18,18,4);ctx.fillStyle='#334e68';ctx.fillText('目标',w-180,24);
ctx.fillStyle='#0f766e';ctx.fillRect(w-125,18,18,4);ctx.fillStyle='#334e68';ctx.fillText('实测',w-100,24);
function drawLine(arr,color){ctx.strokeStyle=color;ctx.lineWidth=3;ctx.beginPath();let started=false;arr.forEach((v,i)=>{if(v==null){started=false;return;}const x=pad+i/(maxPoints-1)*(w-pad-16);const y=h-pad-(v-30)/(80-30)*(h-pad*2);if(!started){ctx.moveTo(x,y);started=true;}else ctx.lineTo(x,y);});ctx.stroke();}}
connect();draw();
</script>
</body>
</html>
)HTML";

void handleRoot()
{
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleNotFound()
{
    server.send(404, "text/plain; charset=utf-8", "Not found");
}

void onWebSocketEvent(uint8_t client, WStype_t type, uint8_t *payload, size_t length)
{
    if (type == WStype_TEXT && length > 0) {
        for (size_t i = 0; i < length; i++) {
            Serial2.write(payload[i]);
        }
        Serial2.write('\n');
    }
}

void setup()
{
    Serial.begin(115200);
    Serial2.begin(UART_BAUD, SERIAL_8N1, ESP32_RX2_PIN, ESP32_TX2_PIN);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);

    server.on("/", handleRoot);
    server.onNotFound(handleNotFound);
    server.begin();

    webSocket.begin();
    webSocket.onEvent(onWebSocketEvent);

    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());
}

void loop()
{
    static String uartLine;

    server.handleClient();
    webSocket.loop();

    while (Serial2.available() > 0) {
        char ch = (char)Serial2.read();
        if (ch == '\n') {
            uartLine.trim();
            if (uartLine.length() > 0) {
                webSocket.broadcastTXT(uartLine);
                Serial.println(uartLine);
            }
            uartLine = "";
        } else if (ch != '\r') {
            if (uartLine.length() < 120) {
                uartLine += ch;
            } else {
                uartLine = "";
            }
        }
    }
}
