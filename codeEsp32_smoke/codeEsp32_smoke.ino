#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

// ============================================================================
//  ESP32 SMOKE SENSOR - ADC STREAMING + WEB LOG
//
//  Chức năng:
//  1. ESP32 kết nối vào Wi-Fi ở chế độ Station.
//  2. Đọc tín hiệu analog từ mạch photodiode tại GPIO34.
//  3. Stream dữ liệu ADC trực tiếp lên trình duyệt bằng Server-Sent Events (SSE).
//  4. Hiển thị đồ thị ADC thời gian thực.
//  5. Tự động bắt đầu một phiên ghi log khi ADC vượt ngưỡng START_THRESHOLD.
//  6. Tự động kết thúc phiên khi ADC thấp hơn STOP_THRESHOLD liên tục
//     trong END_HOLD_TIME.
//  7. Mỗi log lưu:
//       - Đồ thị ADC theo thời gian
//       - IR ADC lớn nhất / trung bình
//       - Gas AO lớn nhất / trung bình
//       - Tỷ lệ DO ở mức HIGH
//       - Cụm 3 đồ thị: IR, AO và DO
//       - Thời gian bắt đầu
//       - Thời gian kết thúc
//       - Thời lượng
//       - Số mẫu
//  8. Các log hoàn thành được lưu trong localStorage của trình duyệt,
//     nên refresh trang vẫn còn dữ liệu.
//
//  Lưu ý:
//  - Logic hiện tại giả sử: khói tăng -> ADC tăng.
//  - Nếu mạch của bạn cho quan hệ ngược lại (khói tăng -> ADC giảm),
//    cần đảo điều kiện START/STOP trong hàm processMeasurement() ở JavaScript.
// ============================================================================


// ============================================================================
// CẤU HÌNH WIFI
// ============================================================================

// Thay bằng tên Wi-Fi và mật khẩu thực tế.
const char* WIFI_SSID = "Khu S";
const char* WIFI_PASSWORD = "khu@s2022";


// ============================================================================
// CẤU HÌNH ADC
// ============================================================================

// GPIO34: tín hiệu analog từ mạch photodiode hồng ngoại.
// GPIO35: chân AO (Analog Output) của cảm biến MP-2/MQ-2.
// GPIO32: chân DO (Digital Output) của cảm biến MP-2/MQ-2.
//
// GPIO34 và GPIO35 được dùng để đọc ADC.
// GPIO32 được dùng để đọc trạng thái digital HIGH/LOW từ comparator trên module.
const int IR_PIN = 34;
const int GAS_AO_PIN = 35;
const int GAS_DO_PIN = 32;

// ADC ESP32 được đọc ở độ phân giải 12 bit: 0 -> 4095.
const int ADC_RESOLUTION_BITS = 12;

// Chu kỳ lấy mẫu.
// 50 ms tương đương 20 mẫu/giây, đủ nhanh cho tín hiệu khói và nhẹ cho web.
const unsigned long SAMPLE_INTERVAL_MS = 50;

// Thời gian giữa hai lần in ADC ra Serial Monitor.
// Không nên Serial.print ở mỗi mẫu vì sẽ làm log Serial quá nhiều.
const unsigned long SERIAL_PRINT_INTERVAL_MS = 500;

unsigned long lastSampleTime = 0;
unsigned long lastSerialPrintTime = 0;


// ============================================================================
// CẤU HÌNH TỰ ĐỘNG KẾT NỐI LẠI WIFI
// ============================================================================

const unsigned long WIFI_RECONNECT_INTERVAL_MS = 5000;
unsigned long lastWiFiReconnectTime = 0;


// ============================================================================
// WEB SERVER + SERVER SENT EVENTS
// ============================================================================

// Web Server chạy ở cổng 80.
AsyncWebServer server(80);

// Endpoint /events dùng để stream dữ liệu ADC xuống trình duyệt.
AsyncEventSource events("/events");


// ============================================================================
// TRANG WEB
// ============================================================================

const char PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">

<head>
    <meta charset="UTF-8">

    <meta
        name="viewport"
        content="width=device-width, initial-scale=1.0"
    >

    <title>ESP32 Smoke Sensor Logger</title>

    <style>
        /* ==================================================================
           CÀI ĐẶT CHUNG
           ================================================================== */

        * {
            box-sizing: border-box;
        }

        body {
            margin: 0;
            padding: 20px;
            background: #111827;
            color: #f3f4f6;
            font-family: Arial, sans-serif;
        }

        .container {
            max-width: 1200px;
            margin: auto;
        }

        h1,
        h2 {
            margin-top: 0;
        }

        /* ==================================================================
           HEADER
           ================================================================== */

        .header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            flex-wrap: wrap;
            gap: 15px;
            margin-bottom: 20px;
        }

        .header h1 {
            margin-bottom: 5px;
        }

        .subtitle {
            color: #9ca3af;
        }

        /* ==================================================================
           TRẠNG THÁI KẾT NỐI
           ================================================================== */

        .statusBox {
            display: flex;
            align-items: center;
            gap: 10px;
            padding: 10px 16px;
            background: #1f2937;
            border-radius: 30px;
        }

        .statusDot {
            width: 12px;
            height: 12px;
            border-radius: 50%;
            background: #f59e0b;
            box-shadow: 0 0 8px #f59e0b;
        }

        .statusText {
            color: #f59e0b;
            font-size: 14px;
            font-weight: bold;
        }

        /* ==================================================================
           THẺ DỮ LIỆU
           ================================================================== */


        /* ==================================================================
           ĐỒ THỊ THỜI GIAN THỰC
           ================================================================== */

        .chartBox {
            padding: 15px;
            background: #1f2937;
            border-radius: 12px;
        }

        .realtimeGrid {
            display: grid;
            grid-template-columns: 1fr;
            gap: 16px;
        }

        .chartTitle {
            margin-bottom: 4px;
            font-size: 16px;
            font-weight: bold;
        }

        .chartUnit {
            margin-bottom: 10px;
            color: #9ca3af;
            font-size: 13px;
        }

        #irChart,
        #gasAnalogChart,
        #gasDigitalChart {
            width: 100%;
            height: 300px;
            display: block;
        }

        #gasDigitalChart {
            height: 210px;
        }

        .liveControls {
            margin-top: 12px;
            margin-bottom: 5px;
        }

        .controls {
            display: flex;
            gap: 10px;
            flex-wrap: wrap;
            margin-top: 15px;
        }

        button {
            padding: 10px 18px;
            border: none;
            border-radius: 8px;
            font-size: 15px;
            cursor: pointer;
        }

        #pauseBtn {
            background: #f59e0b;
            color: #111827;
        }

        #clearLiveBtn {
            background: #374151;
            color: white;
        }

        #clearLogsBtn {
            background: #7f1d1d;
            color: white;
        }

        /* ==================================================================
           TRẠNG THÁI GHI LOG
           ================================================================== */

        .recordPanel {
            display: flex;
            justify-content: space-between;
            align-items: center;
            gap: 15px;
            flex-wrap: wrap;
            margin-top: 20px;
            padding: 16px 18px;
            background: #1f2937;
            border-radius: 12px;
        }

        .recordInfo {
            color: #9ca3af;
            font-size: 14px;
        }

        .recordStatus {
            padding: 8px 14px;
            border-radius: 20px;
            font-size: 13px;
            font-weight: bold;
        }

        .recordStatus.waiting {
            background: #374151;
            color: #d1d5db;
        }

        .recordStatus.recording {
            background: #7f1d1d;
            color: #fca5a5;
        }

        /* ==================================================================
           KHU VỰC NHẬT KÝ
           ================================================================== */

        .logSection {
            margin-top: 25px;
        }

        .logHeader {
            display: flex;
            justify-content: space-between;
            align-items: center;
            gap: 10px;
            flex-wrap: wrap;
            margin-bottom: 15px;
        }

        .logHeader h2 {
            margin-bottom: 0;
        }

        .logItem {
            margin-bottom: 18px;
            padding: 18px;
            background: #1f2937;
            border-radius: 12px;
        }

        .logTitle {
            margin-bottom: 15px;
            font-size: 18px;
            font-weight: bold;
        }

        .logInfo {
            display: grid;
            grid-template-columns: repeat(4, 1fr);
            gap: 12px;
            margin-bottom: 15px;
        }

        .logInfoItem {
            padding: 12px;
            background: #111827;
            border-radius: 8px;
        }

        .logInfoLabel {
            margin-bottom: 5px;
            color: #9ca3af;
            font-size: 12px;
        }

        .logInfoValue {
            font-size: 16px;
            font-weight: bold;
        }

        .logCanvas {
            width: 100%;
            height: 230px;
            display: block;
            background: #111827;
            border-radius: 8px;
        }


        /* ==================================================================
           CỤM 3 ĐỒ THỊ CỦA MỖI PHIÊN
           ================================================================== */

        .logChartGroup {
            display: grid;
            grid-template-columns: 1fr;
            gap: 14px;
        }

        .logChartBlock {
            padding: 12px;
            background: #111827;
            border-radius: 10px;
        }

        .logChartTitle {
            margin-bottom: 10px;
            color: #d1d5db;
            font-size: 14px;
            font-weight: bold;
        }

        .logChartBlock .logCanvas {
            background: #0b1220;
        }

        .digitalCanvas {
            height: 180px;
        }

        .emptyLog {
            padding: 20px;
            background: #1f2937;
            color: #9ca3af;
            border-radius: 12px;
            text-align: center;
        }

        /* ==================================================================
           RESPONSIVE
           ================================================================== */

        @media (max-width: 950px) {
            .logInfo {
                grid-template-columns: repeat(2, 1fr);
            }
        }

        @media (max-width: 520px) {
            body {
                padding: 12px;
            }

            #irChart,
            #gasAnalogChart {
                height: 260px;
            }

            #gasDigitalChart {
                height: 180px;
            }

            .logInfo {
                grid-template-columns: 1fr;
            }

            .logCanvas {
                height: 200px;
            }
        }
    </style>
