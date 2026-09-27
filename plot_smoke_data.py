#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Vẽ và so sánh tín hiệu Photodiode IR với cảm biến MP-2/MQ-2 từ file CSV
được xuất bởi giao diện ESP32 Smoke Sensor Logger.

CSV mong đợi có các cột:
    sample,time_s,time_from_trigger_s,IR_raw,MP2_AO_raw,MP2_DO

Kết quả mặc định:
    Tạo thư mục cùng tên với file CSV đầu vào (bỏ phần mở rộng .csv),
    sau đó lưu:
        comparison_raw.png
        comparison_normalized.png
        comparison_metrics.txt

Cách dùng:
    python plot_smoke_data.py smoke_log_12_2026-09-26_20-35-10.csv

Ví dụ trên sẽ tạo:
    smoke_log_12_2026-09-26_20-35-10/
        comparison_raw.png
        comparison_normalized.png
        comparison_metrics.txt

Có thể chọn thư mục gốc để xuất:
    python plot_smoke_data.py data.csv --outdir ket_qua

Khi đó kết quả sẽ nằm trong:
    ket_qua/data/

Muốn vừa lưu ảnh vừa mở cửa sổ đồ thị:
    python plot_smoke_data.py data.csv --show

Nếu không truyền tên file, chương trình sẽ thử mở hộp thoại chọn CSV.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt


REQUIRED_COLUMNS = [
    "sample",
    "time_s",
    "time_from_trigger_s",
    "IR_raw",
    "MP2_AO_raw",
    "MP2_DO",
]


def choose_csv_file() -> Path | None:
    """Mở hộp thoại chọn file nếu người dùng không truyền đường dẫn."""
    try:
        import tkinter as tk
        from tkinter import filedialog

        root = tk.Tk()
        root.withdraw()
        root.update()
        filename = filedialog.askopenfilename(
            title="Chọn file CSV log ESP32",
            filetypes=[("CSV files", "*.csv"), ("All files", "*.*")],
        )
        root.destroy()
        return Path(filename) if filename else None
    except Exception:
        return None


def load_log_csv(path: Path) -> pd.DataFrame:
    """Đọc CSV và làm sạch các cột số."""
    df = pd.read_csv(path, encoding="utf-8-sig")

    missing = [c for c in REQUIRED_COLUMNS if c not in df.columns]
    if missing:
        raise ValueError(
            "File CSV thiếu cột bắt buộc: " + ", ".join(missing)
        )

    df = df[REQUIRED_COLUMNS].copy()

    for column in REQUIRED_COLUMNS:
        df[column] = pd.to_numeric(df[column], errors="coerce")

    df = df.dropna(subset=[
        "time_from_trigger_s",
        "IR_raw",
        "MP2_AO_raw",
        "MP2_DO",
    ])

    if df.empty:
        raise ValueError("CSV không còn mẫu hợp lệ sau khi làm sạch dữ liệu.")

    df = df.sort_values("time_from_trigger_s").reset_index(drop=True)
    return df


