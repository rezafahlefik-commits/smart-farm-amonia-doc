// ======================================================
// LIBRARY
// ======================================================
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <math.h>

// ======================================================
// KONEKSI WIFI
// ======================================================
char ssid[] = "RRC";
char pass[] = "Rejarajacrit";

// ======================================================
// CONFIG SERVER NODE.JS (EXPRESS)
// ======================================================
// SILAKAN GANTI "192.168.1.100" DENGAN IP LAPTOP/SERVER ANDA
String SERVER_URL = "http://192.168.18.139:5000/api/update";

// ======================================================
// KONFIGURASI PIN HARDWARE
// ======================================================
#define DHTPIN 4
#define DHTTYPE DHT22

#define MQ135_PIN 34
#define RELAY_PIN 27

// SWITCH 3 POSISI
#define SWITCH_ON_PIN 18
#define SWITCH_OFF_PIN 19

// ======================================================
// INISIALISASI SENSOR & LCD
// ======================================================
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Manajemen Waktu Rutin (Menggantikan BlynkTimer agar Non-Blocking)
unsigned long lastRoutineTime = 0;
const long routineInterval = 4000; // Kirim rutin otomatis ke database setiap 5 menit (300.000 ms)

// ======================================================
// KONSTANTA MQ135
// ======================================================
#define RL 10.0
#define R0 15.0 

// ======================================================
// BATAS PARAMETER KONTROL FAN (OTOMATIS)
// ======================================================
#define FAN_ON_TEMP 35.0
#define FAN_OFF_TEMP 33.0

// ======================================================
// VARIABEL DATA GLOBAL
// ======================================================
float suhu = 0.0;
float kelembaban = 0.0;
int adcValue = 0;
float voltage = 0.0;
float rs = 0.0;
float ratio = 0.0;
float ppm = 0.0;

// STATUS SISTEM
int fanStatus = 0; // 0 = OFF, 1 = ON
int fanMode = 1;   // 0 = MANUAL, 1 = AUTO

// Variabel untuk mendeteksi perubahan status secara instan
int lastFanStatus = -1;
int lastFanMode = -1;

// ======================================================
// FUNGSI KONEKSI WIFI
// ======================================================
void connectWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Menghubungkan ke WiFi...");
    WiFi.begin(ssid, pass);
    
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 16) { // Maksimal tunggu 8 detik di awal
      delay(500);
      Serial.print(".");
      timeout++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n[WIFI] Terhubung!");
      Serial.print("[WIFI] IP Address ESP32: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("\n[WIFI] Gagal terkoneksi (Akan dicoba kembali secara background)");
    }
  }
}

// ======================================================
// FUNGSI KIRIM DATA KE SERVER NODE.JS (POST JSON)
// ======================================================
void sendToExpressServer(String pemicu)
{
  if (WiFi.status() == WL_CONNECTED)
  {
    HTTPClient http;
    http.begin(SERVER_URL);
    http.addHeader("Content-Type", "application/json");

    // Menyusun JSON Body string sesuai struktur req.body Node.js backend Anda
    String jsonBody = "{\"temp\":" + String(suhu, 1) + 
                      ",\"hum\":" + String(kelembaban, 0) + 
                      ",\"nh3\":" + String(ppm, 1) + 
                      ",\"mode\":\"" + String(fanMode == 1 ? "AUTO" : "MANUAL") + "\"" +
                      ",\"fan\":\"" + String(fanStatus == 1 ? "ON" : "OFF") + "\"" +
                      ",\"pemicu\":\"" + pemicu + "\"}";

    Serial.println("[HTTP] Mengirim data log ke Server...");
    int httpCode = http.POST(jsonBody);

    Serial.print("[HTTP] Response Code: ");
    Serial.println(httpCode);
    
    if (httpCode > 0) {
      String response = http.getString();
      Serial.println("[HTTP] Balasan Server: " + response);
    }
    http.end();
  } else {
    Serial.println("[HTTP] Gagal kirim: WiFi tidak tersambung.");
    connectWiFi(); // Coba hubungkan ulang jika mendadak terputus
  }
}

// ======================================================
// SETUP UTAMA
// ======================================================
void setup()
{
  Serial.begin(115200);
  delay(500);
  Serial.println("\n========== STARTING HARDWARE SMART FARM ==========");

  // ======================================================
  // INISIALISASI LCD (Ditempatkan paling atas agar instan menyala)
  // ======================================================
  Wire.begin(21, 22);   // SDA=21, SCL=22
  lcd.init();
  lcd.backlight();
  lcd.clear();
  
  lcd.setCursor(0, 0);
  lcd.print("  SMART FARM   ");
  lcd.setCursor(0, 1);
  lcd.print("SYSTEM STARTING");

  // ======================================================
  // INISIALISASI HARDWARE PIN & SENSOR
  // ======================================================
  dht.begin();
  
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Fan mati saat pertama boot

  pinMode(SWITCH_ON_PIN, INPUT_PULLUP);
  pinMode(SWITCH_OFF_PIN, INPUT_PULLUP);

  // Hubungkan ke Wi-Fi AP router lokal
  connectWiFi();

  delay(1500);
  lcd.clear();
}