</head>

<body>

<div class="container">

    <!-- ================================================================
         HEADER
         ================================================================ -->

    <div class="header">

        <div>
            <h1>ESP32 Smoke Sensor</h1>

            <div class="subtitle">
                Photodiode GPIO34 + Gas AO GPIO35 + Gas DO GPIO32 — Streaming SSE
            </div>
        </div>

        <div class="statusBox">

            <div
                id="statusDot"
                class="statusDot">
            </div>

            <div
                id="statusText"
                class="statusText">
                ĐANG KẾT NỐI
            </div>

        </div>

    </div>


    <!-- ================================================================
         CỤM ĐỒ THỊ THỜI GIAN THỰC
         ================================================================ -->

    <div class="realtimeGrid">

        <!-- ------------------------------------------------------------
             1. PHOTODIODE
             Đơn vị: ADC raw 12 bit (0..4095)
             ------------------------------------------------------------ -->

        <div class="chartBox">

            <div class="chartTitle">
                Photodiode IR — GPIO34
            </div>

            <div class="chartUnit">
                Đơn vị: RAW ADC 12 bit
            </div>

            <canvas id="irChart"></canvas>

        </div>


        <!-- ------------------------------------------------------------
             2. MP-2 / MQ-2 ANALOG OUTPUT
             Đơn vị: mV tại chân GPIO35 của ESP32
             ------------------------------------------------------------ -->

        <div class="chartBox">

            <div class="chartTitle">
                MP-2 / MQ-2 Analog AO — GPIO35
            </div>

            <div class="chartUnit">
                Đơn vị: RAW ADC 12 bit
            </div>

            <canvas id="gasAnalogChart"></canvas>

        </div>


        <!-- ------------------------------------------------------------
             3. MP-2 / MQ-2 DIGITAL OUTPUT
             Đơn vị: Logic 0 / 1
             ------------------------------------------------------------ -->

        <div class="chartBox">

            <div class="chartTitle">
                MP-2 / MQ-2 Digital DO — GPIO32
            </div>

            <div class="chartUnit">
                Đơn vị: Logic LOW/HIGH (0/1)
            </div>

            <canvas id="gasDigitalChart"></canvas>

        </div>

    </div>


    <!-- ================================================================
         ĐIỀU KHIỂN ĐỒ THỊ REALTIME
         ================================================================ -->

    <div class="controls liveControls">

        <button
            id="pauseBtn"
            onclick="togglePause()">
            Tạm dừng đồ thị
        </button>

        <button
            id="clearLiveBtn"
            onclick="clearLiveData()">
            Xóa dữ liệu hiện tại
        </button>

    </div>


    <!-- ================================================================
         TRẠNG THÁI GHI PHIÊN
         ================================================================ -->

    <div class="recordPanel">

        <div>
            <strong>Ghi log tự động</strong>

            <div
                class="recordInfo"
                id="recordInfo">
                Bắt đầu khi ADC ≥ 500, kết thúc khi ADC ≤ 400 liên tục 2 giây.
            </div>
        </div>

        <div
            id="recordStatus"
            class="recordStatus waiting">
            ĐANG CHỜ TÍN HIỆU
        </div>

    </div>


    <!-- ================================================================
         NHẬT KÝ CÁC PHIÊN ĐÃ ĐO
         ================================================================ -->

    <div class="logSection">

        <div class="logHeader">

            <h2>Nhật ký đo</h2>

            <button
                id="clearLogsBtn"
                onclick="clearLogs()">
                Xóa toàn bộ log
            </button>

        </div>

        <div id="logContainer"></div>

    </div>

</div>


<script>

// ============================================================================
// CẤU HÌNH ĐỒ THỊ THỜI GIAN THỰC
// ============================================================================

// Số điểm tối đa hiển thị trên đồ thị realtime.
// Dữ liệu cũ hơn sẽ được loại khỏi đồ thị để tránh chậm trình duyệt.
const MAX_POINTS = 500;

// Photodiode: ADC raw 12 bit.
const IR_Y_MIN = 0;
const IR_Y_MAX = 5000; 

// MP-2/MQ-2 AO: ADC raw 12 bit.
const GAS_RAW_MIN = 0;
const GAS_RAW_MAX = 5000;

// MP-2/MQ-2 DO: chỉ có 0 hoặc 1.
const DIGITAL_MIN = 0;
const DIGITAL_MAX = 1;


// ============================================================================
// CẤU HÌNH PHÁT HIỆN MỘT PHIÊN CÓ KHÓI
// ============================================================================

// Khi ADC >= START_THRESHOLD thì bắt đầu ghi log.
const START_THRESHOLD = 500;

// Khi đang ghi, nếu ADC <= STOP_THRESHOLD thì bắt đầu tính thời gian chờ kết thúc.
const STOP_THRESHOLD = 400;

// Tín hiệu phải nằm dưới STOP_THRESHOLD liên tục đủ thời gian này mới kết thúc.
// Mục đích: tránh ADC dao động quanh ngưỡng làm phiên log bị ngắt sai.
const END_HOLD_TIME = 2000;