def baseline_and_normalized_response(
    values: np.ndarray,
    relative_time: np.ndarray,
) -> tuple[np.ndarray, float, int, float]:
    """
    Chuẩn hóa đáp ứng về khoảng 0..1 theo baseline trước trigger.

    - Baseline: median của các mẫu t < 0.
    - Nếu không có mẫu t < 0: dùng median 10% mẫu đầu.
    - Tự xác định chiều đáp ứng tăng hay giảm sau trigger.
    - normalized = 0 ở baseline, 1 tại độ lệch cực đại sau trigger.

    Trả về:
        normalized, baseline, polarity, response_peak
    polarity = +1 nếu tín hiệu đáp ứng theo chiều tăng, -1 nếu theo chiều giảm.
    """
    values = np.asarray(values, dtype=float)
    relative_time = np.asarray(relative_time, dtype=float)

    pre_mask = relative_time < 0
    if np.any(pre_mask):
        baseline = float(np.median(values[pre_mask]))
    else:
        n = max(3, int(round(len(values) * 0.10)))
        n = min(n, len(values))
        baseline = float(np.median(values[:n]))

    post_mask = relative_time >= 0
    post_values = values[post_mask] if np.any(post_mask) else values

    positive_excursion = float(np.max(post_values - baseline))
    negative_excursion = float(np.max(baseline - post_values))
    polarity = 1 if positive_excursion >= negative_excursion else -1

    response = polarity * (values - baseline)
    post_response = response[post_mask] if np.any(post_mask) else response
    response_peak = float(np.max(post_response)) if len(post_response) else 0.0

    if not np.isfinite(response_peak) or response_peak <= 0:
        normalized = np.zeros_like(values, dtype=float)
        response_peak = 0.0
    else:
        normalized = response / response_peak
        # Giữ đồ thị so sánh dễ đọc: nhiễu âm dưới baseline về 0,
        # cực đại được giới hạn ở 1.
        normalized = np.clip(normalized, 0.0, 1.0)

    return normalized, baseline, polarity, response_peak


def first_crossing_time(
    relative_time: np.ndarray,
    normalized: np.ndarray,
    threshold: float,
) -> float | None:
    """Thời điểm đầu tiên sau trigger mà đáp ứng chuẩn hóa đạt ngưỡng."""
    mask = (relative_time >= 0) & (normalized >= threshold)
    indices = np.flatnonzero(mask)
    if len(indices) == 0:
        return None
    return float(relative_time[indices[0]])


def recovery_time_after_peak(
    relative_time: np.ndarray,
    normalized: np.ndarray,
    threshold: float = 0.10,
) -> float | None:
    """
    Tìm thời điểm đầu tiên sau cực đại mà đáp ứng giảm xuống <= threshold.
    Đây là mốc phục hồi tương đối, không phải hằng số thời gian cảm biến chuẩn hóa.
    """
    post_indices = np.flatnonzero(relative_time >= 0)
    if len(post_indices) == 0:
        return None

    peak_index = post_indices[np.argmax(normalized[post_indices])]
    tail_indices = np.arange(peak_index + 1, len(normalized))
    if len(tail_indices) == 0:
        return None

    recovered = tail_indices[normalized[tail_indices] <= threshold]
    if len(recovered) == 0:
        return None

    return float(relative_time[recovered[0]])


def signal_metrics(
    name: str,
    raw: np.ndarray,
    relative_time: np.ndarray,
    normalized: np.ndarray,
    baseline: float,
    polarity: int,
) -> dict[str, float | str | None]:
    """Tính một số chỉ số để hỗ trợ so sánh IR và MP-2."""
    post_indices = np.flatnonzero(relative_time >= 0)
    if len(post_indices) == 0:
        post_indices = np.arange(len(relative_time))

    peak_local = int(np.argmax(normalized[post_indices]))
    peak_index = int(post_indices[peak_local])

    t10 = first_crossing_time(relative_time, normalized, 0.10)
    t50 = first_crossing_time(relative_time, normalized, 0.50)
    t90 = first_crossing_time(relative_time, normalized, 0.90)

    rise_10_90 = None
    if t10 is not None and t90 is not None:
        rise_10_90 = t90 - t10

    return {
        "name": name,
        "baseline": baseline,
        "direction": "tăng" if polarity > 0 else "giảm",
        "peak_raw": float(raw[peak_index]),
        "peak_time_s": float(relative_time[peak_index]),
        "t10_s": t10,
        "t50_s": t50,
        "t90_s": t90,
        "rise_10_90_s": rise_10_90,
        "recovery_10_s": recovery_time_after_peak(relative_time, normalized, 0.10),
    }


def fmt_optional(value: float | None, digits: int = 3) -> str:
    if value is None or not np.isfinite(value):
        return "không xác định trong cửa sổ log"
    return f"{value:.{digits}f} s"


