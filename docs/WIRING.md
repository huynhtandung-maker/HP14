# HP14 — Bảng đấu nối chính thức

> GPIO đã khóa cho firmware v1.1.0. Không tự đổi dây nếu chưa đồng bộ lại `config.h`.

## 1. Bản đồ chân

| Linh kiện | Chân linh kiện | ESP32-C6 | Ghi chú |
|---|---|---:|---|
| TFT GC9A01 | RST/RES | GPIO0 | Reset màn hình |
| Nút PAGE | Signal | GPIO1 | Nút xuống GND |
| MQ-135 | AO | GPIO2 | Qua cầu chia 10kΩ/10kΩ |
| DHT11 | DATA | GPIO3 | VCC 3V3 |
| OLED | SDA | GPIO6 | I²C |
| OLED | SCL | GPIO7 | I²C |
| RGB onboard | Data | GPIO8 | Không nối ngoài |
| LED đỏ | Anode qua 220Ω | GPIO10 | Cathode xuống GND |
| Nút FOCUS | Signal | GPIO11 | Nút xuống GND |
| Buzzer | Driver input | GPIO15 | Qua transistor NPN |
| TFT | CLK/SCL | GPIO18 | SPI clock |
| TFT | MOSI/SDA/DIN | GPIO19 | SPI data |
| TFT | CS | GPIO20 | Chip select |
| TFT | DC | GPIO21 | Data/command |
| LED xanh | Anode qua 220Ω | GPIO22 | Cathode xuống GND |
| LED vàng | Anode qua 220Ω | GPIO23 | Cathode xuống GND |

## 2. Nguồn

```text
3V3 → OLED VCC
     → DHT11 VCC
     → TFT VCC
     → TFT BLK

5V  → MQ-135 VCC
     → Buzzer 5V

GND → GND chung toàn hệ thống
```

## 3. Cầu chia áp MQ-135

Không nối AO của MQ-135 trực tiếp vào ADC ESP32-C6.

```text
MQ-135 AO
    │
   10kΩ
    │
    ●──────── GPIO2
    │
   10kΩ
    │
   GND
```

Firmware nhân lại điện áp theo hệ số `2.0` để tái dựng gần đúng tín hiệu cảm biến.

## 4. Hai nút bấm

```text
3V3 ── 10kΩ ──┬── GPIO1  ── PAGE  ── GND
               │
               └── pull-up ngoài

3V3 ── 10kΩ ──┬── GPIO11 ── FOCUS ── GND
               │
               └── pull-up ngoài
```

Firmware vẫn bật `INPUT_PULLUP` làm dự phòng.

Với nút 4 chân, dùng một chân bên trái và một chân bên phải. Hai chân cùng một phía thường đã nối với nhau bên trong.

## 5. Buzzer qua transistor

```text
GPIO15 ── 1kΩ ── Base transistor NPN
Base   ── 10kΩ ── GND
Emitter          ── GND
Collector        ── cực âm buzzer
5V               ── cực dương buzzer
```

Cấu hình hiện tại dùng `BUZZER_ACTIVE_HIGH = false` theo phần cứng thực tế. Nếu mạch driver được thay đổi, chỉ chỉnh cực tính trong `config.h`.

## 6. Bố trí cảm biến

- Đặt DHT11 cách MQ-135 ít nhất 15–20 cm.
- Không đặt DHT11 ngay trên luồng khí nóng từ bộ nung MQ-135.
- Tạo khe thông gió cho MQ-135.
- Không bọc kín cảm biến trong hộp không có luồng khí.