// Số log hoàn chỉnh tối đa giữ trên trình duyệt.
const MAX_LOGS = 20;

// Tên vùng lưu localStorage.
const STORAGE_KEY = "esp32_smoke_logs_ir_ao_do_v3";


// ============================================================================
// BIẾN ĐỒ THỊ REALTIME
// ============================================================================

const irCanvas =
    document.getElementById(
        "irChart"
    );

const irCtx =
    irCanvas.getContext(
        "2d"
    );

const gasAnalogCanvas =
    document.getElementById(
        "gasAnalogChart"
    );

const gasAnalogCtx =
    gasAnalogCanvas.getContext(
        "2d"
    );

const gasDigitalCanvas =
    document.getElementById(
        "gasDigitalChart"
    );

const gasDigitalCtx =
    gasDigitalCanvas.getContext(
        "2d"
    );

// Ba mảng được giữ đồng bộ theo cùng một thời điểm lấy mẫu.
//
// liveIRData:
//     ADC raw photodiode, 0..4095.
//
// liveGasAnalogRawData:
//     ADC raw của chân AO GPIO35, 0..4095.
//
// liveGasDigitalData:
//     Trạng thái DO, 0 hoặc 1.
let liveIRData = [];
let liveGasAnalogRawData = [];
let liveGasDigitalData = [];

// paused chỉ tạm dừng cập nhật đồ thị realtime.
// Việc phát hiện và ghi log vẫn tiếp tục hoạt động.
let paused = false;


// ============================================================================
// BIẾN QUẢN LÝ PHIÊN LOG
// ============================================================================

// true khi đang có một phiên đo được ghi.
let recording = false;

// Log đang ghi hiện tại.
let currentLog = null;

// Danh sách các log đã hoàn thành.
let logHistory = [];

// Thời điểm đầu tiên tín hiệu xuống dưới STOP_THRESHOLD.
let belowThresholdSince = null;

// ID tăng dần cho từng phiên.
let logCounter = 0;


// ============================================================================
// HÀM CHUẨN BỊ CANVAS
// ============================================================================

function prepareCanvas(
    canvas,
    context
) {

    const rect =
        canvas.getBoundingClientRect();

    const ratio =
        window.devicePixelRatio ||
        1;

    canvas.width =
        rect.width *
        ratio;

    canvas.height =
        rect.height *
        ratio;

    context.setTransform(
        ratio,
        0,
        0,
        ratio,
        0,
        0
    );
}


// ============================================================================
// RESIZE TOÀN BỘ ĐỒ THỊ REALTIME
// ============================================================================

function resizeRealtimeCharts() {

    prepareCanvas(
        irCanvas,
        irCtx
    );

    prepareCanvas(
        gasAnalogCanvas,
        gasAnalogCtx
    );

    prepareCanvas(
        gasDigitalCanvas,
        gasDigitalCtx
    );

    drawIRRealtimeChart();
    drawGasAnalogRealtimeChart();
    drawGasDigitalRealtimeChart();
}


// Khi thay đổi kích thước cửa sổ, vẽ lại toàn bộ đồ thị.
window.addEventListener(
    "resize",
    function() {

        resizeRealtimeCharts();

        setTimeout(
            function() {
                redrawAllLogCharts();
            },
            50
        );
    }
);


// ============================================================================
// HÀM VẼ ĐỒ THỊ ANALOG REALTIME DÙNG CHUNG
// ============================================================================

function drawAnalogRealtimeChart(
    canvas,
    context,
    values,
    yMin,
    yMax,
    unitText,
    lineColor
) {

    const width =
        canvas.clientWidth;

    const height =
        canvas.clientHeight;

    context.clearRect(
        0,
        0,
        width,
        height
    );

    const left = 62;
    const right = 18;
    const top = 15;
    const bottom = 35;

    const plotWidth =
        width -
        left -
        right;

    const plotHeight =
        height -
        top -
        bottom;

    context.strokeStyle =
        "#374151";

    context.lineWidth =
        1;

    context.fillStyle =
        "#9ca3af";

    context.font =
        "11px Arial";

    const gridCount =
        5;

    // ------------------------------------------------------------------------
    // Lưới + nhãn trục Y.
    // ------------------------------------------------------------------------

    for (
        let i = 0;
        i <= gridCount;
        i++
    ) {

        const y =
            top +
            (
                plotHeight /
                gridCount
            ) *
            i;

        context.beginPath();

        context.moveTo(
            left,
            y
        );

        context.lineTo(
            width - right,
            y
        );

        context.stroke();

        const axisValue =
            yMax -
            (
                (
                    yMax -
                    yMin
                ) /
                gridCount
            ) *
            i;

        context.fillText(
            Math.round(
                axisValue
            ),
            5,
            y + 4
        );
    }

    // Đơn vị của riêng đồ thị.
    context.fillStyle =
        "#9ca3af";

    context.fillText(
        unitText,
        5,
        12
    );

    if (
        values.length <
        2
    ) {
        return;
    }

    // ------------------------------------------------------------------------
    // Đường dữ liệu.
    // ------------------------------------------------------------------------

    context.strokeStyle =
        lineColor;

    context.lineWidth =
        2;

    context.lineJoin =
        "round";

    context.lineCap =
        "round";

    context.beginPath();

    for (
        let i = 0;
        i < values.length;
        i++
    ) {

        const x =
            left +
            (
                i /
                Math.max(
                    MAX_POINTS - 1,
                    1
                )
            ) *
            plotWidth;

        const normalized =
            (
                values[i] -
                yMin
            ) /
            (
                yMax -
                yMin
            );

        const clamped =
            Math.max(
                0,
                Math.min(
                    1,
                    normalized
                )
            );

        const y =
            top +
            plotHeight -
            clamped *
            plotHeight;

        if (
            i ===
            0
        ) {

            context.moveTo(
                x,
                y
            );

        } else {

            context.lineTo(
                x,
                y
            );
        }
    }

    context.stroke();
}


// ============================================================================
// REALTIME 1: PHOTODIODE IR - ADC RAW 12 BIT
// ============================================================================

function drawIRRealtimeChart() {

    drawAnalogRealtimeChart(
        irCanvas,
        irCtx,
        liveIRData,
        IR_Y_MIN,
        IR_Y_MAX,
        "ADC",
        "#38bdf8"
    );
}


// ============================================================================
// REALTIME 2: MP-2 / MQ-2 AO - RAW ADC 12 BIT
// ============================================================================

function drawGasAnalogRealtimeChart() {

    drawAnalogRealtimeChart(
        gasAnalogCanvas,
        gasAnalogCtx,
        liveGasAnalogRawData,
        GAS_RAW_MIN,
        GAS_RAW_MAX,
        "RAW",
        "#f59e0b"
    );
}


// ============================================================================
// REALTIME 3: MP-2 / MQ-2 DO - LOGIC 0/1
// ============================================================================

