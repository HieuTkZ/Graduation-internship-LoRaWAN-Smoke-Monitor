# ESP32 Smoke Monitor

Mô hình phát hiện và theo dõi **mức khói tương đối** dựa trên **tán xạ hồng ngoại**, sử dụng LED IR333-A, photodiode BPV10NF, vi điều khiển ESP32 và cảm biến khí MP-2 làm kênh đo bổ trợ.

> **Lưu ý quan trọng:** dữ liệu hiện tại chưa được hiệu chuẩn bằng thiết bị chuẩn đo độ che khuất, mật độ quang hoặc nồng độ aerosol. Vì vậy, các giá trị phần trăm nếu sử dụng chỉ được hiểu là **chỉ số khói tương đối**, không phải nồng độ khói tuyệt đối theo `%/m`, `mg/m³` hay một chuẩn công nghiệp tương đương.

---

## 1. Giới thiệu

Đề tài xuất phát từ quá trình khảo sát một thiết bị báo khói không dây LoRaWAN sử dụng nguyên lý quang điện. Sau giai đoạn phân tích thiết bị, nhóm xây dựng một mô hình thực nghiệm riêng dùng ESP32 để:

- thu tín hiệu tán xạ hồng ngoại từ buồng khói;
- đọc tín hiệu khí từ cảm biến MP-2;
- hiển thị dữ liệu theo thời gian thực trên giao diện web;
- tự động ghi nhận từng phiên đo;
- lưu các thông số như IR max, IR trung bình, MP-2 max, MP-2 trung bình và số mẫu;
- phục vụ thử nghiệm, so sánh và đánh giá đáp ứng của hai kênh cảm biến.

Ở phiên bản hiện tại, **Wi-Fi + Web Server trên ESP32** được sử dụng cho việc quan sát và ghi dữ liệu. **LoRaWAN chưa phải là phần truyền thông được kiểm chứng trong mô hình thực nghiệm này** và được xem là hướng tích hợp tiếp theo.

---

## 2. Mục tiêu dự án

1. Nghiên cứu nguyên lý phát hiện khói bằng tán xạ ánh sáng hồng ngoại.
2. Thiết kế mạch phát – thu sử dụng IR333-A và BPV10NF.
3. Đọc tín hiệu quang học bằng ADC 12-bit của ESP32.
4. Tích hợp cảm biến khí MP-2 để bổ sung thông tin về sản phẩm khí của nguồn cháy.
5. Xây dựng giao diện web hiển thị dữ liệu thời gian thực.
6. Tự động nhận biết và lưu từng phiên có khói dựa trên ngưỡng ADC.
7. Thử nghiệm với nhiều nguồn khói và đánh giá mối quan hệ giữa kênh quang học và kênh khí.
8. Làm nền tảng cho các bước tiếp theo như chuẩn hóa buồng đo, hiệu chuẩn cảm biến, chống báo giả và truyền cảnh báo qua LoRaWAN.

---

## 3. Nguyên lý hoạt động

### 3.1. Kênh quang học

Mô hình sử dụng một buồng khói dạng labyrinth để hạn chế ánh sáng môi trường đi trực tiếp tới photodiode.

```text
IR LED 940 nm
     │
     ▼
+------------------+
|   Smoke Chamber  |
|    / Labyrinth   |
+------------------+
     │  ánh sáng tán xạ bởi hạt khói
     ▼
Photodiode BPV10NF
     │
     ▼
Mạch R-C chuyển dòng quang -> điện áp
     │
     ▼
ESP32 ADC
```

Khi không có khói, photodiode nhận rất ít ánh sáng trực tiếp. Khi các hạt aerosol đi vào vùng quan sát, ánh sáng hồng ngoại bị tán xạ và một phần truyền tới photodiode, làm tăng tín hiệu điện ở đầu ra.

### 3.2. Kênh khí MP-2

MP-2 được sử dụng như một **kênh bổ trợ**, phản ánh thành phần khí/sản phẩm cháy. Giá trị MP-2 trong firmware hiện được giữ ở dạng **ADC raw**; không xem đây là phép đo trực tiếp nồng độ khói.

Hai kênh cảm biến phản ánh hai đặc tính khác nhau:

- **IR + photodiode:** chủ yếu phản ánh tán xạ quang của aerosol/hạt khói.
- **MP-2:** nhạy với thành phần khí và điều kiện cháy.

Do đó MP-2 không được dùng như một “chuẩn nồng độ khói” để hiệu chuẩn trực tiếp kênh quang học.

---

## 4. Kiến trúc hệ thống

