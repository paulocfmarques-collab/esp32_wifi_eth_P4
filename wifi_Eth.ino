#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <Preferences.h>
#include "esp_system.h"
#include "driver/temperature_sensor.h" 
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ETH.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define MAX_LINHAS 8

// Definição dos pinos I2C para o OLED no ESP32-P4 Dev Kit
#define PIN_SDA 7
#define PIN_SCL 8

// Definições de Hardware padrão para o ESP32-P4 DevKit (PHY IP101)
#define ETH_PHY_TYPE   ETH_PHY_IP101  // Caso apresente erro de ID, use ETH_PHY_GENERIC
#define ETH_PHY_ADDR   1              // Endereço do PHY I2C/SMI
#define ETH_PHY_MDC    31             // Pino Management Data Clock
#define ETH_PHY_MDIO   52             // Pino Management Data Input/Output
#define ETH_PHY_POWER  51             // Pino de controle de energia/Reset do PHY
#define ETH_CLK_MODE   EMAC_CLK_EXT_IN // Modo do clock RMII externo

// Inicializa o objeto do display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define LED 1
#define BOTAO_RESET 2 

bool blinkAtivo = false;
bool estadoLed = false;
unsigned long ultimoToggle = 0;
unsigned long intervaloBlink = 500; 
String historicoLinhas[MAX_LINHAS];
int totalLinhas = 0;
bool oledInicializado = false;

// Variável para monitorar o status da conexão
bool eth_connected = false;

WebServer server(80);
Preferences prefs;

// HTML da página de configuração
const char* htmlPage PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Configuração WiFi</title>
<style>
  body { font-family: Arial, sans-serif; margin: 40px; background-color: #f4f4f9; text-align: center; }
  .container { background: white; max-width: 300px; margin: auto; padding: 20px; border-radius: 8px; box-shadow: 0 4px 8px rgba(0,0,0,0.1); }
  input { width: 100%; padding: 8px; margin: 10px 0; box-sizing: border-box; }
  input[type="submit"] { background: #007bff; color: white; border: none; cursor: pointer; }
</style>
</head>
<body>
<div class="container">
  <h2>Configuração WiFi - ESP32-P4</h2>
  <form action="/salvar" method="POST">
    <label>SSID:</label>
    <input type="text" name="ssid" placeholder="Nome da rede" required>
    <label>Senha:</label>
    <input type="password" name="senha" placeholder="Senha da rede">
    <input type="submit" value="Salvar">
  </form>
</div>
</body>
</html>
)rawliteral";

WiFiUDP udp;
const int udpPort = 4210;

// Função para escrever linha por linha dinamicamente com rolagem automática
void adicionarLinha(String novoTexto) {
  Serial.println("[OLED] " + novoTexto); // Também espelha no Monitor Serial
  
  if (!oledInicializado) return; // Proteção contra ponteiro nulo se o display falhar

  // Move o histórico para cima se a tela estiver cheia
  if (totalLinhas >= MAX_LINHAS) {
    for (int i = 0; i < MAX_LINHAS - 1; i++) {
      historicoLinhas[i] = historicoLinhas[i + 1];
    }
    historicoLinhas[MAX_LINHAS - 1] = novoTexto;
  } else {
    historicoLinhas[totalLinhas] = novoTexto;
    totalLinhas++;
  }

  // Renderiza o histórico atualizado na tela
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  for (int i = 0; i < totalLinhas; i++) {
    display.setCursor(0, i * 8); 
    display.println(historicoLinhas[i]);
  }
  display.display();
}

float lerTemperaturaP4() {
  static temperature_sensor_handle_t temp_sensor = NULL;
  if (temp_sensor == NULL) {
    temperature_sensor_config_t temp_cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    if (temperature_sensor_install(&temp_cfg, &temp_sensor) != ESP_OK) {
      return 0.0;
    }
  }
  float tsens_out;
  temperature_sensor_enable(temp_sensor);
  temperature_sensor_get_celsius(temp_sensor, &tsens_out);
  temperature_sensor_disable(temp_sensor);
  return tsens_out;
}

// Função de callback para monitorar eventos de rede
void onNetworkEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("Interface Ethernet iniciada.");
      adicionarLinha("Interface Ethernet iniciada.");
      // Opcional: Define o nome do dispositivo na rede
      ETH.setHostname("esp32-p4-node");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("Cabo Ethernet conectado!");
      adicionarLinha("Cabo Ethernet conectado!");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("IP obtido via DHCP: ");
      Serial.println(ETH.localIP());
      adicionarLinha("IP obtido via DHCP: ");
      adicionarLinha(ETH.localIP().toString().c_str());
      eth_connected = true;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("Cabo Ethernet desconectado.");
      adicionarLinha("Cabo Ethernet desconectado.");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("Interface Ethernet parada.");
      adicionarLinha("Interface Ethernet parada.");
      eth_connected = false;
      break;
    default:
      break;
  }
}