function drawGasDigitalRealtimeChart() {

    const width =
        gasDigitalCanvas.clientWidth;

    const height =
        gasDigitalCanvas.clientHeight;

    gasDigitalCtx.clearRect(
        0,
        0,
        width,
        height
    );

    const left = 62;
    const right = 18;
    const top = 20;
    const bottom = 35;

    const plotWidth =
        width -
        left -
        right;

    const plotHeight =
        height -
        top -
        bottom;

    const yHigh =
        top +
        plotHeight *
        0.2;

    const yLow =
        top +
        plotHeight *
        0.8;

    // Hai đường mức logic.
    gasDigitalCtx.strokeStyle =
        "#374151";

    gasDigitalCtx.lineWidth =
        1;

    gasDigitalCtx.beginPath();

    gasDigitalCtx.moveTo(
        left,
        yHigh
    );

    gasDigitalCtx.lineTo(
        width - right,
        yHigh
    );

    gasDigitalCtx.stroke();

    gasDigitalCtx.beginPath();

    gasDigitalCtx.moveTo(
        left,
        yLow
    );

    gasDigitalCtx.lineTo(
        width - right,
        yLow
    );

    gasDigitalCtx.stroke();

    gasDigitalCtx.fillStyle =
        "#9ca3af";

    gasDigitalCtx.font =
        "11px Arial";

    gasDigitalCtx.fillText(
        "HIGH 1",
        5,
        yHigh + 4
    );

    gasDigitalCtx.fillText(
        "LOW 0",
        5,
        yLow + 4
    );

    gasDigitalCtx.fillText(
        "Logic",
        5,
        12
    );

    if (
        liveGasDigitalData.length <
        2
    ) {
        return;
    }

    // ------------------------------------------------------------------------
    // Vẽ DO dạng bậc thang.
    // ------------------------------------------------------------------------

    gasDigitalCtx.strokeStyle =
        "#a78bfa";

    gasDigitalCtx.lineWidth =
        2;

    gasDigitalCtx.lineJoin =
        "miter";

    gasDigitalCtx.beginPath();

    let previousY =
        liveGasDigitalData[0]
            ?
            yHigh
            :
            yLow;

    gasDigitalCtx.moveTo(
        left,
        previousY
    );

    for (
        let i = 1;
        i < liveGasDigitalData.length;
        i++
    ) {

        const x =
            left +
            (
                i /
                Math.max(
                    MAX_POINTS - 1,
                    1
                )
            ) *
            plotWidth;

        const currentY =
            liveGasDigitalData[i]
                ?
                yHigh
                :
                yLow;

        gasDigitalCtx.lineTo(
            x,
            previousY
        );

        if (
            currentY !==
            previousY
        ) {

            gasDigitalCtx.lineTo(
                x,
                currentY
            );
        }

        previousY =
            currentY;
    }

    gasDigitalCtx.stroke();
}


// ============================================================================
// CẬP NHẬT TRẠNG THÁI KẾT NỐI SSE
// ============================================================================

function setConnectionStatus(
    text,
    color
) {

    const dot =
        document.getElementById(
            "statusDot"
        );

    const status =
        document.getElementById(
            "statusText"
        );

    status.innerText = text;
    status.style.color = color;

    dot.style.background = color;

    dot.style.boxShadow =
        "0 0 8px " +
        color;
}


// ============================================================================
// THÊM MỘT MẪU VÀO CỤM ĐỒ THỊ REALTIME
// ============================================================================

function addLiveValue(
    irValue,
    gasAnalogRaw,
    gasDigitalValue
) {

    // Tạm dừng chỉ ảnh hưởng phần hiển thị.
    // Logic ghi log vẫn chạy bình thường.
    if (paused) {
        return;
    }

    liveIRData.push(
        irValue
    );

    liveGasAnalogRawData.push(
        gasAnalogRaw
    );

    liveGasDigitalData.push(
        gasDigitalValue
    );

    if (
        liveIRData.length >
        MAX_POINTS
    ) {
        liveIRData.shift();
    }

    if (
        liveGasAnalogRawData.length >
        MAX_POINTS
    ) {
        liveGasAnalogRawData.shift();
    }

    if (
        liveGasDigitalData.length >
        MAX_POINTS
    ) {
        liveGasDigitalData.shift();
    }

    drawIRRealtimeChart();
    drawGasAnalogRealtimeChart();
    drawGasDigitalRealtimeChart();
}


// ============================================================================
// TRẠNG THÁI GHI LOG
// ============================================================================

function setRecordStatus(isRecording) {

    const element =
        document.getElementById(
            "recordStatus"
        );

    if (isRecording) {

        element.innerText =
            "● ĐANG GHI LOG";

        element.className =
            "recordStatus recording";

    } else {

        element.innerText =
            "ĐANG CHỜ TÍN HIỆU";

        element.className =
            "recordStatus waiting";
    }
}


// ============================================================================
// BẮT ĐẦU MỘT PHIÊN LOG MỚI
// ============================================================================
//
// Phiên vẫn được kích hoạt theo tín hiệu IR để giữ nguyên thuật toán cũ.
// Khi phiên bắt đầu, cả IR và Gas GPIO35 đều được ghi đồng thời.
// ============================================================================

function startLog(
    irValue,
    gasAnalogRaw,
    gasDigitalValue,
    espTime
) {

    recording = true;
    belowThresholdSince = null;

    logCounter++;

    currentLog = {

        // ID của phiên.
        id: logCounter,

        // Đồng hồ thực của thiết bị đang mở trình duyệt.
        startDate:
            new Date().toISOString(),

        // millis() của ESP32 dùng để tính thời lượng.
        startEspTime:
            espTime,

        endDate:
            null,

        duration:
            0,

        // Dữ liệu thô theo thời gian.
        irValues: [],
        gasAnalogRawValues: [],
        gasDigitalValues: [],
        times: [],

        // Dùng tính trung bình.
        irSum:
            0,

        gasAnalogRawSum:
            0,

        count:
            0,

        // Thống kê Digital Output.
        digitalHighCount:
            0,

        digitalLowCount:
            0,

        // Giá trị lớn nhất trong phiên.
        maxIR:
            irValue,

        maxGasAnalogRaw:
            gasAnalogRaw,

        // Giá trị trung bình sẽ được tính khi kết thúc phiên.
        averageIR:
            0,

        averageGasAnalogRaw:
            0,

        // Tỷ lệ thời gian DO ở mức HIGH trong phiên (%).
        digitalHighPercent:
            0
    };

    setRecordStatus(
        true
    );

    console.log(
        "Bat dau log #",
        currentLog.id
    );
}


// ============================================================================
// THÊM MẪU IR + GAS VÀO PHIÊN LOG HIỆN TẠI
// ============================================================================

function addLogSample(
    irValue,
    gasAnalogRaw,
    gasDigitalValue,
    espTime
) {

    if (
        !recording ||
        currentLog === null
    ) {
        return;
    }

    // Thời gian tương đối tính từ lúc bắt đầu phiên, đơn vị ms.
    const elapsed =
        espTime -
        currentLog.startEspTime;

    currentLog.irValues.push(
        irValue
    );

    currentLog.gasAnalogRawValues.push(
        gasAnalogRaw
    );

    currentLog.gasDigitalValues.push(
        gasDigitalValue
    );

    currentLog.times.push(
        elapsed
    );

    currentLog.irSum +=
        irValue;

    currentLog.gasAnalogRawSum +=
        gasAnalogRaw;

    currentLog.count++;

    if (
        irValue >
        currentLog.maxIR
    ) {
        currentLog.maxIR =
            irValue;
    }

    if (
        gasAnalogRaw >
        currentLog.maxGasAnalogRaw
    ) {
        currentLog.maxGasAnalogRaw =
            gasAnalogRaw;
    }

    if (gasDigitalValue) {
        currentLog.digitalHighCount++;
    } else {
        currentLog.digitalLowCount++;
    }
}


// ============================================================================
// KẾT THÚC PHIÊN LOG HIỆN TẠI
// ============================================================================

