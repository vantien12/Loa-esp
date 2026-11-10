# Loa ESP: ESP32-S3 N16R8 + MAX98357 cho Home Assistant

Loa thông báo và nghe nhạc (YouTube qua TriTue/Music Assistant, Zing, radio, TTS) chạy ESPHome.
- Không cần cài gì trên máy tính: GitHub tự build, nạp bằng trình duyệt.
- Wi-Fi cấu hình bằng web, mất Wi-Fi thì loa tự phát Wi-Fi để cấu hình lại.
- Chọn chân GPIO trên web GitHub trước khi build.

## Cài một lần (khoảng 5 phút)
1. Tạo repo mới trên GitHub, ví dụ `loa-esp`, để chế độ **Public**. GitHub Pages miễn phí cần repo public.
2. Tải toàn bộ các file trong thư mục này lên repo bằng nút **Add file → Upload files**. Giữ nguyên cấu trúc thư mục `.github/workflows` và `pages`.
3. Vào **Settings → Actions → General → Workflow permissions**, chọn **Read and write permissions**, rồi Save.
4. Tab **Actions** sẽ tự build bản chân mặc định (LRC 16, BCLK 15, DIN 7). Chờ khoảng 5–10 phút cho có dấu ✓ xanh.
5. Vào **Settings → Pages**, mục Source chọn **Deploy from a branch**, Branch chọn **gh-pages / (root)**, rồi Save.
6. Trang nạp sẽ ở `https://<tên-github>.github.io/loa-esp/`.

## Chọn chân GPIO khác
Vào **Actions → Build firmware loa ESP → Run workflow**, chọn LRC/BCLK/DIN rồi bấm **Run workflow**. Build xong, bộ chân mới hiện trong danh sách trên trang nạp, các bản cũ vẫn giữ.

Chỉ cho chọn những chân an toàn với N16R8. Đã bỏ các chân sau:
- chân khởi động: 0, 3, 45, 46
- chân USB: 19, 20
- chân flash/PSRAM: 26–37

## Nạp và cấu hình Wi-Fi
1. Mở trang nạp bằng Chrome hoặc Edge trên máy tính, chọn bộ chân, bấm **Nạp firmware**.
2. Cắm vào cổng **USB** của ESP32-S3 (không phải cổng COM) thì nạp xong cấu hình Wi-Fi được ngay.
3. Không vào được Wi-Fi nhà hơn 1 phút, loa tự phát Wi-Fi `loa-esp-xxxxxx`, mật khẩu `12345678`. Kết nối vào đó, trang chọn Wi-Fi tự mở, hoặc vào `192.168.4.1`.
4. Home Assistant tự phát hiện loa: **Settings → Devices & services → ESPHome**.

## Cập nhật về sau
- Từ ESPHome Builder trong HA: bấm **Adopt** loa, rồi cập nhật qua Wi-Fi (OTA).
- Hoặc build lại bằng GitHub rồi nạp lại qua trang web. Wi-Fi đã lưu sẽ bị xoá khi nạp qua USB.

## Đổi mật khẩu Wi-Fi phát ra / tên loa
Sửa mục `substitutions` ở đầu file `loa-esp-s3.yaml` (`ap_password`, `friendly_name`) rồi build lại.
