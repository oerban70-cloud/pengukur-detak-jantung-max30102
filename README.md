# Pengukur Detak Jantung MAX30102 — ESP32 + Sensor MAX30102 + LCD I2C

Detak jantungmu abnormal, tapi kamu tidak pernah tahu? Satu sensor kecil di
ujung jari membacanya dan memberi tahu kamu sebelum terlambat. Tempelkan
ujung jari pada sensor MAX30102, detak jantung (BPM) dan kadar oksigen darah
(SpO2) langsung tampil di LCD — dan begitu detak keluar dari rentang normal,
buzzer berbunyi dan LED merah menyala.

Video Short (60 detik): link menyusul setelah upload

## Cara kerja

1. Jari ditempel pada sensor MAX30102. Sensor menembakkan cahaya merah dan
   inframerah ke ujung jari dan mengukur pantulannya, yang berubah mengikuti
   aliran darah di setiap detak jantung.
2. ESP32 mengumpulkan 100 sampel (4 detik pada 25 sampel per detik), lalu
   menghitung detak jantung (BPM) dan SpO2 dengan algoritma Maxim
   (`maxim_heart_rate_and_oxygen_saturation` dari library SparkFun MAX3010x).
   Setelah itu hasil dihitung ulang tiap 1 detik dari 25 sampel terbaru.
3. Hasil tampil di LCD 16x2 I2C: baris pertama detak dalam BPM, baris kedua
   SpO2 dalam persen plus status Normal atau ALARM!.
4. Kalau detak keluar dari 60–100 BPM (detak istirahat normal) atau SpO2 di
   bawah 95% (SpO2 normal 95–100%), LED merah menyala dan buzzer berbunyi
   3 kali. Kalau normal, LED hijau menyala dan buzzer diam.
5. Kalau jari belum menempel (sinyal inframerah di bawah 50000), LCD meminta
   "Tempelkan jari pada sensor..." dan semua indikator mati.

PERHATIAN: alat ini hanya untuk edukasi dan pemantauan santai, BUKAN alat
medis dan BUKAN untuk diagnosis. Kalau merasa tidak enak badan, periksa ke
tenaga medis.

## Wiring

| Komponen | Pin modul | ESP32 |
| --- | --- | --- |
| Sensor MAX30102 VIN | VIN | 3V3 |
| Sensor MAX30102 GND | GND | GND |
| Sensor MAX30102 SDA | SDA | GPIO21 |
| Sensor MAX30102 SCL | SCL | GPIO22 |
| LCD 16x2 I2C VCC | VCC | 5V (VIN) |
| LCD 16x2 I2C GND | GND | GND |
| LCD 16x2 I2C SDA | SDA | GPIO21 |
| LCD 16x2 I2C SCL | SCL | GPIO22 |
| Buzzer aktif HIGH (+) | + | GPIO25 |
| Buzzer (-) | - | GND |
| LED hijau (seri 220 Ω) | Anoda | GPIO32 |
| LED hijau (seri 220 Ω) | Katoda | GND |
| LED merah (seri 220 Ω) | Anoda | GPIO33 |
| LED merah (seri 220 Ω) | Katoda | GND |
| ESP32 | USB | Adaptor USB 5V |

Sensor dan LCD berbagi bus I2C yang sama (SDA GPIO21, SCL GPIO22); alamat
LCD 0x27, alamat sensor 0x57, jadi tidak bentrok. Skema lengkap (gaya
Fritzing): lihat `schematic.png`.

## Konfigurasi

Di `PengukurDetakJantungMAX30102/PengukurDetakJantungMAX30102.ino`:

- `BPM_MIN_NORMAL` (60) / `BPM_MAKS_NORMAL` (100) — rentang detak istirahat
  normal; di luar ini alarm bunyi.
- `SPO2_MIN_NORMAL` (95) — SpO2 di bawah ini alarm bunyi.
- `JUMLAH_SAMPEL` (100) / `SAMPEL_BARU` (25) — 100 sampel awal (4 detik),
  lalu hitung ulang tiap 25 sampel baru (1 detik).
- `BATAS_JARI_IR` (50000) — sinyal IR di bawah ini dianggap jari tidak
  menempel.
- `KECERAHAN_LED` (60) / `RATA_RATA_SAMPEL` (4) / `MODE_LED` (2) /
  `LAJU_SAMPEL` (100) / `LEBAR_PULSA` (411) / `RENTANG_ADC` (4096) —
  pengaturan sensor MAX30102 (lihat datasheet Maxim MAX30102).
- `JUMLAH_BEEP_ALARM` (3) / `BEEP_ALARM_MS` (200) / `JEDA_ALARM_MS` (200) —
  pola bunyi buzzer saat alarm.

## Komponen

- ESP32 DevKit
- Sensor MAX30102 (modul detak jantung + SpO2, I2C, 3V3)
- LCD 16x2 + backpack I2C (alamat 0x27)
- Buzzer aktif HIGH (5V)
- LED hijau + LED merah, masing-masing seri resistor 220 Ω
- Adaptor USB 5V untuk ESP32

## Compile (terverifikasi)

Terverifikasi 23 Nov 2026 dengan `arduino-cli` 1.6.0, ESP32 core 4.0.0:

```sh
arduino-cli compile --fqbn esp32:esp32:esp32 PengukurDetakJantungMAX30102
```

- Sketch memakai 320.385 byte (24%) dari program storage (maks. 1.310.720 byte).
- Variabel global memakai 26.288 byte (8%) dari dynamic memory (maks. 327.680 byte).

## Library

Pasang lewat Arduino Library Manager (Sketch > Include Library >
Manage Libraries), atau lewat arduino-cli:

```
arduino-cli lib install "SparkFun MAX3010x Pulse and Proximity Sensor Library"
arduino-cli lib install "LiquidCrystal I2C"
```

- SparkFun MAX3010x Pulse and Proximity Sensor Library 1.1.2
- LiquidCrystal_I2C 1.1.2

File ZIP kedua library juga ada di folder `libraries/`.

## Lisensi

MIT — lihat `LICENSE`.