function finishLog(espTime) {

    if (
        currentLog === null
    ) {
        return;
    }

    currentLog.endDate =
        new Date().toISOString();

    currentLog.duration =
        espTime -
        currentLog.startEspTime;

    currentLog.averageIR =
        currentLog.count > 0
            ?
            currentLog.irSum /
            currentLog.count
            :
            0;

    currentLog.averageGasAnalogRaw =
        currentLog.count > 0
            ?
            currentLog.gasAnalogRawSum /
            currentLog.count
            :
            0;

    currentLog.digitalHighPercent =
        currentLog.count > 0
            ?
            (
                currentLog.digitalHighCount /
                currentLog.count
            ) *
            100
            :
            0;

    // Đưa log mới nhất lên đầu danh sách.
    logHistory.unshift(
        currentLog
    );

    // Chỉ giữ tối đa MAX_LOGS phiên hoàn chỉnh.
    if (
        logHistory.length >
        MAX_LOGS
    ) {
        logHistory =
            logHistory.slice(
                0,
                MAX_LOGS
            );
    }

    console.log(
        "Ket thuc log #",
        currentLog.id,
        currentLog
    );

    // Lưu vào localStorage để refresh trang vẫn còn log.
    saveLogs();

    // Hiển thị danh sách log.
    renderLogs();

    // Reset để chờ phiên tiếp theo.
    currentLog = null;
    recording = false;
    belowThresholdSince = null;

    setRecordStatus(
        false
    );
}


// ============================================================================
// THUẬT TOÁN PHÁT HIỆN BẮT ĐẦU / KẾT THÚC PHIÊN
// ============================================================================
//
// Hiện tại việc START/STOP vẫn dựa trên ADC photodiode GPIO34.
// GAS GPIO35 chỉ được ghi đồng bộ để phục vụ so sánh và hiệu chuẩn.
// ============================================================================

function processMeasurement(
    irValue,
    gasAnalogRaw,
    gasDigitalValue,
    espTime
) {

    // ------------------------------------------------------------------------
    // CHƯA GHI LOG
    // ------------------------------------------------------------------------

    if (!recording) {

        if (
            irValue >=
            START_THRESHOLD
        ) {

            startLog(
                irValue,
                gasAnalogRaw,
                gasDigitalValue,
                espTime
            );

            addLogSample(
                irValue,
                gasAnalogRaw,
                gasDigitalValue,
                espTime
            );
        }

        return;
    }


    // ------------------------------------------------------------------------
    // ĐANG GHI LOG
    // ------------------------------------------------------------------------

    addLogSample(
        irValue,
        gasAnalogRaw,
        gasDigitalValue,
        espTime
    );


    // ------------------------------------------------------------------------
    // KIỂM TRA KẾT THÚC THEO TÍN HIỆU IR
    // ------------------------------------------------------------------------

    if (
        irValue <=
        STOP_THRESHOLD
    ) {

        if (
            belowThresholdSince ===
            null
        ) {
            belowThresholdSince =
                espTime;
        }

        if (
            espTime -
            belowThresholdSince >=
            END_HOLD_TIME
        ) {
            finishLog(
                espTime
            );
        }

    } else {

        // IR tăng lại trước khi hết thời gian chờ -> hủy chờ kết thúc.
        belowThresholdSince =
            null;
    }
}


// ============================================================================
// FORMAT THỜI GIAN
// ============================================================================

function formatDate(dateString) {

    if (!dateString) {
        return "-";
    }

    const date =
        new Date(
            dateString
        );

    return date.toLocaleTimeString(
        "vi-VN",
        {
            hour: "2-digit",
            minute: "2-digit",
            second: "2-digit"
        }
    );
}


function formatDuration(ms) {

    const totalSeconds =
        ms /
        1000;

    if (
        totalSeconds <
        60
    ) {

        return (
            totalSeconds.toFixed(1) +
            " giây"
        );
    }

    const minutes =
        Math.floor(
            totalSeconds /
            60
        );

    const seconds =
        totalSeconds -
        minutes *
        60;

    return (
        minutes +
        " phút " +
        seconds.toFixed(1) +
        " giây"
    );
}


// ============================================================================
// LƯU LOG VÀO LOCALSTORAGE
// ============================================================================

function saveLogs() {

    try {

        localStorage.setItem(
            STORAGE_KEY,
            JSON.stringify(
                logHistory
            )
        );

    } catch (error) {

        console.warn(
            "Khong the luu log vao localStorage:",
            error
        );
    }
}


// ============================================================================
// ĐỌC LOG TỪ LOCALSTORAGE KHI MỞ / REFRESH TRANG
// ============================================================================

function loadLogs() {

    try {

        const stored =
            localStorage.getItem(
                STORAGE_KEY
            );

        if (!stored) {
            return;
        }

        const parsed =
            JSON.parse(
                stored
            );

        if (
            !Array.isArray(
                parsed
            )
        ) {
            return;
        }

        logHistory =
            parsed.slice(
                0,
                MAX_LOGS
            );

        // Tìm ID lớn nhất đã lưu để phiên tiếp theo không bị trùng ID.
        for (
            const log of logHistory
        ) {

            if (
                Number(log.id) >
                logCounter
            ) {

                logCounter =
                    Number(log.id);
            }
        }

    } catch (error) {

        console.warn(
            "Khong the doc log tu localStorage:",
            error
        );

        logHistory = [];
    }
}


// ============================================================================
// HIỂN THỊ DANH SÁCH LOG
// ============================================================================

function renderLogs() {

    const container =
        document.getElementById(
            "logContainer"
        );

    container.innerHTML =
        "";

    if (
        logHistory.length ===
        0
    ) {

        container.innerHTML =
            "<div class='emptyLog'>Chưa có phiên đo nào.</div>";

        return;
    }

    logHistory.forEach(
        function(log) {

            const item =
                document.createElement(
                    "div"
                );

            item.className =
                "logItem";

            const irCanvasId =
                "logIR_" +
                log.id;

            const aoCanvasId =
                "logAO_" +
                log.id;

            const doCanvasId =
                "logDO_" +
                log.id;

            item.innerHTML = `
                <div class="logTitle">
                    Phiên đo #${log.id}
                </div>

                <div class="logInfo">

                    <div class="logInfoItem">
                        <div class="logInfoLabel">
                            IR lớn nhất
                        </div>

                        <div class="logInfoValue">
                            ${Number(log.maxIR).toFixed(0)}
                        </div>
                    </div>

                    <div class="logInfoItem">
                        <div class="logInfoLabel">
                            IR trung bình
                        </div>

                        <div class="logInfoValue">
                            ${Number(log.averageIR).toFixed(1)}
                        </div>
                    </div>

                    <div class="logInfoItem">
                        <div class="logInfoLabel">
                            Gas AO lớn nhất (RAW)
                        </div>

                        <div class="logInfoValue">
                            ${Number(log.maxGasAnalogRaw).toFixed(0)}
                        </div>
                    </div>

                    <div class="logInfoItem">
                        <div class="logInfoLabel">
                            Gas AO trung bình (RAW)
                        </div>

                        <div class="logInfoValue">
                            ${Number(log.averageGasAnalogRaw).toFixed(1)}
                        </div>
                    </div>

                    <div class="logInfoItem">
                        <div class="logInfoLabel">
                            DO HIGH
                        </div>

                        <div class="logInfoValue">
                            ${Number(log.digitalHighPercent).toFixed(1)}%
                        </div>
                    </div>

                    <div class="logInfoItem">
                        <div class="logInfoLabel">
                            Bắt đầu
                        </div>

                        <div class="logInfoValue">
                            ${formatDate(log.startDate)}
                        </div>
                    </div>

                    <div class="logInfoItem">
                        <div class="logInfoLabel">
                            Kết thúc
                        </div>

                        <div class="logInfoValue">
                            ${formatDate(log.endDate)}
                        </div>
                    </div>

                    <div class="logInfoItem">
                        <div class="logInfoLabel">
                            Thời lượng
                        </div>

                        <div class="logInfoValue">
                            ${formatDuration(log.duration)}
                        </div>
                    </div>

                    <div class="logInfoItem">
                        <div class="logInfoLabel">
                            Số mẫu
                        </div>

                        <div class="logInfoValue">
                            ${log.count}
                        </div>
                    </div>

                </div>

                <div class="logChartGroup">

                    <div class="logChartBlock">
                        <div class="logChartTitle">
                            1. Photodiode IR — GPIO34
                        </div>

                        <canvas
                            id="${irCanvasId}"
                            class="logCanvas">
                        </canvas>
                    </div>

                    <div class="logChartBlock">
                        <div class="logChartTitle">
                            2. Gas Analog AO — GPIO35 (RAW)
                        </div>

                        <canvas
                            id="${aoCanvasId}"
                            class="logCanvas">
                        </canvas>
                    </div>

                    <div class="logChartBlock">
                        <div class="logChartTitle">
                            3. Gas Digital DO — GPIO32
                        </div>

                        <canvas
                            id="${doCanvasId}"
                            class="logCanvas digitalCanvas">
                        </canvas>
                    </div>

                </div>
            `;

            container.appendChild(
                item
            );
        }
    );

    // Chờ DOM tạo xong toàn bộ canvas rồi mới vẽ.
    setTimeout(
        function() {
            redrawAllLogCharts();
        },
        0
    );
}


