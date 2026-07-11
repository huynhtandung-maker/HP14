# Hướng dẫn phát hành HP14 lên GitHub

## 1. Kiểm tra trước khi upload

Không được có:

- `HP14/secrets.h`.
- WiFi password.
- ThingsBoard token.
- Ảnh log chứa token hoặc thông tin nhạy cảm.
- Thư mục build tạm.

Chạy tìm kiếm toàn repository với các từ:

```text
TB_TOKEN
WIFI_PASSWORD
mqtt.thingsboard.cloud
```

`mqtt.thingsboard.cloud` là host công khai và có thể giữ; giá trị token thật phải bị loại bỏ.

## 2. Tạo repository bằng giao diện GitHub

1. GitHub → New repository.
2. Repository name: `HP14-Deep-Work-Environment-Monitor`.
3. Description: dùng nội dung trong `GITHUB_METADATA.md`.
4. Chọn Public hoặc Private.
5. Không tạo thêm README vì gói đã có README.
6. Create repository.
7. Upload toàn bộ nội dung thư mục này.
8. Commit message:

```text
feat: release HP14 v1.1.0 WiFi Portal Stable
```

## 3. Tạo release

1. Mở `Releases` → `Draft a new release`.
2. Tag: `v1.1.0`.
3. Target: `main`.
4. Title: `HP14 v1.1.0 — WiFi Portal Stable`.
5. Dán nội dung từ `RELEASE_NOTES_v1.1.0.md`.
6. Đính kèm ZIP firmware nếu cần.
7. Publish release.

## 4. Dùng Git command line

```bash
git init
git branch -M main
git add .
git commit -m "feat: release HP14 v1.1.0 WiFi Portal Stable"
git remote add origin https://github.com/YOUR_USERNAME/HP14-Deep-Work-Environment-Monitor.git
git push -u origin main

git tag -a v1.1.0 -m "HP14 v1.1.0 WiFi Portal Stable"
git push origin v1.1.0
```

## 5. Sau khi public

- Kiểm tra file `secrets.h` không xuất hiện trong lịch sử Git.
- Mở README trên web để kiểm tra Mermaid và bảng.
- Thêm Topics.
- Chọn license.
- Bật Issues nếu muốn nhận phản hồi.
