#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// ======== WIFI ========
const char* ssid = "Davi";
const char* password = "Davi54321@";

// ======== MQTT HIVE MQ CLOUD ========
const char* mqtt_broker   = "c09eb8befcaa413b9017c18cbd0aa560.s1.eu.hivemq.cloud";
const int   mqtt_port     = 8883;
const char* mqtt_username = "ESP32";
const char* mqtt_password = "Senha12345";

// ======== SERVO ========
Servo meuServo;
const int servoPin = 26;
int currentAngle = 90;

// ======== Movimento suave ========
int smoothCurrentAngle = 90;
int smoothTargetAngle = 90;
bool smoothMoving = false;
unsigned long lastStepMillis = 0;
const unsigned long stepInterval = 30;
const int stepSize = 1;
unsigned long detachTimeout = 800;
unsigned long reachedMillis = 0;
bool attachedFlag = false;

void startSmoothMove(int target) {
  if (target < 0) target = 0;
  if (target > 180) target = 180;

  if (!attachedFlag) {
    meuServo.attach(servoPin);
    attachedFlag = true;
    delay(10);
  }

  smoothTargetAngle = target;
  smoothMoving = true;
  lastStepMillis = millis();
}

void handleSmoothMove() {
  unsigned long now = millis();

  if (smoothMoving) {
    if (now - lastStepMillis >= stepInterval) {
      lastStepMillis = now;

      if (smoothCurrentAngle == smoothTargetAngle) {
        smoothMoving = false;
        reachedMillis = now;
        currentAngle = smoothCurrentAngle;
        return;
      }

      if (smoothTargetAngle > smoothCurrentAngle) smoothCurrentAngle += stepSize;
      else smoothCurrentAngle -= stepSize;

      meuServo.write(smoothCurrentAngle);
      currentAngle = smoothCurrentAngle;
    }
  }
  else if (attachedFlag && now - reachedMillis > detachTimeout) {
    meuServo.detach();
    attachedFlag = false;
  }
}

// ======== LIBERAÇÃO POR TEMPO ========
bool liberando = false;
unsigned long liberarTempo = 2000;
unsigned long liberarStart = 0;

// ======== MQTT ========
WiFiClientSecure espClient;
PubSubClient client(espClient);

// MQTT → Comando recebido
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";

  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];

  // --- NOVO: comando remoto de liberar ração ---
  if (String(topic) == "alimentador/liberar") {

    if (msg == "1") {   // qualquer payload serve, mas "1" é padrão
      liberando = true;
      liberarStart = millis();
      startSmoothMove(165); // ABRIR
      Serial.println("MQTT: liberando ração...");
    }
  }
}

// ======== WEB SERVER ========
WebServer server(80);

String htmlPage() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Alimentador Automatico</title>";
  html += "<style>body{text-align:center;font-family:Arial;margin-top:30px;background:#f0f0f0;}button{padding:12px;font-size:20px;margin:8px;border-radius:8px;background:#27ae60;color:white;width:240px;}</style>";
  html += "</head><body>";
  html += "<h1>Alimentador Automatico</h1>";
  html += "<p>Acesse: <b>http://esp32.local</b></p>";
  html += "<p>Angulo atual: <span id='angle'>"+String(currentAngle)+"</span>°</p>";
  html += "<button onclick='liberar()'>Liberar Racao</button>";

  html += "<script>";
  html += "function liberar(){ fetch('/liberar').then(r=>r.text()).then(alert); }";
  html += "function update(){ fetch('/angle').then(r=>r.text()).then(a=>document.getElementById('angle').innerText=a); }";
  html += "setInterval(update,1000);";
  html += "</script>";

  html += "</body></html>";
  return html;
}

void handleRoot(){ server.send(200,"text/html",htmlPage()); }
void handleAngle(){ server.send(200,"text/plain",String(currentAngle)); }

void handleLiberar(){
  liberando = true;
  liberarStart = millis();
  startSmoothMove(165); // abrir
  server.send(200,"text/plain","Liberando ração...");
}

// ======== WIFI ========
void setup_wifi(){
  WiFi.begin(ssid,password);
  while(WiFi.status()!=WL_CONNECTED) delay(300);
  Serial.println("WiFi conectado!");
}

// ======== MQTT reconnect ========
void reconnect(){
  while(!client.connected()){
    if (client.connect("ESP32Client",mqtt_username,mqtt_password)) {
      client.subscribe("alimentador/liberar");   // <--- NOVO
    } else delay(2000);
  }
}

// ======== SETUP ========
void setup(){
  Serial.begin(115200);

  meuServo.attach(servoPin);
  attachedFlag = true;
  meuServo.write(90);

  setup_wifi();

  if (MDNS.begin("esp32"))
    Serial.println("mDNS OK! Abra: http://esp32.local");

  espClient.setInsecure();
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(mqttCallback);

  server.on("/", handleRoot);
  server.on("/angle", handleAngle);
  server.on("/liberar", handleLiberar);
  server.begin();
}

// ======== LOOP ========
void loop(){
  if (!client.connected()) reconnect();
  client.loop();
  server.handleClient();

  handleSmoothMove();

  // controle da abertura por tempo
  if (liberando) {
    if (millis() - liberarStart >= liberarTempo) {
      startSmoothMove(90); // fechar
      liberando = false;
    }
  }
}