// ============================================================================
// VẼ ĐỒ THỊ ANALOG CHO MỘT PHIÊN LOG
// ============================================================================

function drawAnalogLogChart(
    canvasId,
    times,
    values,
    yMin,
    yMax,
    unitText,
    lineColor
) {

    const logCanvas =
        document.getElementById(
            canvasId
        );

    if (!logCanvas) {
        return;
    }

    const logCtx =
        logCanvas.getContext(
            "2d"
        );

    const rect =
        logCanvas.getBoundingClientRect();

    const ratio =
        window.devicePixelRatio ||
        1;

    logCanvas.width =
        rect.width *
        ratio;

    logCanvas.height =
        rect.height *
        ratio;

    logCtx.setTransform(
        ratio,
        0,
        0,
        ratio,
        0,
        0
    );

    const width =
        rect.width;

    const height =
        rect.height;

    logCtx.clearRect(
        0,
        0,
        width,
        height
    );

    const left = 52;
    const right = 15;
    const top = 15;
    const bottom = 30;

    const plotWidth =
        width -
        left -
        right;

    const plotHeight =
        height -
        top -
        bottom;

    // ------------------------------------------------------------------------
    // LƯỚI + THANG ADC 0..4095
    // ------------------------------------------------------------------------

    logCtx.strokeStyle =
        "#374151";

    logCtx.lineWidth =
        1;

    logCtx.fillStyle =
        "#9ca3af";

    logCtx.font =
        "11px Arial";

    const gridCount =
        4;

    for (
        let i = 0;
        i <= gridCount;
        i++
    ) {

        const y =
            top +
            (
                plotHeight /
                gridCount
            ) *
            i;

        logCtx.beginPath();

        logCtx.moveTo(
            left,
            y
        );

        logCtx.lineTo(
            width - right,
            y
        );

        logCtx.stroke();

        const axisValue =
            yMax -
            (
                (
                    yMax -
                    yMin
                ) /
                gridCount
            ) *
            i;

        logCtx.fillText(
            Math.round(
                axisValue
            ),
            5,
            y + 4
        );
    }

    logCtx.fillStyle =
        "#9ca3af";

    logCtx.fillText(
        unitText,
        5,
        12
    );

    if (
        !Array.isArray(
            values
        ) ||
        !Array.isArray(
            times
        ) ||
        values.length <
        2
    ) {
        return;
    }

    let lastTime =
        Number(
            times[
                times.length -
                1
            ]
        );

    if (
        !isFinite(
            lastTime
        ) ||
        lastTime <=
        0
    ) {
        lastTime =
            1;
    }

    // ------------------------------------------------------------------------
    // ĐƯỜNG TÍN HIỆU
    // ------------------------------------------------------------------------

    logCtx.strokeStyle =
        lineColor;

    logCtx.lineWidth =
        2;

    logCtx.lineJoin =
        "round";

    logCtx.lineCap =
        "round";

    logCtx.beginPath();

    for (
        let i = 0;
        i < values.length;
        i++
    ) {

        const time =
            Number(
                times[i]
            );

        const value =
            Number(
                values[i]
            );

        const x =
            left +
            (
                time /
                lastTime
            ) *
            plotWidth;

        const normalized =
            (
                value -
                yMin
            ) /
            (
                yMax -
                yMin
            );

        const y =
            top +
            plotHeight -
            normalized *
            plotHeight;

        if (i === 0) {

            logCtx.moveTo(
                x,
                y
            );

        } else {

            logCtx.lineTo(
                x,
                y
            );
        }
    }

    logCtx.stroke();

    // ------------------------------------------------------------------------
    // THỜI GIAN TRỤC X
    // ------------------------------------------------------------------------

    logCtx.fillStyle =
        "#9ca3af";

    logCtx.fillText(
        "0 s",
        left,
        height - 8
    );

    const endLabel =
        (
            lastTime /
            1000
        ).toFixed(1) +
        " s";

    logCtx.fillText(
        endLabel,
        Math.max(
            left,
            width - 62
        ),
        height - 8
    );
}


// ============================================================================
// VẼ ĐỒ THỊ DIGITAL DO 0/1 CHO MỘT PHIÊN LOG
// ============================================================================