// ======================================================
// LOOP PROGRAM UTAMA
// ======================================================
void loop()
{
  // ======================================================
  // 1. BACA SENSOR DHT22 (SUHU & KELEMBABAN)
  // ======================================================
  suhu = dht.readTemperature();
  kelembaban = dht.readHumidity();

  if (isnan(suhu) || isnan(kelembaban))
  {
    Serial.println("[ERROR] Gagal membaca sensor DHT22! Set nilai ke 0.");
    suhu = 0;
    kelembaban = 0;
  }

  // ======================================================
  // 2. BACA SENSOR MQ135 (GAS AMONIA)
  // ======================================================
  adcValue = analogRead(MQ135_PIN);
  voltage = adcValue * (3.3 / 4095.0);
  if (voltage < 0.1) {
    voltage = 0.1;
  }
  rs = ((3.3 - voltage) / voltage) * RL;
  ratio = rs / R0;
  ppm = 116.6020682 * pow(ratio, -2.769034857);
  ppm = ppm * 0.3; // Kalibrasi faktor lingkungan lokal
  
  if (ppm < 0)    { ppm = 0; }
  if (ppm > 1000) { ppm = 1000; }

  // ======================================================
  // 3. BACA FISIK SWITCH 3 POSISI & LOGIKA KONTROL FAN
  // ======================================================
  int readOn = digitalRead(SWITCH_ON_PIN);
  int readOff = digitalRead(SWITCH_OFF_PIN);

  if (readOff == LOW) // POSISI MANUAL OFF
  {
    fanMode = 0;     // MANUAL
    fanStatus = 0;   // OFF
    digitalWrite(RELAY_PIN, LOW);
  }
  else if (readOn == LOW) // POSISI MANUAL ON
  {
    fanMode = 0;     // MANUAL
    fanStatus = 1;   // ON
    digitalWrite(RELAY_PIN, HIGH);
  }
  else // POSISI TENGAH (AUTOMATIC MODE)
  {
    fanMode = 1;     // AUTO
    
    // Aturan Otomatis Kipas menyala berdasarkan Amonia atau Suhu Tinggi
    if (suhu >= FAN_ON_TEMP || ppm >= 15.0) {
      digitalWrite(RELAY_PIN, HIGH);
      fanStatus = 1;
    } 
    // Aturan Otomatis Kipas mati kembali jika parameter aman kembali
    else if (suhu <= FAN_OFF_TEMP && ppm < 9.0) {
      digitalWrite(RELAY_PIN, LOW);
      fanStatus = 0;
    }
  }

  // ======================================================
  // 4. PEMBARUAN DISPLAY LCD REAL-TIME
  // ======================================================
  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print(suhu, 1);
  lcd.print((char)223); // Simbol derajat Celcius
  lcd.print("C ");

  lcd.setCursor(9, 0);
  lcd.print("H:");
  lcd.print(kelembaban, 0);
  lcd.print("% ");

  lcd.setCursor(0, 1);
  lcd.print("NH3:");
  lcd.print(ppm, 1);
  lcd.print(" ");

  // Cetak Status Kipas di LCD
  lcd.setCursor(10, 1);
  lcd.print(fanStatus == 1 ? "ON " : "OFF");

  // Cetak Label Mode di LCD ujung kanan bawah
  lcd.setCursor(14, 1);
  lcd.print(fanMode == 1 ? "[A]" : "[M]");

  // ======================================================
  // 5. SERIAL MONITOR DEBUGGING
  // ======================================================
  Serial.println("========== MONITORING SMART FARM ==========");
  Serial.printf("Suhu Kandang  : %.1f C\n", suhu);
  Serial.printf("Kelembaban    : %.0f %%\n", kelembaban);
  Serial.printf("Gas NH3       : %.1f PPM (ADC: %d | %.2fV)\n", ppm, adcValue, voltage);
  Serial.printf("Mode Kontrol  : %s\n", fanMode == 1 ? "AUTOMATIC" : "MANUAL");
  Serial.printf("Kondisi Fan   : %s\n", fanStatus == 1 ? "MENYALA (ON)" : "MATI (OFF)");
  Serial.println("===========================================\n");

// ======================================================
  // 6. MANAJEMEN DATA TRANSMISI KE EXPRESS (NON-BLOCKING)
  // ======================================================
  unsigned long currentTime = millis();
  
  // Skenario A: Kirim secara cepat setiap 2.5 Detik sekali agar Dashboard Web Real-time
  if (currentTime - lastRoutineTime >= 2500 || lastRoutineTime == 0) {
    sendToExpressServer("Realtime");
    lastRoutineTime = currentTime;
    
    // Sinkronisasi status akhir agar skenario B tidak bentrok
    lastFanStatus = fanStatus;
    lastFanMode = fanMode;
  }
  // Skenario B: Kirim INSTAN interupt jika tiba-tiba ada perubahan saklar fisik sebelum batasan 2.5 detik
  else if (fanStatus != lastFanStatus || fanMode != lastFanMode) {
    sendToExpressServer("Perubahan Hardware");
    
    // Perbarui penanda perubahan terakhir
    lastFanStatus = fanStatus;
    lastFanMode = fanMode;
  }

  delay(3000); // Dipercepat dari 500ms ke 200ms agar pembacaan fisik switch tombol jauh lebih responsif
}
