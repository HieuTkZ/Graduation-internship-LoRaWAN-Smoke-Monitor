# LoRaWAN Smoke Monitor

## Giới thiệu

**LoRaWAN Smoke Monitor** là dự án nghiên cứu, phân tích và nâng cấp thiết bị đầu báo khói sử dụng công nghệ **LoRaWAN**.

Mục tiêu của dự án là tìm hiểu nguyên lý hoạt động của thiết bị hiện có, phân tích phần cứng và phương thức truyền dữ liệu, sau đó nghiên cứu bổ sung chức năng **giám sát mức độ/nồng độ khói** thay vì chỉ phát hiện trạng thái có hoặc không có khói.

Dự án được thực hiện trong quá trình thực tập nghiên cứu.

---

## Mục tiêu dự án

- Khảo sát cấu tạo của thiết bị đầu báo khói LoRaWAN.
- Phân tích nguyên lý phát hiện khói.
- Xác định các thành phần chính trên PCB.
- Nghiên cứu module truyền thông LoRa/LoRaWAN.
- Phân tích quá trình xử lý và truyền dữ liệu cảnh báo.
- Nghiên cứu phương pháp đo mức độ hoặc nồng độ khói.
- Xây dựng thuật toán phân loại mức độ khói.
- Truyền dữ liệu mức khói thông qua mạng LoRaWAN.
- Thực nghiệm và đánh giá độ ổn định của hệ thống.

---

## Kiến trúc hệ thống

```text
        Smoke
          │
          ▼
+---------------------+
|   Smoke Sensor      |
| / Optical Chamber   |
+----------+----------+
           │
           ▼
+---------------------+
|      MCU / ADC      |
| Signal Processing   |
+----------+----------+
           │
     ┌─────┴─────┐
     │           │
     ▼           ▼
+---------+   +---------+
| Buzzer  |   | LoRaWAN |
| / Alarm |   | Module  |
+---------+   +----+----+
                  │
                  ▼
              Gateway
                  │
                  ▼
            LoRaWAN Server
                  │
                  ▼
          Dashboard / Database
```

---

## Thiết bị nghiên cứu

Thiết bị ban đầu là một đầu báo khói có khả năng truyền cảnh báo thông qua mạng **LoRaWAN**.

Một số thành phần quan sát được:

- Buồng cảm biến khói quang học.
- Vi điều khiển.
- Module LoRa/LoRaWAN.
- Antenna.
- Buzzer cảnh báo.
- LED trạng thái.
- Nút nhấn kiểm tra/reset.
- Nguồn pin.
- Mạch xử lý tín hiệu cảm biến.

> Thông tin chi tiết về từng IC và linh kiện sẽ được cập nhật trong quá trình phân tích phần cứng.

---

## Hướng nâng cấp

Thiết bị gốc chủ yếu có chức năng:

```text
Không có khói          -> Normal
Có khói vượt ngưỡng    -> Alarm
```

Dự án hướng đến việc mở rộng thành:

```text
Không có khói
      │
      ▼
Khói nhẹ
      │
      ▼
Khói trung bình
      │
      ▼
Khói cao
      │
      ▼
Cảnh báo cháy
```

Hệ thống sẽ thu thập giá trị cảm biến, xử lý và gửi mức độ khói về máy chủ thông qua LoRaWAN.

---

## Phân chia công việc

### Phần 1 - Hardware Analysis

Công việc:

- Phân tích cấu tạo thiết bị.
- Xác định linh kiện trên PCB.
- Nghiên cứu nguyên lý cảm biến khói quang học.
- Xây dựng sơ đồ khối phần cứng.
- Phân tích nguồn và các tín hiệu vào/ra.
- Reverse engineering các phần cần thiết.

### Phần 2 - Firmware & LoRaWAN

Công việc:

- Nghiên cứu giao thức LoRaWAN.
- Phân tích module LoRa trên thiết bị.
- Nghiên cứu quá trình Join Network.
- Phân tích uplink/downlink.
- Thiết kế payload truyền dữ liệu.
- Xây dựng firmware xử lý và truyền dữ liệu.
- Kết nối Gateway và LoRaWAN Server.

### Phần 3 - Smoke Measurement & Testing

Công việc:

- Nghiên cứu phương pháp đo mức độ/nồng độ khói.
- Khảo sát tín hiệu từ cảm biến hiện tại.
- Nghiên cứu cảm biến bổ sung nếu cần.
- Xây dựng thuật toán xác định mức khói.
- Hiệu chuẩn cảm biến.
- Thiết kế các bài kiểm thử.
- Thu thập và phân tích dữ liệu thực nghiệm.

---