function drawDigitalLogChart(
    canvasId,
    times,
    values
) {

    const logCanvas =
        document.getElementById(
            canvasId
        );

    if (!logCanvas) {
        return;
    }

    const logCtx =
        logCanvas.getContext(
            "2d"
        );

    const rect =
        logCanvas.getBoundingClientRect();

    const ratio =
        window.devicePixelRatio ||
        1;

    logCanvas.width =
        rect.width *
        ratio;

    logCanvas.height =
        rect.height *
        ratio;

    logCtx.setTransform(
        ratio,
        0,
        0,
        ratio,
        0,
        0
    );

    const width =
        rect.width;

    const height =
        rect.height;

    logCtx.clearRect(
        0,
        0,
        width,
        height
    );

    const left = 52;
    const right = 15;
    const top = 20;
    const bottom = 30;

    const plotWidth =
        width -
        left -
        right;

    const plotHeight =
        height -
        top -
        bottom;

    const yHigh =
        top +
        plotHeight *
        0.2;

    const yLow =
        top +
        plotHeight *
        0.8;

    // ------------------------------------------------------------------------
    // HAI MỨC LOGIC 0 VÀ 1
    // ------------------------------------------------------------------------

    logCtx.strokeStyle =
        "#374151";

    logCtx.lineWidth =
        1;

    logCtx.fillStyle =
        "#9ca3af";

    logCtx.font =
        "11px Arial";

    logCtx.beginPath();
    logCtx.moveTo(left, yHigh);
    logCtx.lineTo(width - right, yHigh);
    logCtx.stroke();

    logCtx.beginPath();
    logCtx.moveTo(left, yLow);
    logCtx.lineTo(width - right, yLow);
    logCtx.stroke();

    logCtx.fillText(
        "1",
        25,
        yHigh + 4
    );

    logCtx.fillText(
        "0",
        25,
        yLow + 4
    );

    if (
        !Array.isArray(
            values
        ) ||
        !Array.isArray(
            times
        ) ||
        values.length <
        2
    ) {
        return;
    }

    let lastTime =
        Number(
            times[
                times.length -
                1
            ]
        );

    if (
        !isFinite(
            lastTime
        ) ||
        lastTime <=
        0
    ) {
        lastTime =
            1;
    }

    // ------------------------------------------------------------------------
    // VẼ TÍN HIỆU DẠNG BẬC THANG
    // ------------------------------------------------------------------------

    logCtx.strokeStyle =
        "#a78bfa";

    logCtx.lineWidth =
        2;

    logCtx.lineJoin =
        "miter";

    logCtx.beginPath();

    let previousY =
        Number(values[0])
            ?
            yHigh
            :
            yLow;

    logCtx.moveTo(
        left,
        previousY
    );

    for (
        let i = 1;
        i < values.length;
        i++
    ) {

        const x =
            left +
            (
                Number(times[i]) /
                lastTime
            ) *
            plotWidth;

        const currentY =
            Number(values[i])
                ?
                yHigh
                :
                yLow;

        // Đi ngang theo trạng thái cũ.
        logCtx.lineTo(
            x,
            previousY
        );

        // Nếu trạng thái đổi thì vẽ cạnh đứng.
        if (
            currentY !==
            previousY
        ) {

            logCtx.lineTo(
                x,
                currentY
            );
        }

        previousY =
            currentY;
    }

    logCtx.stroke();

    // ------------------------------------------------------------------------
    // THỜI GIAN TRỤC X
    // ------------------------------------------------------------------------

    logCtx.fillStyle =
        "#9ca3af";

    logCtx.fillText(
        "0 s",
        left,
        height - 8
    );

    const endLabel =
        (
            lastTime /
            1000
        ).toFixed(1) +
        " s";

    logCtx.fillText(
        endLabel,
        Math.max(
            left,
            width - 62
        ),
        height - 8
    );
}


// ============================================================================
// VẼ LẠI TOÀN BỘ CỤM 3 ĐỒ THỊ CỦA TỪNG PHIÊN
// ============================================================================

function redrawAllLogCharts() {

    for (
        const log of logHistory
    ) {

        // 1. Photodiode IR GPIO34.
        drawAnalogLogChart(
            "logIR_" +
            log.id,
            log.times,
            log.irValues,
            IR_Y_MIN,
            IR_Y_MAX,
            "ADC",
            "#38bdf8"
        );

        // 2. Analog Output AO GPIO35.
        drawAnalogLogChart(
            "logAO_" +
            log.id,
            log.times,
            log.gasAnalogRawValues,
            GAS_RAW_MIN,
            GAS_RAW_MAX,
            "RAW",
            "#f59e0b"
        );

        // 3. Digital Output DO GPIO32.
        drawDigitalLogChart(
            "logDO_" +
            log.id,
            log.times,
            log.gasDigitalValues
        );
    }
}


// ============================================================================
// NÚT TẠM DỪNG ĐỒ THỊ REALTIME
// ============================================================================

function togglePause() {

    paused =
        !paused;

    const button =
        document.getElementById(
            "pauseBtn"
        );

    if (paused) {

        button.innerText =
            "Tiếp tục đồ thị";

    } else {

        button.innerText =
            "Tạm dừng đồ thị";
    }
}


// ============================================================================
// XÓA DỮ LIỆU ĐỒ THỊ REALTIME
// ============================================================================

function clearLiveData() {

    // Chỉ xóa dữ liệu đang hiển thị trên đồ thị realtime.
    // Các log đã hoàn thành không bị xóa.
    liveIRData = [];
    liveGasAnalogRawData = [];
    liveGasDigitalData = [];

    drawIRRealtimeChart();
    drawGasAnalogRealtimeChart();
    drawGasDigitalRealtimeChart();
}


// ============================================================================
// XÓA TOÀN BỘ LOG ĐÃ HOÀN THÀNH
// ============================================================================

function clearLogs() {

    const ok =
        confirm(
            "Bạn có chắc muốn xóa toàn bộ nhật ký đo?"
        );

    if (!ok) {
        return;
    }

    logHistory = [];

    // Không reset logCounter để ID trong phiên hiện tại/tiếp theo không bị lặp
    // trong cùng lần mở trang.

    localStorage.removeItem(
        STORAGE_KEY
    );

    renderLogs();
}


// ============================================================================
// KẾT NỐI SERVER-SENT EVENTS
// ============================================================================

// Trình duyệt giữ một kết nối HTTP lâu dài tới /events.
// ESP32 chủ động đẩy từng gói ADC xuống mà không cần fetch liên tục.
const source =
    new EventSource(
        "/events"
    );


// Khi kết nối SSE thành công.
source.onopen =
    function() {

        setConnectionStatus(
            "ĐANG CHẠY",
            "#22c55e"
        );
    };


// Khi mất kết nối.
// EventSource sẽ tự thử kết nối lại.
source.onerror =
    function() {

        setConnectionStatus(
            "MẤT KẾT NỐI",
            "#ef4444"
        );
    };


// Nhận sự kiện tên "adc" do ESP32 gửi.
//
// Mỗi packet:
// {
//   "ir":    RAW ADC photodiode GPIO34,
//   "gasAO": RAW ADC chân AO GPIO35,
//   "gasDO": trạng thái DO GPIO32 (0 hoặc 1),
//   "time":  millis() ESP32
// }
source.addEventListener(
    "adc",
    function(event) {

        try {

            const packet =
                JSON.parse(
                    event.data
                );

            const irValue =
                Number(
                    packet.ir
                );

            const gasAnalogValue =
                Number(
                    packet.gasAO
                );

            const gasDigitalValue =
                Number(
                    packet.gasDO
                );

            const espTime =
                Number(
                    packet.time
                );

            if (
                !Number.isFinite(
                    irValue
                ) ||
                !Number.isFinite(
                    gasAnalogValue
                ) ||
                !Number.isFinite(
                    gasDigitalValue
                ) ||
                !Number.isFinite(
                    espTime
                )
            ) {
                return;
            }

            // Đồ thị realtime nhận đủ 3 dữ liệu.
            addLiveValue(
                irValue,
                gasAnalogValue,
                gasDigitalValue
            );

            // Một phiên log lưu đồng bộ cả IR raw, AO raw và DO raw.
            processMeasurement(
                irValue,
                gasAnalogValue,
                gasDigitalValue,
                espTime
            );

        } catch (error) {

            console.warn(
                "Loi du lieu SSE:",
                error
            );
        }
    }
);


// ============================================================================
// KHỞI TẠO TRANG WEB
// ============================================================================

// Hiển thị cấu hình ngưỡng hiện tại.
document.getElementById(
    "recordInfo"
).innerText =
    "Bắt đầu theo IR khi ADC ≥ " +
    START_THRESHOLD +
    ", kết thúc khi ADC ≤ " +
    STOP_THRESHOLD +
    " liên tục " +
    (
        END_HOLD_TIME /
        1000
    ).toFixed(1) +
    " giây.";


// Đọc các log đã lưu trước đó.
loadLogs();


// Hiển thị log.
renderLogs();


