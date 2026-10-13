# Timbangan Truk (RFID + Jembatan Timbang) — Depok

Firmware ESP32-S3 untuk jembatan timbang truk: driver menempelkan kartu RFID di
reader masuk/keluar, berat dari indikator timbangan langsung dikirim ke server
`monitoring.xampah.com`. Status alat, live log, dan update firmware OTA bisa
dipantau dari dashboard **Depok → Fleet (OTA) → Timbangan truk**.

```
Indikator timbangan --RS232 RX18 (inverted, 9600)--> ┌──────────┐ -- LCD ST7920 (7/6/5)
Reader RFID (RS485) --RX44, DE/RE 35 (9600)--------> │ ESP32-S3 │ -- RTC DS3231 (I2C 8/9)
  slave 1 = masuk, slave 2 = keluar                  │          │ -- Tombol Masuk 2 / Keluar 4 / Reset 42
                                                     └────┬─────┘
                                       Wi-Fi ─────────────┘
              HTTPS: POST /api/weightDataIn, /api/weightDataOut
              MQTT (TLS 8883): depok_truk/<id>/status | log | cmd
```

Semua pin sama persis dengan firmware sebelumnya.

## Alur operator

| Layar | Kejadian |
|---|---|
| **Home** (jam) | Tempel kartu → berat saat itu langsung dikirim (masuk/keluar sesuai reader). Tahan **Reset** 3 dtk untuk set jam (Masuk = +1, tahan Reset = field berikutnya, simpan setelah detik). |
| **Mengirim...** | Upload berjalan di task terpisah; layar tetap responsif. |
| **Hasil** (3 dtk) | `Data Terkirim!`, `Sudah Tercatat` (409, data sama < 1 menit), atau `Gagal!` + alasan + kode. |
| **Display Timbangan** | Driver, no. polisi, customer, berat live. **Masuk**/**Keluar** = kirim ulang dengan berat sekarang, **Reset** = ke Home. Kembali ke Home otomatis setelah 2 menit. Kartu baru bisa langsung ditempel. |
| **Error** (5 dtk) | Format kartu salah, timbangan offline, berat ≤ 0,1 kg, jam belum valid. |

Frame kartu dari reader: `STX slave_id,id_driver,nama_driver,id_customer,no_polisi,nama_customer ETX`.

## Konfigurasi (`secrets.ini`)

Kredensial tidak di-commit. Di VPS jalankan:

```bash
bash scripts/provision-device.sh depok-truk-01 --site depok_truk
```

Salin `~/rmc-api-secrets/devices/depok-truk-01.secrets.ini` ke repo ini sebagai
`secrets.ini` (sudah di-gitignore). Format lengkap: `secrets.ini.example`.
Wi-Fi bisa di `secrets.ini` (`DP_WIFI_SSID`/`DP_WIFI_PASS`) atau default di
`include/config.h`.

## Build & flash

```bash
pio run -t upload            # build + flash lewat USB (env default)
pio run -e release           # image OTA -> .pio/build/release/firmware.bin
pio test -e native           # unit test lib/WeighCore (butuh gcc/clang)
pio device monitor           # log serial (USB native)
```

**Flash pertama dari firmware lama wajib lewat USB**: tabel partisi (2 slot app
untuk OTA) dan setelan memori ikut berubah, dan itu tidak bisa lewat OTA.

## Update OTA

1. Naikkan `FIRMWARE_VERSION` di `include/config.h`.
2. `pio run -e release` (menolak build tanpa kredensial MQTT).
3. Dashboard → Depok → Fleet (OTA) → **Timbangan truk** → *Firmware*: upload
   `firmware.bin` dengan versi yang sama.
4. *Rollout* → deploy ke `depok-truk-01`.

Selama download timbangan tetap dipakai normal. Reboot (beberapa detik) menunggu
sampai tidak ada truk yang sedang ditimbang (maks. 3 menit). Image dicek
SHA-256 sebelum dipakai, perintah OTA untuk site lain ditolak, dan image baru
yang gagal online 3× boot otomatis kembali ke versi lama.

Perintah remote lain (`depok_truk/<id>/cmd`): `PING`, `STATUS`, `RESTART`
(juga menunggu timbangan idle, maks. 1 menit).

## Status di dashboard

Tiap 10 detik (retained, LWT `{"online":false}`): status timbangan (LIVE/STALE/
OFFLINE + berat), truk yang sedang di layar, sumber jam (RTC/NTP) dan drift,
hasil upload terakhir, heap, RSSI, alasan reboot. Log live termasuk 24 baris
terakhir sebelum crash/watchdog/brownout (`[PREV] ...`).

## Catatan teknis

- **PSRAM dimatikan** (`board_build.arduino.memory_type = qio_qspi`). GPIO35
  (DE/RE RS485) adalah jalur data PSRAM octal di ESP32-S3R8; firmware lama
  menyalakan PSRAM *dan* men-drive GPIO35, yang merusak memori → crash acak.
  Firmware ini berjalan di SRAM internal (cukup) dan menolak men-drive GPIO35
  bila PSRAM ternyata aktif. Jangan kembalikan ke `qio_opi`.
- **Jam**: RTC DS3231 dikoreksi otomatis dari NTP (saat online pertama dan tiap
  6 jam). Tanpa jam valid (RTC mati dan tanpa NTP) data **tidak** dikirim —
  firmware lama mengarang tanggal `2024-01-..`.
- **Berat**: tanpa pembacaan baru dari indikator selama 5 dtk, berat dianggap
  tidak tersedia (`N/A`) dan tidak dikirim, supaya angka lama tidak tercatat.
- **Tanpa blocking**: HTTP di task `api`, MQTT/OTA di task `mqtt` (core 0);
  loop utama (core 1) hanya membaca serial, tombol, dan menggambar LCD. Watchdog
  30 dtk; supervisor me-reboot (saat idle) bila MQTT putus ≥ 15 menit (backoff
  sampai 2 jam).
- **Upload**: retry otomatis hanya bila request pasti belum sampai ke server
  (gagal connect/kirim), supaya tidak ada data dobel. Header
  `Accept: application/json` wajib (tanpanya error validasi Laravel jadi 302).