```text
                 +----------------------+
                 |     Smoke Chamber    |
                 | IR333-A + BPV10NF    |
                 +----------+-----------+
                            |
                            | Analog IR
                            v
+--------------+      +-----+-------------------+
|   MP-2 AO    |----->|                         |
|   MP-2 DO    |----->|          ESP32          |
+--------------+      |                         |
                      | ADC + Signal Streaming  |
                      +-----------+-------------+
                                  |
                                  | Wi-Fi
                                  v
                      +-------------------------+
                      | ESPAsyncWebServer + SSE |
                      +------------+------------+
                                   |
                                   v
                      +-------------------------+
                      | Browser Dashboard       |
                      | Realtime + Measurement  |
                      | Logs + localStorage     |
                      +-------------------------+
```

---

## 5. Phần cứng

### 5.1. Linh kiện chính

| Thành phần | Chức năng |
|---|---|
| ESP32 | Đọc ADC, xử lý và chạy Web Server |
| IR333-A | LED phát hồng ngoại, bước sóng đỉnh khoảng 940 nm |
| BPV10NF | Photodiode PIN thu ánh sáng hồng ngoại |
| MP-2 | Cảm biến khí dùng làm kênh bổ trợ |
| R hạn dòng LED | Giới hạn dòng LED phát |
| R tải photodiode | Chuyển dòng quang thành điện áp |
| Tụ lọc | Giảm nhiễu tín hiệu photodiode |
| Cầu chia áp | Hạ mức tín hiệu từ module MP-2 trước khi đưa vào ESP32 |

### 5.2. Thông số mạch quang học

Thiết kế thực nghiệm sử dụng:

- nguồn LED hồng ngoại: **3.3 V**;
- điện trở hạn dòng LED: **100 Ω**;
- điện trở tải photodiode: **100 kΩ**;
- tụ lọc song song: **1 µF**;
- hằng số thời gian danh định của mạch R-C: khoảng **0.1 s**.

### 5.3. Kết nối ESP32

| Tín hiệu | GPIO ESP32 | Kiểu đọc |
|---|---:|---|
| Photodiode IR | GPIO34 | ADC |
| MP-2 AO | GPIO35 | ADC |
| MP-2 DO | GPIO32 | Digital |

> GPIO của ESP32 chỉ làm việc ở mức điện áp phù hợp với 3.3 V. Tín hiệu từ module MP-2 cần đi qua cầu chia áp trước khi đưa vào các chân ESP32 nếu module có thể xuất mức 5 V.

---

## 6. Firmware và giao diện web

Firmware chính: `codeEsp32_smoke.ino`

### 6.1. Thư viện sử dụng

```cpp
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
```

Cần cài các thư viện tương ứng cho ESP32 trước khi biên dịch.

### 6.2. Chức năng chính

Firmware hiện tại hỗ trợ:

- ESP32 kết nối Wi-Fi ở chế độ Station;
- đọc đồng thời:
  - IR ADC tại GPIO34;
  - MP-2 AO tại GPIO35;
  - MP-2 DO tại GPIO32;
- độ phân giải ADC: **12 bit**, dải raw `0–4095`;
- chu kỳ lấy mẫu: **50 ms**, tương đương khoảng **20 mẫu/s**;
- truyền dữ liệu realtime bằng **Server-Sent Events (SSE)** qua endpoint `/events`;
- Web Server chạy ở cổng `80`;
- hiển thị 3 đồ thị realtime:
  - Photodiode IR;
  - MP-2 Analog Output;
  - MP-2 Digital Output;
- tự động bắt đầu/kết thúc phiên đo;
- tính và lưu thống kê cho từng phiên;
- lưu tối đa **20 phiên** hoàn chỉnh trên trình duyệt;
- sử dụng `localStorage` để dữ liệu log vẫn còn sau khi refresh trang;
- tự động thử kết nối lại Wi-Fi khi mất kết nối.

---

## 7. Quy tắc tự động ghi một phiên đo

Phiên đo hiện được kích hoạt theo tín hiệu IR.

```text
IR >= 500 ADC
      |
      v
Bắt đầu phiên đo
      |
      v
Ghi đồng thời IR + MP-2 AO + MP-2 DO
      |
      v
IR <= 400 ADC ?
      |
     Có
      |
      v
Duy trì dưới ngưỡng 2 giây ?
      |
     Có
      |
      v
Kết thúc phiên đo và lưu log
```

Các tham số trong giao diện web:

```javascript
const START_THRESHOLD = 500;
const STOP_THRESHOLD  = 400;
const END_HOLD_TIME   = 2000; // ms
```

Mục đích của khoảng giữ 2 giây là tránh kết thúc phiên sai khi ADC dao động ngắn quanh ngưỡng.

