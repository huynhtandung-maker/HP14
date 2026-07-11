# Changelog

Tất cả thay đổi đáng chú ý của HP14 được ghi tại đây.

## [1.1.0] — 2026-07-11

### Added

- WiFi scan cache trước khi mở setup AP.
- Pending WiFi credentials.
- Commit WiFi mới chỉ sau khi kết nối thành công.
- Log chẩn đoán portal theo từng bước.
- Tương thích khóa Preferences từ các phiên bản WiFi trước.

### Fixed

- Portal bị khóa do quét WiFi trong HTTP request.
- Form lưu WiFi bị văng hoặc không tới `/save`.
- WiFi mới ghi đè cấu hình cũ trước khi được xác minh.
- Vòng lặp portal khi candidate WiFi thất bại.

### Kept stable

- GPIO pin map.
- TFT Hardware SPI.
- PAGE/FOCUS UX.
- ThingsBoard telemetry.
- MQ-135 relative trend model.

## [1.0.0]

- Port lại luồng save-and-restart từ HP13.
- Portal mở bằng giữ hai nút 5 giây.

## [0.8.0]

- Modern TFT UI.
- PAGE + FOCUS interaction.
- WiFi setup portal.

## [0.7.0]

- TFT chuyển sang Hardware SPI.
- Nút phản hồi theo lần nhấn.

## [0.1.0–0.6.0]

- Các phiên bản thử nghiệm ban đầu cho cảm biến, TFT, OLED, LED, buzzer, nút và MQTT.