void salvarWifi() {
  adicionarLinha("Salvando rede...");
  String novoSSID = server.arg("ssid");
  String novaSenha = server.arg("senha");

  prefs.begin("wifi", false);
  prefs.putString("ssid", novoSSID);
  prefs.putString("senha", novaSenha);
  prefs.end();

  server.send(200, "text/html", "<h2>Configuracao salva! Reiniciando...</h2>");
  delay(2000);
  ESP.restart();
}

void iniciarPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32_P4_CONFIG");
  
  adicionarLinha("Portal Ativo!");
  adicionarLinha("Wifi: ESP32_P4_CONFIG");
  adicionarLinha("IP: 192.168.4.1");

  server.on("/", HTTP_GET, []() {
      server.send(200, "text/html", htmlPage);
  });
  server.on("/salvar", HTTP_POST, salvarWifi);
  server.begin();
}

bool conectarWifi() {
  prefs.begin("wifi", true);
  String ssid = prefs.getString("ssid", "");
  String password = prefs.getString("senha", "");
  prefs.end();

  if (ssid == "") return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  
  adicionarLinha("Conectando a:");
  adicionarLinha(ssid);

  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
    delay(500);
    digitalWrite(LED, !digitalRead(LED)); 
    tentativas++;
  }
  digitalWrite(LED, LOW);
  return WiFi.status() == WL_CONNECTED;
}

void zerarConfiguracoes() {
  adicionarLinha("Limpando Memoria...");

  prefs.begin("wifi", false);
  prefs.clear(); 
  prefs.end();
  
  udp.beginPacket(udp.remoteIP(), udp.remotePort());
  udp.print("WiFi zerado. Reiniciando...\n");
  udp.endPacket();
  
  for(int i=0; i<10; i++) {
    digitalWrite(LED, HIGH); delay(100);
    digitalWrite(LED, LOW); delay(100);
  }
  ESP.restart(); 
}

