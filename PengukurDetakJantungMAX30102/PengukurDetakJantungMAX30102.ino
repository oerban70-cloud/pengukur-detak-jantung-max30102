// PengukurDetakJantungMAX30102 — Pengukur detak jantung + SpO2 (ESP32 + MAX30102)
// ---------------------------------------------------------------------------
// Tempelkan ujung jari pada sensor MAX30102. Sensor menembakkan cahaya merah
// dan inframerah ke jari dan mengukur pantulannya; dari pantulan itu ESP32
// menghitung detak jantung (BPM) dan kadar oksigen darah (SpO2) dengan
// algoritma Maxim, lalu menampilkannya di LCD I2C 16x2.
//
// Detak istirahat normal: 60-100 BPM. SpO2 normal: 95-100%.
// Kalau detak keluar dari 60-100 BPM atau SpO2 di bawah 95%, buzzer berbunyi
// dan LED merah menyala; kalau normal, LED hijau menyala.
//
// CATATAN PENTING: alat ini hanya untuk edukasi dan pemantauan santai, BUKAN
// alat medis dan BUKAN untuk diagnosis. Kalau merasa tidak enak badan,
// periksa ke tenaga medis.
//
// Wiring (lihat schematic.png):
//   Sensor MAX30102  VIN -> 3V3, GND -> GND, SDA -> GPIO21, SCL -> GPIO22
//   LCD 16x2 I2C     VCC -> 5V (VIN), GND -> GND, SDA -> GPIO21, SCL -> GPIO22
//   (sensor dan LCD berbagi bus I2C yang sama; alamat LCD 0x27, sensor 0x57)
//   Buzzer aktif HIGH   + -> GPIO25, - -> GND
//   LED hijau (seri resistor 220 ohm) anoda -> GPIO32, katoda -> GND
//   LED merah  (seri resistor 220 ohm) anoda -> GPIO33, katoda -> GND
//   ESP32 dicatu USB 5V.
//
// Library yang dipakai:
//   SparkFun MAX3010x Pulse and Proximity Sensor Library 1.1.2
//   LiquidCrystal_I2C 1.1.2
//   (lihat folder libraries/)
// ---------------------------------------------------------------------------

#include <Wire.h>
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include <LiquidCrystal_I2C.h>

// ------------------------- Konfigurasi -------------------------------------
// Pin I2C (bus bawaan ESP32, dipakai bersama sensor dan LCD).
constexpr uint8_t PIN_SDA = 21;  // SDA: sensor MAX30102 + LCD
constexpr uint8_t PIN_SCL = 22;  // SCL: sensor MAX30102 + LCD

// Pin aktuator dan indikator.
constexpr uint8_t PIN_BUZZER    = 25;  // buzzer aktif HIGH
constexpr uint8_t PIN_LED_HIJAU = 32;  // LED hijau: kondisi normal
constexpr uint8_t PIN_LED_MERAH = 33;  // LED merah: kondisi tidak normal

// Alamat I2C dan ambang pengukuran.
constexpr uint8_t  ALAMAT_LCD_I2C  = 0x27;  // alamat backpack LCD PCF8574
constexpr uint8_t  JUMLAH_SAMPEL   = 100;   // 100 sampel = 4 detik @ 25 sampel/detik
constexpr uint8_t  SAMPEL_BARU     = 25;    // sampel baru tiap hitung ulang (1 detik)
constexpr uint32_t BATAS_JARI_IR   = 50000; // sinyal IR di bawah ini = jari tidak menempel
constexpr int32_t  BPM_MIN_NORMAL  = 60;    // detak istirahat normal, bawah
constexpr int32_t  BPM_MAKS_NORMAL = 100;   // detak istirahat normal, atas
constexpr int32_t  SPO2_MIN_NORMAL = 95;    // SpO2 normal, bawah