### Thông tin lưu cho mỗi phiên

- thời gian bắt đầu;
- thời gian kết thúc;
- thời lượng;
- số mẫu;
- IR lớn nhất;
- IR trung bình;
- MP-2 AO lớn nhất;
- MP-2 AO trung bình;
- tỷ lệ trạng thái DO ở mức HIGH;
- dữ liệu đồ thị IR/AO/DO của phiên.

---

## 8. Chỉ số khói tương đối

Do chưa có thiết bị chuẩn để quy đổi trực tiếp sang mật độ khói tuyệt đối, có thể biểu diễn tín hiệu IR dưới dạng một chỉ số tương đối:

```text
S_IR = 100 × (IR - IR_clean) / (IR_ref - IR_clean)
```

Trong đó:

- `IR`: giá trị ADC đang đo;
- `IR_clean`: ngưỡng nền trong môi trường sạch;
- `IR_ref`: giá trị tham chiếu của một quy trình hiệu chuẩn xác định.

Trong các thử nghiệm hiện tại:

```text
IR_clean = 400 ADC
```

Nếu `IR_ref` chỉ được lấy từ cực đại quan sát trong một loại thử nghiệm, kết quả chỉ có ý nghĩa là **chỉ số tương đối nội bộ**, không được diễn giải thành nồng độ khói tuyệt đối.

---

## 9. Thực nghiệm

Mô hình đã được thử với hai nguồn khói:

- **khói nhựa thông:** 3 phiên đo;
- **khói đốt giấy:** 4 phiên đo.

Tổng cộng: **7 phiên đo**.

### 9.1. Khói nhựa thông

| Phiên | Số mẫu | IR max | IR avg | MP-2 max | MP-2 avg | Vout max |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 846 | 4077 | 1777.0 | 1437 | 872.0 | 2.89 V |
| 2 | 483 | 2775 | 726.5 | 1046 | 737.4 | 2.19 V |
| 3 | 284 | 1490 | 617.5 | 466 | 290.2 | 1.34 V |

Quan sát chính:

- `IRmax` tăng từ **1490 → 4077 ADC** khi số mẫu của phiên tăng;
- hồi quy `IRmax` theo số mẫu cho `R² ≈ 0.974`;
- `IRmax` và `MP-2max` đồng biến trong ba phép đo, `R² ≈ 0.987`;
- do chỉ có 3 phiên, các hệ số trên chỉ mang ý nghĩa mô tả xu hướng, chưa đủ để xây dựng mô hình hiệu chuẩn.

### 9.2. Khói đốt giấy

| Phiên | Số mẫu | IR max | IR avg | MP-2 max | MP-2 avg | Vout max |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 370 | 2666 | 1153.9 | 3184 | 2944.1 | 2.02 V |
| 2 | 1251 | 2112 | 801.4 | 3451 | 3088.9 | 1.51 V |
| 3 | 252 | 2938 | 953.8 | 3167 | 3061.5 | 2.12 V |
| 4 | 530 | 1894 | 1015.4 | 2917 | 2660.6 | 1.52 V |

Quan sát chính:

- `IRmax` nằm trong khoảng **1894–2938 ADC**;
- `MP-2max` nằm trong khoảng **2917–3451 ADC**;
- quan hệ `IRmax` – số mẫu yếu hơn nhóm nhựa thông, `R² ≈ 0.381`;
- quan hệ `IRmax` – `MP-2max` gần như không rõ ràng, `R² ≈ 0.024`.

Kết quả cho thấy kênh IR và MP-2 không phản ánh cùng một đại lượng. MP-2 đặc biệt phụ thuộc vào thành phần khí của nguồn cháy, trong khi photodiode phụ thuộc vào lượng và đặc tính aerosol trong vùng tán xạ.

---

## 10. Cách chạy

### Bước 1 — Chuẩn bị môi trường

- Arduino IDE hoặc PlatformIO;
- ESP32 board package;
- thư viện `AsyncTCP`;
- thư viện `ESPAsyncWebServer`.

### Bước 2 — Cấu hình Wi-Fi

Trong `codeEsp32_smoke.ino`, thay thông tin mạng bằng Wi-Fi của bạn:

```cpp
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
```

Không nên commit mật khẩu Wi-Fi thật lên repository công khai.

### Bước 3 — Nạp chương trình

1. Chọn đúng board ESP32.
2. Chọn đúng cổng COM/Serial.
3. Biên dịch và upload firmware.
4. Mở Serial Monitor ở `115200 baud`.
5. Sau khi ESP32 kết nối Wi-Fi, Serial Monitor sẽ hiển thị địa chỉ IP.

### Bước 4 — Mở giao diện web