void executa_comando(String cmd) {
  adicionarLinha("> " + cmd);

  if (cmd == "RESET_WIFI") {
    zerarConfiguracoes(); 
  }
  else if (cmd == "LED_ON") {
    blinkAtivo = false;
    estadoLed = true;
    digitalWrite(LED, HIGH);
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.print("LED ligado\n");
    udp.endPacket();
  }
  else if (cmd == "LED_OFF") {
    blinkAtivo = false;
    estadoLed = false;
    digitalWrite(LED, LOW);
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.print("LED desligado\n");
    udp.endPacket();
  }
  else if (cmd == "TEMP") {
    float t = lerTemperaturaP4();
    adicionarLinha("Temp: " + String(t) + "C");
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.printf("CPU Temp: %.2f\n", t);
    udp.endPacket();
  }
  else if (cmd == "CPU") // Informações sobre a CPU
  {
    //Serial.println(udp.remoteIP());
    //Serial.println(udp.remotePort());
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.printf("Modelo: %s\n", ESP.getChipModel());
    udp.printf("Revisao: %d\n", ESP.getChipRevision());
    udp.printf("Nucleos: %d\n", ESP.getChipCores());
    udp.printf("CPU: %d MHz\n", ESP.getCpuFreqMHz());
    udp.printf("RAM livre: %u bytes\n", ESP.getFreeHeap());
    udp.endPacket();
  }
  else if (cmd == "RAM") // Informações sobre a RAM
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.printf("Heap livre: %u\n", ESP.getFreeHeap());
    udp.printf("Menor heap livre: %u\n", ESP.getMinFreeHeap());
    udp.printf("Maior bloco livre: %u\n", ESP.getMaxAllocHeap());
    udp.endPacket();
  }
  else if (cmd == "FLASH") // Informações sobre a flash
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.printf("Flash total: %u\n", ESP.getFlashChipSize());
    udp.printf("Velocidade Flash: %u\n", ESP.getFlashChipSpeed());
    udp.printf("Tamanho Sketch: %u\n", ESP.getSketchSize());
    udp.printf("Espaco livre: %u\n", ESP.getFreeSketchSpace());
    udp.endPacket();
  }
  else if (cmd == "INIT") // Motivo do reset
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.printf("Motivo reset: %d\n", esp_reset_reason());    
    udp.endPacket();
  }
  else if (cmd == "UPTIME") // Tempo ligado
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.printf("Uptime: %lu ms\n", millis());    
    udp.endPacket();
  }
  else if (cmd == "MAC") // MAC address
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.printf("MAC: ");
    udp.println(WiFi.macAddress());
    udp.endPacket();
  }
  else if (cmd == "NET_INFO") // MAC address
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.printf("IP: ");
    udp.println(WiFi.localIP());
    udp.printf("Gateway: ");
    udp.println(WiFi.gatewayIP());
    udp.printf("Mascara de rede: ");
    udp.println(WiFi.subnetMask());
    udp.printf("RSSI: %d dbm\n", WiFi.RSSI());
    udp.printf("Nome da Rede: %s\n", WiFi.SSID());
    udp.endPacket();
    }  
  else if (cmd.startsWith("LED_PISCA")) // Comando para piscar o LED uma quantidade de vezes
  {
    int piscadas = 10;
    int tempo = 250;

    int p1 = cmd.indexOf(':');
    int p2 = cmd.indexOf(':', p1 + 1);

    if (p1 > 0 && p2 > 0) 
    {
      piscadas = cmd.substring(p1 + 1, p2).toInt();
      tempo = cmd.substring(p2 + 1).toInt();
    }

    for (int i = 0; i < piscadas; i++) 
    {
      digitalWrite(LED, HIGH);  delay(tempo);
      digitalWrite(LED, LOW);   delay(tempo);
    }

    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.printf("LED piscou %d vezes com %d ms\n", piscadas, tempo);
    udp.endPacket();
  }
  else if (cmd.startsWith("LED_BLINK")) // Comando para piscar led com tempo
  {
    int p = cmd.indexOf(':');

    if (p > 0) 
    {
      intervaloBlink = cmd.substring(p + 1).toInt();
    }

    blinkAtivo = true;

    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.printf("Blink iniciado (%lu ms)\n", intervaloBlink);
    udp.endPacket();
  }
  else 
  {
    udp.beginPacket(udp.remoteIP(), udp.remotePort());
    udp.print("Comando Invalido\n");
    udp.endPacket();
  }
}

void setup() {
  Serial.begin(115200);
  delay(500); // Delay crucial para estabilização elétrica do chip C6

  pinMode(LED, OUTPUT);
  pinMode(BOTAO_RESET, INPUT_PULLUP); 

  // Inicializa o barramento I2C explicitando os pinos do P4
  Wire.begin(PIN_SDA, PIN_SCL); 
  if(display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oledInicializado = true;
    display.clearDisplay();
    display.display();
    adicionarLinha("OLED Pronto!");
  } else {
    Serial.println("Falha ao encontrar o Display OLED nos pinos mapeados!");
  }

  WiFi.onEvent(onNetworkEvent);
  // Inicializa o hardware Ethernet com os parâmetros da placa
  ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_POWER, ETH_CLK_MODE);
  ETH.setDefault();
  Serial.println("Prioridade de rota definida: Ethernet possui preferência sobre o Wi-Fi.");
  
  if (conectarWifi()) {
    adicionarLinha("Wifi Conectado!");
    adicionarLinha(WiFi.localIP().toString());
    Serial.println("Wifi Conectado!");
    Serial.println(WiFi.localIP().toString());
    udp.begin(udpPort);
  } else {
    iniciarPortal();
  }
}

void loop() {
  if (WiFi.getMode() == WIFI_AP) {
    server.handleClient();
  }

  int packetSize = udp.parsePacket();
  if (packetSize) {
    char packetBuffer[255];
    int len = udp.read(packetBuffer, 255);
    if (len > 0) {
      packetBuffer[len] = 0;
    }
    String comando = String(packetBuffer);
    comando.trim();
    executa_comando(comando);
  }

  if (digitalRead(BOTAO_RESET) == LOW) {
    delay(50); 
    if (digitalRead(BOTAO_RESET) == LOW) {
      zerarConfiguracoes();
    }
  }

  if (blinkAtivo) {
    unsigned long atual = millis();
    if (atual - ultimoToggle >= intervaloBlink) {
      ultimoToggle = atual;
      estadoLed = !estadoLed;
      digitalWrite(LED, estadoLed);
    }
  }
}