## Payload dự kiến

| Byte | Data | Description |
|------|------|-------------|
| 0 | Alarm Status | Trạng thái cảnh báo |
| 1-2 | Smoke Level | Giá trị mức khói |
| 3 | Battery | Phần trăm pin |
| 4 | Sensor Status | Trạng thái cảm biến |

Ví dụ:

```text
Alarm       : 0
Smoke Level : 135
Battery     : 82%
Sensor      : Normal
```

Payload thực tế sẽ được cập nhật sau khi hoàn thành quá trình phân tích firmware và giao thức của thiết bị.

---

## Thuật toán dự kiến

```text
START
  │
  ▼
Initialize System
  │
  ▼
Read Smoke Sensor
  │
  ▼
Signal Processing
  │
  ▼
Calculate Smoke Level
  │
  ▼
Compare Threshold
  │
  ├── Normal
  ├── Low
  ├── Medium
  └── High / Alarm
  │
  ▼
Update Local Alarm
  │
  ▼
Create LoRaWAN Payload
  │
  ▼
Send Uplink
  │
  ▼
Sleep / Wait
  │
  └──────────────► Repeat
```

---

## Cấu trúc Repository

```text
LoRaWAN-Smoke-Monitor/
│
├── README.md
│
├── docs/
│   ├── hardware/
│   ├── lorawan/
│   ├── sensor/
│   └── reports/
│
├── hardware/
│   ├── schematic/
│   ├── pcb-analysis/
│   └── datasheets/
│
├── firmware/
│   ├── src/
│   ├── include/
│   └── tests/
│
├── lorawan/
│   ├── payload/
│   ├── decoder/
│   └── configuration/
│
├── experiments/
│   ├── raw-data/
│   ├── processed-data/
│   └── results/
│
├── images/
│
└── references/
```

---

## Thực nghiệm

Các trường hợp dự kiến được kiểm tra:

1. Môi trường không có khói.
2. Mức khói thấp.
3. Mức khói trung bình.
4. Mức khói cao.
5. Kiểm tra thời gian phản hồi.
6. Kiểm tra khả năng gửi cảnh báo LoRaWAN.
7. Kiểm tra độ ổn định của cảm biến.
8. Kiểm tra mức tiêu thụ năng lượng.

Dữ liệu thô sẽ được lưu tại:

```text
experiments/raw-data/
```

Dữ liệu sau xử lý sẽ được lưu tại:

```text
experiments/processed-data/
```

---

## Công nghệ sử dụng

- LoRa
- LoRaWAN
- Embedded System
- Microcontroller
- Optical Smoke Detection
- ADC / Signal Processing
- IoT
- Sensor Calibration
- Data Analysis

Các công nghệ cụ thể sẽ tiếp tục được cập nhật trong quá trình nghiên cứu.

---

## Tiến độ

- [x] Khảo sát thiết bị ban đầu
- [x] Tháo và quan sát phần cứng
- [ ] Xác định toàn bộ linh kiện chính
- [ ] Xác định MCU
- [ ] Xác định module LoRaWAN
- [ ] Phân tích nguyên lý buồng cảm biến
- [ ] Đo tín hiệu cảm biến
- [ ] Phân tích firmware
- [ ] Phân tích payload LoRaWAN
- [ ] Thiết kế chức năng đo mức khói
- [ ] Tích hợp phần cứng
- [ ] Tích hợp firmware
- [ ] Truyền dữ liệu LoRaWAN
- [ ] Thực nghiệm
- [ ] Hiệu chuẩn
- [ ] Đánh giá kết quả
- [ ] Hoàn thiện báo cáo

---

## Kết quả mong đợi

Sau khi hoàn thành, hệ thống dự kiến có khả năng:

- Phát hiện sự xuất hiện của khói.
- Đánh giá mức độ khói.
- Phân loại nhiều mức cảnh báo.
- Cảnh báo bằng buzzer tại thiết bị.
- Truyền trạng thái thiết bị qua LoRaWAN.
- Truyền giá trị mức khói về Server.
- Theo dõi tình trạng pin.
- Lưu trữ dữ liệu phục vụ phân tích.

---


## Tài liệu tham khảo

Các datasheet, tài liệu kỹ thuật và bài báo liên quan sẽ được lưu trong:

```text
references/
```

---

## Trạng thái dự án

> 🚧 **Project under development**

Dự án hiện đang trong giai đoạn nghiên cứu và thử nghiệm. Nội dung, sơ đồ phần cứng, firmware và kết quả thực nghiệm sẽ được cập nhật trong quá trình phát triển.

---

## License

Dự án được sử dụng cho mục đích nghiên cứu và học tập.