def write_metrics_report(
    output_path: Path,
    df: pd.DataFrame,
    ir_metrics: dict,
    mp2_metrics: dict,
) -> None:
    """Ghi file TXT tóm tắt các chỉ số so sánh."""
    t_start = float(df["time_from_trigger_s"].iloc[0])
    t_end = float(df["time_from_trigger_s"].iloc[-1])

    onset_delay = None
    if ir_metrics["t10_s"] is not None and mp2_metrics["t10_s"] is not None:
        onset_delay = float(mp2_metrics["t10_s"] - ir_metrics["t10_s"])

    peak_delay = float(mp2_metrics["peak_time_s"] - ir_metrics["peak_time_s"])

    lines = [
        "SO SÁNH ĐÁP ỨNG IR VÀ MP-2",
        "=" * 60,
        f"Số mẫu: {len(df)}",
        f"Cửa sổ thời gian so với trigger: {t_start:.3f} s -> {t_end:.3f} s",
        "Trigger được quy ước tại t = 0 s.",
        "",
    ]

    for metrics in (ir_metrics, mp2_metrics):
        lines.extend([
            f"[{metrics['name']}]",
            f"Baseline trước trigger: {metrics['baseline']:.3f}",
            f"Chiều đáp ứng tự nhận diện: {metrics['direction']}",
            f"Giá trị raw tại cực đại đáp ứng: {metrics['peak_raw']:.3f}",
            f"Thời điểm cực đại đáp ứng: {metrics['peak_time_s']:.3f} s",
            f"t10: {fmt_optional(metrics['t10_s'])}",
            f"t50: {fmt_optional(metrics['t50_s'])}",
            f"t90: {fmt_optional(metrics['t90_s'])}",
            f"Thời gian tăng 10%-90%: {fmt_optional(metrics['rise_10_90_s'])}",
            f"Mốc phục hồi về <=10% sau cực đại: {fmt_optional(metrics['recovery_10_s'])}",
            "",
        ])

    lines.extend([
        "[CHÊNH LỆCH MP-2 SO VỚI IR]",
        (
            "Chênh lệch thời điểm đạt 10%: "
            + (fmt_optional(onset_delay) if onset_delay is not None else "không xác định")
        ),
        f"Chênh lệch thời điểm cực đại: {peak_delay:.3f} s",
        "Giá trị dương nghĩa là MP-2 xuất hiện muộn hơn IR; giá trị âm nghĩa là MP-2 xuất hiện sớm hơn IR.",
        "",
        "Lưu ý: các mốc 10%, 50%, 90% và phục hồi được tính từ dữ liệu của chính phiên log,",
        "phù hợp cho so sánh tương đối giữa hai kênh nhưng không thay thế quy trình hiệu chuẩn cảm biến.",
    ])

    output_path.write_text("\n".join(lines), encoding="utf-8")


def plot_raw(df: pd.DataFrame, output_path: Path, show: bool) -> None:
    """Vẽ IR raw và MP-2 AO raw trên cùng một thang ADC."""
    x = df["time_from_trigger_s"].to_numpy(dtype=float)
    ir = df["IR_raw"].to_numpy(dtype=float)
    mp2 = df["MP2_AO_raw"].to_numpy(dtype=float)

    fig, ax = plt.subplots(figsize=(11, 5.8))
    ax.plot(x, ir, label="IR GPIO34")
    ax.plot(x, mp2, label="MP-2 AO GPIO35")
    ax.axvline(0.0, linestyle="--", linewidth=1.2, label="Trigger t = 0 s")

    ax.set_title("So sánh tín hiệu RAW: IR và MP-2")
    ax.set_xlabel("Thời gian so với trigger (s)")
    ax.set_ylabel("ADC raw")
    ax.set_ylim(0, 4095)
    ax.grid(True, alpha=0.25)
    ax.legend()
    fig.tight_layout()

    fig.savefig(output_path, dpi=300, bbox_inches="tight")
    if show:
        plt.show()
    plt.close(fig)


