#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <IRremote.hpp>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <ArduinoOTA.h>

#define IR_SEND_PIN 4 // Pino IR LED
#define DEFAULT_WIFI_SSID "controle"
#define DEFAULT_WIFI_PASSWORD "12345678"

WebServer server(80);

const char* ap_ssid = "controle";
const char* ap_password = "12345678";

bool isAPMode = false;
String networkSSID = "";
String networkPass = "";

const char* html_page_root = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Controle Remoto</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 20px;
            padding: 0;
            background-color: #f4f4f9;
        }
        h2 {
            text-align: center;
            color: #333;
        }
        #controls {
            display: flex;
            flex-wrap: wrap;
            justify-content: center;
        }
        .device-group {
            background-color: #ffffff;
            border-radius: 8px;
            padding: 10px;
            margin: 10px;
            box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1);
            width: 200px;
        }
        .device-title {
            text-align: center;
            font-weight: bold;
            color: #333;
        }
        button {
            width: 100%;
            padding: 8px;
            margin: 5px 0;
            background-color: #4CAF50;
            color: white;
            border: none;
            border-radius: 5px;
            cursor: pointer;
            font-size: 14px;
        }
        button:hover {
            background-color: #45a049;
        }
        a {
            display: block;
            text-align: center;
            margin-top: 20px;
            color: #007bff;
            text-decoration: none;
        }
        a:hover {
            text-decoration: underline;
        }
    </style>
    <script>
        async function sendCommand(device, command) {
            await fetch(`/send?device=${device}&command=${command}`);
        }

        async function loadControls() {
            let response = await fetch('/json');
            let json = await response.json();
            let container = document.getElementById("controls");
            container.innerHTML = "";
            for (let device in json) {
                let deviceGroup = document.createElement("div");
                deviceGroup.classList.add("device-group");

                let deviceTitle = document.createElement("div");
                deviceTitle.classList.add("device-title");
                deviceTitle.textContent = device;
                deviceGroup.appendChild(deviceTitle);

                for (let command in json[device]["comand"]) {
                    let button = document.createElement("button");
                    button.textContent = `${command}`;
                    button.onclick = () => sendCommand(device, command);
                    deviceGroup.appendChild(button);
                }

                container.appendChild(deviceGroup);
            }
        }

        window.onload = loadControls;
    </script>
</head>
<body>
    <h2>Controle Remoto</h2>
    <div id="controls"></div>
    <br>
    <a href="/config">Ir para configuração</a>
</body>
</html>
)rawliteral";


const char* html_page_config = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Configuração do Controle Remoto</title>
    <script>
        async function updateJson() {
            let response = await fetch('/json');
            let json = await response.json();
            document.getElementById("jsonEditor").value = JSON.stringify(json, null, 2);
        }
        async function saveJson() {
            let json = document.getElementById("jsonEditor").value;
            await fetch('/saveJson', { method: 'POST', body: json });
            alert("Configurações salvas!");
        }
        async function saveWiFi() {
            let ssid = document.getElementById("ssid").value;
            let pass = document.getElementById("password").value;
            await fetch('/saveWiFi', { method: 'POST', body: JSON.stringify({ ssid, pass }) });
            alert("Configurações de WiFi salvas! Reiniciando...");
        }
        async function startOTA() {
            let password = document.getElementById("otaPassword").value;
            await fetch(`/startOTA?password=${password}`);
            alert("Iniciando OTA...");
        }
        window.onload = updateJson;
    </script>
</head>
<body>
    <h2>Configuração do Controle Remoto</h2>
    <h3>Configuração de Rede WiFi</h3>
    <label>SSID: <input type="text" id="ssid"></label><br>
    <label>Senha: <input type="password" id="password"></label><br>
    <button onclick="saveWiFi()">Salvar WiFi</button>
    
    <h3>Configuração de Controles</h3>
    <textarea id="jsonEditor" rows="10" cols="50"></textarea><br>
    <button onclick="saveJson()">Salvar Controles</button>
    
    <h3>Instalação via OTA</h3>
    <label>Senha OTA: <input type="password" id="otaPassword"></label><br>
    <button onclick="startOTA()">Iniciar OTA</button>
    <br><br>
    <a href="/">Voltar para controle remoto</a>
</body>
</html>
)rawliteral";

StaticJsonDocument<1024> controles;
StaticJsonDocument<256> wifiConfig;

void loadJson() {
    if (SPIFFS.begin(true)) {
        File file = SPIFFS.open("/controles.json", "r");
        if (file) {
            deserializeJson(controles, file);
            file.close();
        }
        file = SPIFFS.open("/wifi.json", "r");
        if (file) {
            deserializeJson(wifiConfig, file);
            file.close();
            networkSSID = wifiConfig["ssid"].as<String>();
            networkPass = wifiConfig["pass"].as<String>();
        }
    }
}

void saveJson() {
    File file = SPIFFS.open("/controles.json", "w");
    if (file) {
        serializeJson(controles, file);
        file.close();
    }
}

void saveWiFiConfig() {
    File file = SPIFFS.open("/wifi.json", "w");
    if (file) {
        serializeJson(wifiConfig, file);
        file.close();
    }
}

void handleRoot() {
    String page = html_page_root;
    server.send(200, "text/html", page);
}

void handleConfigPage() {
    String page = html_page_config;
    server.send(200, "text/html", page);
}

