
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ======================================
// FLOODALERT - MONITORAMENTO DE ENCHENTES
// ESP32 + MQTT + THINGSBOARD
// ======================================

// WIFI WOKWI
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// THINGSBOARD MQTT
const char* MQTT_SERVER = "mqtt.thingsboard.cloud";
const int MQTT_PORT = 1883;

// SUBSTITUA PELO TOKEN DO SEU DISPOSITIVO
const char* DEVICE_TOKEN = "6d8gtk8zh5rnb6wc8vuv";

// TOPICO DE TELEMETRIA
const char* MQTT_TOPIC = "v1/devices/me/telemetry";

// ======================================
// DEFINICAO DOS PINOS
// ======================================

#define TRIG_PIN 5
#define ECHO_PIN 18

#define RAIN_PIN 34

#define LED_GREEN 27
#define LED_YELLOW 26
#define LED_RED 25

#define BUZZER_PIN 14

#define OLED_SDA 21
#define OLED_SCL 22

// ======================================
// CONFIGURACAO DO DISPLAY
// ======================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);



// ======================================
// CONFIGURACAO MQTT
// ======================================

WiFiClient espClient;
PubSubClient client(espClient);

// ======================================
// VARIAVEIS DO SISTEMA
// ======================================

const float ALTURA_CANAL_CM = 100.0;

float distancia = 0;
float nivelAgua = 0;
int intensidadeChuva = 0;

String nivelAlerta = "NORMAL";

bool alarme = false;

unsigned long ultimoEnvio = 0;
const unsigned long intervaloEnvio = 2000;

// ======================================
// CONEXAO WIFI
// ======================================

void conectarWiFi() {

  Serial.println();
  Serial.print("Conectando ao Wi-Fi");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, 6);

  while (WiFi.status() != WL_CONNECTED) {

    delay(500);
    Serial.print(".");

  }

  Serial.println();
  Serial.println("Wi-Fi conectado!");

  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

}

// ======================================
// CONEXAO MQTT
// ======================================

void conectarMQTT() {

  if (client.connected()) {
    return;
  }

  Serial.print("Conectando ao ThingsBoard...");

  if (client.connect(
    "AlertadeEnchente-ESP32",
    DEVICE_TOKEN,
    ""
  )) {

    Serial.println("Conectado!");

  } else {

    Serial.print("Falha MQTT: ");
    Serial.println(client.state());

  }

}

// ======================================
// LEITURA DO SENSOR ULTRASSONICO
// ======================================

float lerDistancia() {

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  long duracao = pulseIn(
    ECHO_PIN,
    HIGH,
    30000
  );

  if (duracao == 0) {
    return -1;
  }

  float distanciaCM = duracao * 0.0343 / 2;

  return distanciaCM;

}

// ======================================
// CALCULO DO NIVEL DA AGUA
// ======================================

void calcularNivelAgua() {

  distancia = lerDistancia();

  if (distancia < 0) {

    Serial.println("Erro na leitura ultrassonica!");
    return;

  }

  nivelAgua =
    ((ALTURA_CANAL_CM - distancia) /
    ALTURA_CANAL_CM) * 100.0;

  nivelAgua = constrain(nivelAgua, 0, 100);

}

// ======================================
// LEITURA DA CHUVA
// ======================================

void lerChuva() {

  int leitura = analogRead(RAIN_PIN);

  intensidadeChuva = map(
    leitura,
    0,
    4095,
    0,
    100
  );

}

// ======================================
// CLASSIFICACAO DOS ALERTAS
// ======================================

void verificarAlerta() {

  Serial.println("=== TESTE DE CLASSIFICACAO ===");

  Serial.print("Nivel recebido: ");
  Serial.println(nivelAgua);

  Serial.print("Chuva recebida: ");
  Serial.println(intensidadeChuva);

  if (nivelAgua >= 70) {

    nivelAlerta = "PERIGO";

  }

  else if (
    nivelAgua >= 40 ||
    (intensidadeChuva >= 80 && nivelAgua >= 30)
  ) {

    nivelAlerta = "ATENCAO";

  }

  else {

    nivelAlerta = "NORMAL";

  }

  Serial.print("Alerta calculado: ");
  Serial.println(nivelAlerta);

  Serial.println("==============================");
}