def plot_normalized(
    df: pd.DataFrame,
    ir_norm: np.ndarray,
    mp2_norm: np.ndarray,
    output_path: Path,
    show: bool,
) -> None:
    """Vẽ đáp ứng IR và MP-2 chuẩn hóa 0..1 để so sánh hình dạng/thời gian."""
    x = df["time_from_trigger_s"].to_numpy(dtype=float)

    fig, ax = plt.subplots(figsize=(11, 5.8))
    ax.plot(x, ir_norm, label="IR chuẩn hóa")
    ax.plot(x, mp2_norm, label="MP-2 AO chuẩn hóa")
    ax.axvline(0.0, linestyle="--", linewidth=1.2, label="Trigger t = 0 s")

    ax.set_title("So sánh đáp ứng chuẩn hóa: IR và MP-2")
    ax.set_xlabel("Thời gian so với trigger (s)")
    ax.set_ylabel("Đáp ứng chuẩn hóa (0–1)")
    ax.set_ylim(-0.02, 1.05)
    ax.grid(True, alpha=0.25)
    ax.legend()
    fig.tight_layout()

    fig.savefig(output_path, dpi=300, bbox_inches="tight")
    if show:
        plt.show()
    plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Vẽ so sánh IR và MP-2 từ CSV log ESP32."
    )
    parser.add_argument(
        "csv",
        nargs="?",
        help="Đường dẫn file CSV được tải từ phần log ESP32.",
    )
    parser.add_argument(
        "--outdir",
        default=None,
        help="Thư mục gốc lưu kết quả. Bên trong sẽ tạo thư mục cùng tên file CSV. Mặc định: thư mục chứa CSV.",
    )
    parser.add_argument(
        "--show",
        action="store_true",
        help="Hiển thị cửa sổ matplotlib ngoài việc lưu ảnh PNG.",
    )
    args = parser.parse_args()

    if args.csv:
        csv_path = Path(args.csv).expanduser().resolve()
    else:
        selected = choose_csv_file()
        if selected is None:
            print(
                "Không chọn được file CSV. Hãy chạy: "
                "python plot_smoke_data.py <duong_dan_file.csv>",
                file=sys.stderr,
            )
            return 2
        csv_path = selected.expanduser().resolve()

    if not csv_path.exists():
        print(f"Không tìm thấy file: {csv_path}", file=sys.stderr)
        return 2

    base_outdir = (
        Path(args.outdir).expanduser().resolve()
        if args.outdir
        else csv_path.parent
    )
    outdir = base_outdir / csv_path.stem
    outdir.mkdir(parents=True, exist_ok=True)

    try:
        df = load_log_csv(csv_path)
    except Exception as exc:
        print(f"Lỗi đọc CSV: {exc}", file=sys.stderr)
        return 1

    t = df["time_from_trigger_s"].to_numpy(dtype=float)
    ir = df["IR_raw"].to_numpy(dtype=float)
    mp2 = df["MP2_AO_raw"].to_numpy(dtype=float)

    ir_norm, ir_baseline, ir_polarity, _ = baseline_and_normalized_response(ir, t)
    mp2_norm, mp2_baseline, mp2_polarity, _ = baseline_and_normalized_response(mp2, t)

    ir_metrics = signal_metrics(
        "IR GPIO34", ir, t, ir_norm, ir_baseline, ir_polarity
    )
    mp2_metrics = signal_metrics(
        "MP-2 AO GPIO35", mp2, t, mp2_norm, mp2_baseline, mp2_polarity
    )

    raw_path = outdir / "comparison_raw.png"
    normalized_path = outdir / "comparison_normalized.png"
    metrics_path = outdir / "comparison_metrics.txt"

    plot_raw(df, raw_path, args.show)
    plot_normalized(df, ir_norm, mp2_norm, normalized_path, args.show)
    write_metrics_report(metrics_path, df, ir_metrics, mp2_metrics)

    print("Đã xử lý xong.")
    print(f"CSV nguồn: {csv_path}")
    print(f"Đồ thị RAW: {raw_path}")
    print(f"Đồ thị chuẩn hóa: {normalized_path}")
    print(f"Chỉ số so sánh: {metrics_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