void handleJson() {
    String json;
    serializeJson(controles, json);
    server.send(200, "application/json", json);
}

void handleSaveJson() {
    if (server.hasArg("plain")) {
        DeserializationError error = deserializeJson(controles, server.arg("plain"));
        if (!error) {
            saveJson();
            server.send(200, "text/plain", "JSON atualizado com sucesso!");
        } else {
            server.send(400, "text/plain", "Erro ao processar JSON");
        }
    }
}

void handleSaveWiFi() {
    if (server.hasArg("plain")) {
        DeserializationError error = deserializeJson(wifiConfig, server.arg("plain"));
        if (!error) {
            saveWiFiConfig();
            server.send(200, "text/plain", "Configuração de WiFi salva! Reinicie o ESP32.");
        } else {
            server.send(400, "text/plain", "Erro ao processar configuração WiFi");
        }
    }
}

void handleSendCommand() {
    String device = server.arg("device");
    String command = server.arg("command");

    if (controles.containsKey(device) && controles[device]["comand"].containsKey(command)) {
        uint32_t code = strtoul(controles[device]["comand"][command].as<const char *>(), NULL, 16);
        
        if (String(controles[device]["protocol"]) == "NEC") {
            uint8_t address = code & 0xFF;   // Extrai os primeiros 8 bits
            uint8_t command = (code >> 16) & 0xFF;  // Extrai os próximos 8 bits

            Serial.print("Address: ");
            Serial.println(address, HEX);   // Vai mostrar 0x01
            Serial.print("Command: ");
            Serial.println(command, HEX);   // Vai mostrar 0xFE

            IrSender.sendNEC(address, command, 0);
        }
        else if (String(controles[device]["protocol"]) == "Sony") {
            uint8_t address = (code >> 8) & 0xFF;  // Extrai os 8 primeiros bits para o endereço
            uint8_t command = code & 0xFF;  // Extrai os 8 últimos bits para o comando

            Serial.print("Address: ");
            Serial.println(address, HEX);  // Exibe o endereço
            Serial.print("Command: ");
            Serial.println(command, HEX);  // Exibe o comando

            //Protocol=Sony Address=0x10 Command=0x15 Raw-Data=0x815 12 bits LSB first
            //Send with: IrSender.sendSony(0x10, 0x15, 2, 12);

            IrSender.sendSony(address, command, 2, 12);  // Envia o código Sony com 12 bits, LSB primeiro
        }
        else {
            JsonObject config = controles[device]["config"];
            Serial.print(code);
            IrSender.sendPulseDistanceWidth(
                config["frequencia"], config["pulso_marcacao"], config["intervalo_marcacao"],
                config["pulso_espaco"], config["intervalo_espaco"], config["pulso_inicio"],
                config["intervalo_inicio"], code, config["tt"], PROTOCOL_IS_LSB_FIRST, 0, 0);
        }
    }
    server.send(200, "text/plain", "OK");
}


void setupWiFi() {
    WiFi.begin(networkSSID.c_str(), networkPass.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(500);
        Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nConectado à rede WiFi!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    } else {
        WiFi.softAP(ap_ssid, ap_password);
        isAPMode = true;
        Serial.println("\nFalha na conexão. Modo AP ativado!");
        Serial.print("IP do AP: ");
        Serial.println(WiFi.softAPIP());
    }
}

void setupOTA() {
    ArduinoOTA.onStart([]() {
        String type = ArduinoOTA.getCommand() == U_FLASH ? "flash" : "spiffs";
        Serial.println("Iniciando OTA para " + type);
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("\nConcluído OTA.");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progresso: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        String errorMsg = "Erro OTA: ";
        if (error == OTA_AUTH_ERROR) {
            errorMsg += "Falha de autenticação";
        } else if (error == OTA_BEGIN_ERROR) {
            errorMsg += "Falha ao iniciar";
        } else if (error == OTA_CONNECT_ERROR) {
            errorMsg += "Falha de conexão";
        } else if (error == OTA_RECEIVE_ERROR) {
            errorMsg += "Falha ao receber";
        } else if (error == OTA_END_ERROR) {
            errorMsg += "Falha ao finalizar";
        }
        Serial.println(errorMsg);
    });
    ArduinoOTA.begin();
}

void handleStartOTA() {
    String otaPassword = server.arg("password");
    if (otaPassword == "12345678") {
        ArduinoOTA.begin();
        server.send(200, "text/plain", "OTA Iniciada!");
    } else {
        server.send(400, "text/plain", "Senha inválida!");
    }
}

void setup() {
    Serial.begin(115200);
    IrSender.begin(IR_SEND_PIN);
    loadJson();
    setupWiFi();

    server.on("/", handleRoot);
    server.on("/json", handleJson);
    server.on("/saveJson", HTTP_POST, handleSaveJson);
    server.on("/saveWiFi", HTTP_POST, handleSaveWiFi);
    server.on("/send", handleSendCommand);
    server.on("/config", HTTP_GET, handleConfigPage);
    server.on("/startOTA", handleStartOTA);

    server.begin();
    setupOTA();  // Inicializa a OTA
}

void loop() {
    server.handleClient();
    ArduinoOTA.handle();
}

/*
  "somsony": {
    "protocol": "Sony",
    "comand": {
      "power": "0x815"
    }
  }
*/