Truy cập:

```text
http://<ESP32_IP>/
```

Ví dụ:

```text
http://192.168.1.100/
```

Trình duyệt sẽ tự kết nối tới endpoint SSE và bắt đầu hiển thị dữ liệu realtime.

---

## 11. Hạn chế hiện tại

Mô hình hiện tại vẫn là một hệ thử nghiệm nghiên cứu, chưa phải đầu báo cháy thương mại hoặc thiết bị đo khói chuẩn.

Các hạn chế chính:

- nguồn khói được tạo thủ công, chưa có máy tạo aerosol chuẩn;
- số lần đo còn ít;
- chưa có phép lặp ở cùng một mức khói chuẩn;
- chưa có thiết bị tham chiếu đo độ che khuất, mật độ quang hoặc nồng độ khối lượng;
- buồng labyrinth làm dòng khói đi vào không đồng đều;
- mạch R-C làm tăng độ ổn định nhưng cũng làm chậm đáp ứng;
- ADC ESP32 có phi tuyến và cần hiệu chuẩn nếu muốn đo điện áp chính xác;
- MP-2 cần thời gian gia nhiệt và không thể quy đổi ADC thành “ppm khói” một cách trực tiếp cho mọi loại nguồn cháy;
- mô hình hiện tại dùng Wi-Fi cho thử nghiệm, chưa tích hợp truyền LoRaWAN vào phiên bản đo thực nghiệm.

---

## 12. Hướng phát triển

Các hướng cải tiến tiếp theo:

1. Chuẩn hóa hình học LED – photodiode – buồng khói.
2. Kiểm soát đường vào aerosol để cải thiện độ lặp lại.
3. Kích LED theo xung và đo hai trạng thái LED bật/tắt để trừ nền ánh sáng môi trường.
4. Sử dụng mạch khuếch đại chuyển dòng thành áp (TIA) cho photodiode.
5. Hiệu chuẩn ADC và kênh quang học bằng thiết bị tham chiếu.
6. Thử nghiệm lặp lại với nhiều mức khói và nhiều nguồn cháy.
7. Bổ sung cảm biến nhiệt độ hoặc cảm biến CO/VOC để hỗ trợ phân loại tình huống.
8. Xây dựng thuật toán đa cảm biến để giảm báo giả do hơi nước hoặc khói nấu ăn.
9. Tối ưu tiêu thụ năng lượng bằng chế độ ngủ sâu và điều khiển LED theo chu kỳ.
10. Tích hợp LoRaWAN để truyền cảnh báo từ nút cảm biến tới gateway/server sau khi thuật toán phát hiện được xác nhận.

---

## 13. Tài liệu tham khảo chính

1. National Institute of Standards and Technology (NIST), *How Do Smoke Detectors Work?*, 2025.
2. National Bureau of Standards/NIST, *Fire Research and Safety – Photoelectric Detectors*, NBS Special Publication 540.
3. Everlight Electronics Co., Ltd., *IR333-A – 5.0 mm Infrared LED Datasheet*, 2016.
4. Vishay Semiconductors, *BPV10NF – Silicon PIN Photodiode Datasheet*, Rev. 2.3, 2025.
5. Espressif Systems, *ESP32 / ESP32-WROOM-32 Datasheet*, mục ADC.
6. Zhengzhou Winsen Electronics Technology Co., Ltd., *MP-2 Smoke Gas Sensor Manual*, Version 1.4, 2021.

---

## 14. Trạng thái dự án

**Prototype / Research stage**

Hệ thống đã hoàn thành các chức năng chính của mô hình thử nghiệm:

- [x] Khảo sát nguyên lý đầu báo khói quang điện
- [x] Thiết kế mạch IR333-A + BPV10NF
- [x] Thi công mạch thử nghiệm
- [x] Tích hợp ESP32
- [x] Đọc ADC photodiode
- [x] Tích hợp MP-2
- [x] Thiết kế cầu chia áp bảo vệ ESP32
- [x] Web dashboard realtime
- [x] Tự động ghi phiên đo
- [x] Lưu log trên trình duyệt
- [x] Thử nghiệm với khói nhựa thông
- [x] Thử nghiệm với khói đốt giấy
- [ ] Hiệu chuẩn bằng thiết bị tham chiếu
- [ ] Chuẩn hóa buồng và nguồn tạo khói
- [ ] Thu thập bộ dữ liệu lớn hơn
- [ ] Thuật toán phân loại đa cảm biến
- [ ] Tích hợp LoRaWAN vào prototype cuối

---

## 15. License

Dự án được xây dựng cho mục đích **nghiên cứu, học tập và thực tập tốt nghiệp**.