// Chuẩn bị 3 canvas realtime.
resizeRealtimeCharts();


// Trạng thái ban đầu.
setRecordStatus(
    false
);

</script>

</body>

</html>
)rawliteral";


// ============================================================================
// HÀM KẾT NỐI WIFI LẦN ĐẦU
// ============================================================================

void connectWiFi()
{
    Serial.println();
    Serial.println("========================================");
    Serial.print("Dang ket noi WiFi: ");
    Serial.println(WIFI_SSID);

    // ESP32 hoạt động như một thiết bị kết nối vào router/hotspot.
    WiFi.mode(WIFI_STA);

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );

    // Chờ kết nối.
    while (
        WiFi.status() !=
        WL_CONNECTED
    )
    {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("Da ket noi WiFi.");

    Serial.print("Dia chi IP: ");
    Serial.println(
        WiFi.localIP()
    );

    Serial.println("========================================");
}


// ============================================================================
// HÀM THỬ KẾT NỐI LẠI WIFI
// ============================================================================

void reconnectWiFiIfNeeded(
    unsigned long currentTime
)
{
    // Nếu Wi-Fi vẫn còn kết nối thì không làm gì.
    if (
        WiFi.status() ==
        WL_CONNECTED
    )
    {
        return;
    }

    // Không reconnect liên tục.
    if (
        currentTime -
        lastWiFiReconnectTime <
        WIFI_RECONNECT_INTERVAL_MS
    )
    {
        return;
    }

    lastWiFiReconnectTime =
        currentTime;

    Serial.println(
        "Mat WiFi. Dang thu ket noi lai..."
    );

    WiFi.disconnect();

    WiFi.begin(
        WIFI_SSID,
        WIFI_PASSWORD
    );
}


// ============================================================================
// SETUP
// ============================================================================

void setup()
{
    // ------------------------------------------------------------------------
    // SERIAL MONITOR
    // ------------------------------------------------------------------------

    Serial.begin(
        115200
    );

    delay(
        500
    );


    // ------------------------------------------------------------------------
    // ADC
    // ------------------------------------------------------------------------

    pinMode(
        IR_PIN,
        INPUT
    );

    pinMode(
        GAS_AO_PIN,
        INPUT
    );

    pinMode(
        GAS_DO_PIN,
        INPUT
    );

    // ADC 12 bit => giá trị từ 0 đến 4095.
    analogReadResolution(
        ADC_RESOLUTION_BITS
    );

    // Cấu hình attenuation cho cả hai kênh ADC1.
    // Dù vậy, điện áp thực tế đưa vào GPIO34/GPIO35 vẫn phải nằm
    // trong giới hạn an toàn của ESP32.
    analogSetPinAttenuation(
        IR_PIN,
        ADC_11db
    );

    analogSetPinAttenuation(
        GAS_AO_PIN,
        ADC_11db
    );


    // ------------------------------------------------------------------------
    // WIFI
    // ------------------------------------------------------------------------

    connectWiFi();


    // ------------------------------------------------------------------------
    // ROUTE TRANG CHÍNH
    // ------------------------------------------------------------------------

    server.on(
        "/",
        HTTP_GET,
        [](AsyncWebServerRequest* request)
        {
            request->send_P(
                200,
                "text/html",
                PAGE
            );
        }
    );


    // ------------------------------------------------------------------------
    // KHI TRÌNH DUYỆT KẾT NỐI VÀO SSE
    // ------------------------------------------------------------------------

    events.onConnect(
        [](AsyncEventSourceClient* client)
        {
            Serial.println(
                "Web client da ket noi SSE."
            );

            // Gửi một gói để trình duyệt biết kết nối đã sẵn sàng.
            // reconnect = 1000 ms: nếu mất kết nối, browser thử reconnect sau 1 s.
            client->send(
                "connected",
                NULL,
                millis(),
                1000
            );
        }
    );


    // Gắn SSE handler vào Web Server.
    server.addHandler(
        &events
    );


    // ------------------------------------------------------------------------
    // KHỞI ĐỘNG WEB SERVER
    // ------------------------------------------------------------------------

    server.begin();

    Serial.println(
        "Web Server da khoi dong."
    );

    Serial.print(
        "Mo trinh duyet tai: http://"
    );

    Serial.println(
        WiFi.localIP()
    );
}


// ============================================================================
// LOOP
// ============================================================================

void loop()
{
    const unsigned long currentTime =
        millis();


    // ------------------------------------------------------------------------
    // KIỂM TRA / KẾT NỐI LẠI WIFI
    // ------------------------------------------------------------------------

    reconnectWiFiIfNeeded(
        currentTime
    );


    // Nếu chưa có Wi-Fi thì không gửi dữ liệu web.
    if (
        WiFi.status() !=
        WL_CONNECTED
    )
    {
        return;
    }


    // ------------------------------------------------------------------------
    // KIỂM TRA ĐÃ ĐẾN CHU KỲ LẤY MẪU CHƯA
    // ------------------------------------------------------------------------

    if (
        currentTime -
        lastSampleTime <
        SAMPLE_INTERVAL_MS
    )
    {
        return;
    }

    lastSampleTime =
        currentTime;


    // ------------------------------------------------------------------------
    // ĐỌC HAI KÊNH ADC
    // ------------------------------------------------------------------------

    // Tín hiệu photodiode hồng ngoại.
    const int irValue =
        analogRead(
            IR_PIN
        );

    // Tín hiệu Analog Output (AO) raw từ cảm biến MP-2/MQ-2.
    const int gasAnalogValue =
        analogRead(
            GAS_AO_PIN
        );

    // Tín hiệu Digital Output (DO) từ comparator trên module.
    // Kết quả chỉ là LOW (0) hoặc HIGH (1).
    const int gasDigitalValue =
        digitalRead(
            GAS_DO_PIN
        );


    // ------------------------------------------------------------------------
    // TẠO GÓI JSON
    //
    // Ví dụ:
    // {"ir":1520,"gasAO":875,"gasDO":1,"time":123456}
    //
    // ir    : RAW ADC photodiode GPIO34, 0..4095
    // gasAO : RAW ADC chân AO GPIO35, 0..4095
    // gasDO : RAW digital chân DO GPIO32, 0 hoặc 1
    // time  : millis() của ESP32, đơn vị ms
    // ------------------------------------------------------------------------

    char json[96];

    snprintf(
        json,
        sizeof(json),
        "{\"ir\":%d,\"gasAO\":%d,\"gasDO\":%d,\"time\":%lu}",
        irValue,
        gasAnalogValue,
        gasDigitalValue,
        currentTime
    );


    // ------------------------------------------------------------------------
    // STREAM GÓI DỮ LIỆU TỚI TẤT CẢ TRÌNH DUYỆT ĐANG KẾT NỐI
    // ------------------------------------------------------------------------

    events.send(
        json,
        "adc",
        currentTime
    );


    // ------------------------------------------------------------------------
    // IN GIÁ TRỊ RA SERIAL MONITOR
    // ------------------------------------------------------------------------

    if (
        currentTime -
        lastSerialPrintTime >=
        SERIAL_PRINT_INTERVAL_MS
    )
    {
        lastSerialPrintTime =
            currentTime;

        Serial.print(
            "IR ADC = "
        );

        Serial.print(
            irValue
        );

        Serial.print(
            " | GAS AO = "
        );

        Serial.print(
            gasAnalogValue
        );

        Serial.print(
            " | GAS DO = "
        );

        Serial.println(
            gasDigitalValue
        );
    }

}