// Pengaturan sensor MAX30102 (lihat datasheet Maxim MAX30102).
constexpr uint8_t  KECERAHAN_LED   = 60;    // 0 = mati s.d. 255 = 50 mA
constexpr uint8_t  RATA_RATA_SAMPEL = 4;    // rata-rata 4 sampel (efektif 25 sampel/detik)
constexpr uint8_t  MODE_LED        = 2;     // 2 = LED merah + inframerah
constexpr uint8_t  LAJU_SAMPEL     = 100;   // 100 sampel/detik sebelum dirata-rata
constexpr int      LEBAR_PULSA     = 411;   // lebar pulsa LED, mikrodetik
constexpr int      RENTANG_ADC     = 4096;  // rentang ADC sensor

// Pola bunyi buzzer.
constexpr uint32_t BEEP_ALARM_MS    = 200;  // bunyi tiap beep alarm
constexpr uint32_t JEDA_ALARM_MS    = 200;  // jeda antar beep alarm
constexpr uint8_t  JUMLAH_BEEP_ALARM = 3;   // beep alarm 3x tiap hitung ulang
constexpr uint32_t BAUD_SERIAL      = 115200;

// ------------------------- Objek global ------------------------------------
MAX30105 sensorDetak;
LiquidCrystal_I2C lcd(ALAMAT_LCD_I2C, 16, 2);

uint32_t bufferIR[JUMLAH_SAMPEL];    // data inframerah (untuk hitung BPM + SpO2)
uint32_t bufferMerah[JUMLAH_SAMPEL]; // data merah (untuk hitung SpO2)
int32_t  detakJantung = 0;           // BPM hasil hitung
int32_t  kadarOksigen = 0;           // SpO2 (%) hasil hitung
int8_t   detakValid   = 0;           // 1 = BPM hasil hitung bisa dipercaya
int8_t   oksigenValid = 0;           // 1 = SpO2 hasil hitung bisa dipercaya
bool     jariMenempel = false;       // true kalau sinyal IR cukup kuat

// ------------------------- Fungsi bantu ------------------------------------
// Bunyikan buzzer beberapa kali dengan pola yang sama.
void bunyiBuzzer(uint8_t jumlahBeep, uint32_t lamaBeepMs, uint32_t jedaMs) {
  for (uint8_t i = 0; i < jumlahBeep; i++) {
    digitalWrite(PIN_BUZZER, HIGH);
    delay(lamaBeepMs);
    digitalWrite(PIN_BUZZER, LOW);
    if (i + 1 < jumlahBeep) {
      delay(jedaMs);
    }
  }
}

// Ambil satu sampel baru dari FIFO sensor (menunggu sampai ada data).
void bacaSatuSampel(uint32_t &nilaiIR, uint32_t &nilaiMerah) {
  while (sensorDetak.available() == false) {
    sensorDetak.check();
  }
  nilaiIR = sensorDetak.getIR();
  nilaiMerah = sensorDetak.getRed();
  sensorDetak.nextSample();
}

// Isi kedua buffer dengan 100 sampel pertama (4 detik pengukuran awal).
void isiBufferAwal() {
  for (uint8_t i = 0; i < JUMLAH_SAMPEL; i++) {
    bacaSatuSampel(bufferIR[i], bufferMerah[i]);
  }
}

// Geser buffer dan isi 25 sampel terbaru, lalu hitung ulang BPM + SpO2.
void hitungUlang() {
  for (uint8_t i = SAMPEL_BARU; i < JUMLAH_SAMPEL; i++) {
    bufferIR[i - SAMPEL_BARU] = bufferIR[i];
    bufferMerah[i - SAMPEL_BARU] = bufferMerah[i];
  }
  for (uint8_t i = JUMLAH_SAMPEL - SAMPEL_BARU; i < JUMLAH_SAMPEL; i++) {
    bacaSatuSampel(bufferIR[i], bufferMerah[i]);
  }
  maxim_heart_rate_and_oxygen_saturation(bufferIR, JUMLAH_SAMPEL, bufferMerah,
                                         &kadarOksigen, &oksigenValid,
                                         &detakJantung, &detakValid);
}

// Cek apakah kondisi di luar normal: detak di luar 60-100 BPM atau
// SpO2 di bawah 95% (hanya bila hasil hitungnya valid).
bool kondisiTidakNormal() {
  bool detakAbnormal =
      (detakValid == 1) && (detakJantung < BPM_MIN_NORMAL || detakJantung > BPM_MAKS_NORMAL);
  bool oksigenRendah =
      (oksigenValid == 1) && (kadarOksigen < SPO2_MIN_NORMAL);
  return detakAbnormal || oksigenRendah;
}

