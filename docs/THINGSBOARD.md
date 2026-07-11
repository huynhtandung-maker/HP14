# ThingsBoard Integration

## 1. Tạo device

1. Trong ThingsBoard, tạo device tên `HP14`.
2. Mở credentials của device.
3. Sao chép access token vào `HP14/secrets.h`.

```cpp
#define TB_HOST  "mqtt.thingsboard.cloud"
#define TB_PORT  1883
#define TB_TOKEN "YOUR_DEVICE_ACCESS_TOKEN"
```

HP14 dùng MQTT access token làm username và publish vào:

```text
v1/devices/me/telemetry
```

## 2. Telemetry keys

| Key | Kiểu | Mô tả |
|---|---|---|
| `temperature` | number/null | Nhiệt độ °C |
| `humidity` | number/null | Độ ẩm %RH |
| `heatIndex` | number/null | Nhiệt độ cảm nhận °C |
| `dhtOk` | boolean | DHT có dữ liệu hợp lệ |
| `dhtFresh` | boolean | Dữ liệu DHT còn mới |
| `dhtFailureCount` | number | Số lần đọc lỗi liên tiếp |
| `mq135Raw` | number | ADC raw |
| `mq135AdcMv` | number | Điện áp tại ADC |
| `mq135SensorMv` | number | Điện áp tái dựng trước cầu chia |
| `mq135FilteredMv` | number | Điện áp sau bộ lọc |
| `mq135BaselineMv` | number | Baseline tương đối |
| `mq135Ready` | boolean | MQ đã sẵn sàng |
| `mq135Warmup` | boolean | Đang warm-up/hiệu chuẩn |
| `gasRatioRelative` | number/null | Tỷ lệ so với baseline |
| `gasChangePctRelative` | number/null | % thay đổi tương đối |
| `thermalScoreExperimental` | number | Điểm nhiệt thử nghiệm |
| `dwScoreExperimental` | number | Điểm Deep Work thử nghiệm |
| `dwScoreValid` | boolean | Điểm có đủ dữ liệu hay chưa |
| `environmentState` | string | BOOT/WARMUP/GOOD/CAUTION/POOR/... |
| `currentPage` | number | Trang UI hiện tại 1–4 |
| `focusSessionActive` | boolean | Phiên Focus đang chạy |
| `focusSessionElapsedSec` | number | Thời lượng phiên hiện tại |
| `focusSessionCount` | number | Số phiên đã bắt đầu |
| `wifiPortalActive` | boolean | Portal đang mở |
| `wifiProvisionState` | string | Trạng thái provisioning |
| `wifiRssi` | number | RSSI dBm |
| `uptimeSec` | number | Thời gian chạy |
| `firmwareVersion` | string | Phiên bản firmware |

## 3. Dashboard đề xuất

- Gauge: `dwScoreExperimental`.
- State card: `environmentState`.
- Time series: `temperature`, `humidity`, `heatIndex`.
- Time series: `gasRatioRelative`, `gasChangePctRelative`.
- Status cards: `dhtOk`, `mq135Ready`, `wifiPortalActive`.
- Session card: `focusSessionActive`, `focusSessionElapsedSec`.

## 4. Tần suất gửi

Mặc định telemetry mỗi 30 giây:

```cpp
static constexpr uint32_t TELEMETRY_INTERVAL_MS = 30000UL;
```

Không giảm quá thấp trên gói ThingsBoard giới hạn để tránh gửi dồn dập.