// ======================================
// CONTROLE DOS LEDS E BUZZER
// ======================================

void controlarAlertas() {

  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(LED_RED, LOW);

  if (nivelAlerta == "NORMAL") {

    digitalWrite(LED_GREEN, HIGH);
    noTone(BUZZER_PIN);

    alarme = false;

  }

  else if (nivelAlerta == "ATENCAO") {

    digitalWrite(LED_YELLOW, HIGH);
    noTone(BUZZER_PIN);

    alarme = false;

  }

  else {

    digitalWrite(LED_RED, HIGH);

    tone(BUZZER_PIN, 2000);

    alarme = true;

  }

}

// ======================================
// ATUALIZACAO DO DISPLAY OLED
// ======================================

void atualizarDisplay() {

  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.println("ALERTA");

  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  display.setCursor(0, 15);
  display.print("Agua: ");
  display.print(nivelAgua, 1);
  display.println("%");

  display.setCursor(0, 27);
  display.print("Chuva: ");
  display.print(intensidadeChuva);
  display.println("%");

  display.setCursor(0, 39);
  display.print("Dist: ");
  display.print(distancia, 1);
  display.println(" cm");

  display.setCursor(0, 53);
  display.print("Status: ");
  display.println(nivelAlerta);

  display.display();

}

// ======================================
// ENVIO DOS DADOS MQTT
// ======================================

void enviarMQTT() {

  if (!client.connected()) {
    conectarMQTT();
  }

  if (!client.connected()) {
    return;
  }

  char mensagem[200];

  snprintf(
    mensagem,
    sizeof(mensagem),
    "{\"water_level\":%.1f,"
    "\"distance_cm\":%.1f,"
    "\"rain_intensity\":%d,"
    "\"alert_level\":\"%s\","
    "\"alarm\":%s}",
    nivelAgua,
    distancia,
    intensidadeChuva,
    nivelAlerta.c_str(),
    alarme ? "true" : "false"
  );

  bool enviado = client.publish(
    MQTT_TOPIC,
    mensagem
  );

  if (enviado) {

    Serial.println("Dados enviados ao ThingsBoard:");
    Serial.println(mensagem);

  } else {

    Serial.println("Falha no envio MQTT!");

  }

}

// ======================================
// SETUP
// ======================================

void setup() {

  Serial.begin(115200);

  // Configuracao dos pinos

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_RED, OUTPUT);

  pinMode(BUZZER_PIN, OUTPUT);

  

  analogReadResolution(12);

  // Inicializacao do display

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(
    SSD1306_SWITCHCAPVCC,
    0x3C
  )) {

    Serial.println("Erro ao iniciar OLED!");

    while (true) {
      delay(100);
    }




    

  }

  

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Alerta de Enchente");
  display.println("Inicializando...");
  display.display();

  // Conexoes

  conectarWiFi();

  client.setServer(
    MQTT_SERVER,
    MQTT_PORT
  );

  Serial.println("Alerta de Enchente iniciado!");

}

// ======================================
// LOOP PRINCIPAL
// ======================================

void loop() {

  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
  }

  if (!client.connected()) {
    conectarMQTT();
  }

  client.loop();

  unsigned long agora = millis();

  if (agora - ultimoEnvio >= intervaloEnvio) {

    ultimoEnvio = agora;

    // Ler sensores

    calcularNivelAgua();

    lerChuva();

    // Processar informacoes

    if (distancia >= 0) {

      verificarAlerta();

      controlarAlertas();

      atualizarDisplay();

      enviarMQTT();

      // Monitor serial

      Serial.println("----------------------");

      Serial.print("Nivel da agua: ");
      Serial.print(nivelAgua);
      Serial.println("%");

      Serial.print("Intensidade da chuva: ");
      Serial.print(intensidadeChuva);
      Serial.println("%");

      Serial.print("Alerta: ");
      Serial.println(nivelAlerta);

      Serial.println("----------------------");

    }

  }

  delay(10);

}