// Tampilkan hasil di LCD 16x2: baris 1 detak, baris 2 SpO2 + status.
void tampilkanLCD() {
  lcd.clear();
  if (!jariMenempel) {
    lcd.setCursor(0, 0);
    lcd.print("Tempelkan jari");
    lcd.setCursor(0, 1);
    lcd.print("pada sensor...");
    return;
  }
  lcd.setCursor(0, 0);
  lcd.print("Detak: ");
  if (detakValid == 1) {
    lcd.print(detakJantung);
    lcd.print(" BPM");
  } else {
    lcd.print("-- BPM");
  }
  lcd.setCursor(0, 1);
  lcd.print("SpO2: ");
  if (oksigenValid == 1) {
    lcd.print(kadarOksigen);
    lcd.print("% ");
  } else {
    lcd.print("--% ");
  }
  lcd.print(kondisiTidakNormal() ? "ALARM!" : "Normal");
}

// Atur LED dan buzzer sesuai kondisi. Alarm: LED merah + buzzer 3x.
// Normal: LED hijau saja. Jari tidak menempel: semua mati.
void aturAlarm() {
  if (!jariMenempel) {
    digitalWrite(PIN_LED_HIJAU, LOW);
    digitalWrite(PIN_LED_MERAH, LOW);
    digitalWrite(PIN_BUZZER, LOW);
    return;
  }
  if (kondisiTidakNormal()) {
    digitalWrite(PIN_LED_HIJAU, LOW);
    digitalWrite(PIN_LED_MERAH, HIGH);
    bunyiBuzzer(JUMLAH_BEEP_ALARM, BEEP_ALARM_MS, JEDA_ALARM_MS);
  } else {
    digitalWrite(PIN_LED_HIJAU, HIGH);
    digitalWrite(PIN_LED_MERAH, LOW);
    digitalWrite(PIN_BUZZER, LOW);
  }
}

// Tulis hasil ke Serial Monitor untuk dipantau dari komputer.
void cetakSerial() {
  Serial.print("IR=");
  Serial.print(bufferIR[JUMLAH_SAMPEL - 1]);
  Serial.print("  BPM=");
  Serial.print(detakValid == 1 ? detakJantung : -1);
  Serial.print("  SpO2=");
  Serial.print(oksigenValid == 1 ? kadarOksigen : -1);
  Serial.print("  ");
  Serial.println(kondisiTidakNormal() ? "ALARM" : "Normal");
}

// ------------------------- setup / loop ------------------------------------
void setup() {
  Serial.begin(BAUD_SERIAL);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_HIJAU, OUTPUT);
  pinMode(PIN_LED_MERAH, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED_HIJAU, LOW);
  digitalWrite(PIN_LED_MERAH, LOW);

  Wire.begin(PIN_SDA, PIN_SCL);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Pengukur Detak");
  lcd.setCursor(0, 1);
  lcd.print("Jantung MAX30102");

  if (!sensorDetak.begin(Wire, I2C_SPEED_FAST)) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Sensor tidak");
    lcd.setCursor(0, 1);
    lcd.print("ditemukan!");
    Serial.println("MAX30102 tidak ditemukan. Periksa wiring/power.");
    while (true) {
      delay(1000);
    }
  }

  sensorDetak.setup(KECERAHAN_LED, RATA_RATA_SAMPEL, MODE_LED,
                    LAJU_SAMPEL, LEBAR_PULSA, RENTANG_ADC);
  sensorDetak.setPulseAmplitudeRed(0x0A);  // LED merah redup (hemat daya)
  sensorDetak.setPulseAmplitudeGreen(0);   // LED hijau tidak dipakai

  Serial.println("Tempelkan jari pada sensor MAX30102.");
  isiBufferAwal();
}

void loop() {
  // Jari dianggap menempel bila sinyal inframerah cukup kuat.
  jariMenempel = bufferIR[JUMLAH_SAMPEL - 1] > BATAS_JARI_IR;

  hitungUlang();
  tampilkanLCD();
  aturAlarm();
  cetakSerial();
}
