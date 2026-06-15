#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include "RTClib.h"
#include <Adafruit_NeoPixel.h>

/* hardware
Anello 60 LED WS2812B:
VCC → Alimentatore esterno 5V (da evitare il pin 5V dell'ESP32 per 60 LED accesi insieme).
GND → GND dell'Alimentatore e GND dell'ESP32 (massa in comune obbligatoria).
DIN (Data In) → GPIO 13 dell'ESP32 (puoi cambiarlo nel codice).
Modulo RTC (Consigliato DS3231 per la precisione):
VCC → Pin 3V3 dell'ESP32.
GND → GND dell'ESP32.
SDA → GPIO 21 dell'ESP32.
SCL → GPIO 22 dell'ESP32.
*/

// Configurazione LED
#define LED_PIN        13
#define NUM_LEDS       60
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Istanza RTC e Web Server
RTC_DS3231 rtc;
WebServer server(80);

// --- MODIFICA QUESTE RIGHE CON I DATI DEL TUO ROUTER ---
const char* ssid = "NOME_DELLA_TUA_RETE_WIFI";
const char* password = "PASSWORD_DEL_TUO_WIFI";

// Configurazione IP Statico (Richiesto: 192.168.0.9)
IPAddress ipStatico(192, 168, 0, 9); 
IPAddress gateway(192, 168, 0, 1);    // Di solito l'IP del router è .1
IPAddress subnet(255, 255, 255, 0);   // Maschera di sottorete standard
IPAddress dns1(1, 1, 1, 1);           // DNS primario (Cloudflare)
IPAddress dns2(8, 8, 8, 8);           // DNS secondario (Google)
// ------------------------------------------------------

// Colori personalizzabili (Formato: Rosso, Verde, Blu)
const uint32_t COLORE_ORE    = strip.Color(255, 0, 0);     // Rosso
const uint32_t COLORE_MINUTI = strip.Color(0, 0, 255);     // Blu
const uint32_t COLORE_MARKER = strip.Color(20, 20, 20);     // Bianco tenue
const uint32_t COLORE_SFONDO = strip.Color(0, 0, 0);       // Spento

// Pagina Web con Script di Sincronizzazione automatica
const char HTML_PAGINA[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Sincronizza Orologio</title>
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; background-color: #f0f0f0; }
    button { padding: 15px 25px; font-size: 18px; cursor: pointer; background-color: #4CAF50; color: white; border: none; border-radius: 5px; }
    button:hover { background-color: #45a049; }
    p { font-size: 16px; color: #333; }
  </style>
</head>
<body>
  <h2>Sincronizzazione Orologio LED</h2>
  <p>Clicca il pulsante per inviare l'ora del tuo dispositivo all'orologio.</p>
  <button onclick="inviaOra()">Sincronizza Ora</button>
  <p id="stato"></p>

  <script>
    function inviaOra() {
      const adesso = new Date();
      const anno = adesso.getFullYear();
      const mese = adesso.getMonth() + 1;
      const giorno = adesso.getDate();
      const ore = adesso.getHours();
      const minuti = adesso.getMinutes();
      const secondi = adesso.getSeconds();

      const url = `/set?y=${anno}&m=${mese}&d=${giorno}&h=${ore}&i=${minuti}&s=${secondi}`;
      
      fetch(url)
        .then(response => response.text())
        .then(data => {
          document.getElementById("stato").innerText = "Sincronizzazione riuscita! " + data;
          document.getElementById("stato").style.color = "green";
        })
        .catch(err => {
          document.getElementById("stato").innerText = "Errore di connessione.";
          document.getElementById("stato").style.color = "red";
        });
    }
  </script>
</body>
</html>
)rawliteral";

void gestisciRoot() {
  server.send(200, "text/html", HTML_PAGINA);
}

void gestisciImpostazioneOra() {
  if (server.hasArg("y") && server.hasArg("m") && server.hasArg("d") && 
      server.hasArg("h") && server.hasArg("i") && server.hasArg("s")) {
    
    int anno = server.arg("y").toInt();
    int mese = server.arg("m").toInt();
    int giorno = server.arg("d").toInt();
    int ore = server.arg("h").toInt();
    int minuti = server.arg("i").toInt();
    int secondi = server.arg("s").toInt();

    rtc.adjust(DateTime(anno, mese, giorno, ore, minuti, secondi));
    
    String risposta = "Ora impostata: " + String(ore) + ":" + String(minuti) + ":" + String(secondi);
    server.send(200, "text/plain", risposta);
  } else {
    server.send(400, "text/plain", "Parametri mancanti");
  }
}

void aggiornaQuadrante(int ore, int minuti) {
  for (int i = 0; i < NUM_LEDS; i++) {
    strip.setPixelColor(i, COLORE_SFONDO);
  }

  for (int i = 0; i < NUM_LEDS; i += 5) {
    strip.setPixelColor(i, COLORE_MARKER);
  }

  int ledOre = ((ore % 12) * 5) + (minuti / 12);
  int ledMinuti = minuti;

  strip.setPixelColor(ledMinuti, COLORE_MINUTI);
  strip.setPixelColor(ledOre, COLORE_ORE);

  if (ledOre == ledMinuti) {
    strip.setPixelColor(ledOre, strip.Color(255, 0, 255)); // Viola se sovrapposti
  }

  strip.show();
}

void setup() {
  Serial.begin(9600);
  
  strip.begin();
  strip.setBrightness(50);
  strip.show();

  if (!rtc.begin()) {
    Serial.println("Impossibile trovare il modulo RTC!");
    //while (1); inizialmente non monto rtc
  }

  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // --- CONFIGURAZIONE RETE LOCALE ---
  // Applica l'IP statico prima di connettersi
  if (!WiFi.config(ipStatico, gateway, subnet, dns1, dns2)) {
    Serial.println("Errore nella configurazione dell'IP Statico!");
  }

  Serial.print("Connessione a: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  // Attendi la connessione (lampeggio di cortesia sul LED 0)
  int ledStatus = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    strip.setPixelColor(0, ledStatus ? strip.Color(50, 50, 0) : strip.Color(0,0,0));
    strip.show();
    ledStatus = !ledStatus;
  }
  
  Serial.println("\nWi-Fi Connesso!");
  Serial.print("Indirizzo IP fisso dell'orologio: ");
  Serial.println(WiFi.localIP());

  // Rotte del Web Server
  server.on("/", gestisciRoot);
  server.on("/set", gestisciImpostazioneOra);
  server.begin();
}

void loop() {
  server.handleClient();

  static unsigned long ultimoAggiornamento = 0;
  if (millis() - ultimoAggiornamento >= 1000) {
    ultimoAggiornamento = millis();
    
    DateTime oraAttuale = rtc.now();
    aggiornaQuadrante(oraAttuale.hour(), oraAttuale.minute());
  }
}
