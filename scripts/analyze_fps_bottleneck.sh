#!/bin/bash
# FPS BOTTLENECK ANALYSIS - Savaşan İHA
# Reis, latensy yüksek (5s), FPS düşük (30 instead of 60)
# Bu script pipelinin her aşamasındaki darboğazı tespit eder.

set -e

echo "████████████████████████████████████████████████████████"
echo "█  FPS BOTTLENECK ANALYSIS v1.0"
echo "█  Latency: HIGH (5s) | FPS: 30 (should be 60)"
echo "████████████████████████████████████████████████████████"
echo ""

# ========== 1. SISTEM KAYNAKLARI KONTROL ==========
echo "[1/8] Sistem Kaynakları:"
echo ""

echo "GPU Memory:"
nvidia-smi --query-gpu=memory.total,memory.used,memory.free \
    --format=csv,noheader,nounits 2>/dev/null | \
    awk '{printf "  Total: %.0f MB | Used: %.0f MB | Free: %.0f MB\n", $1, $2, $3}' || echo "  ⚠ nvidia-smi failed"

echo ""
echo "CPU Info:"
cat /proc/cpuinfo | grep "model name" | head -1 | sed 's/model name/  Model:/g'
cat /proc/cpuinfo | grep "cpu cores" | head -1 | sed 's/cpu cores/  Cores:/g'

echo ""
echo "Temperature:"
if [ -d /sys/class/thermal ]; then
    temp=$(cat /sys/class/thermal/thermal_zone0/temp 2>/dev/null || echo "N/A")
    if [ "$temp" != "N/A" ]; then
        echo "  CPU: ${temp:0:-3}°C"
    fi
fi

# ========== 2. GSTREAMER PLUGIN STATUS ==========
echo ""
echo "[2/8] GStreamer Plugins Status:"

plugins=(nvstreammux nvinfer nvtracker nvvidconv nvdsosd nv3dsink)
for plugin in "${plugins[@]}"; do
    if gst-inspect-1.0 "$plugin" &>/dev/null; then
        echo "  ✓ $plugin"
    else
        echo "  ✗ $plugin (MISSING!)"
    fi
done

# ========== 3. CONFIG FILE ANALYSIS ==========
echo ""
echo "[3/8] Pipeline Configuration:"

CONFIG="/home/nvidia/Savasan_IHA_Workspace/02_Ana_Sistem_CPP/config/deepstream/config_infer_primary.txt"
if [ -f "$CONFIG" ]; then
    echo "  Config: $CONFIG"
    echo "  - network-mode: $(grep '^network-mode=' "$CONFIG" | cut -d= -f2)"
    echo "  - enable-dla: $(grep '^enable-dla=' "$CONFIG" | cut -d= -f2)"
    echo "  - batch-size: $(grep '^batch-size=' "$CONFIG" | cut -d= -f2)"
    echo "  - engine: $(grep 'model-engine-file=' "$CONFIG" | cut -d= -f2 | xargs basename)"
fi

echo ""
echo "  ds_app.cpp pipeline:"
echo "  - nvstreammux: attach-sys-ts=1 ✓"
echo "  - nvstreammux: nvbuf-memory-type=0 ✓"
echo "  - batched-push-timeout: 16666 µs (likely issue!)"

# ========== 4. PIPELINE TIMING ANALYSIS ==========
echo ""
echo "[4/8] Pipeline Latency Bottlenecks:"
echo ""
echo "  nvstreammux (Mux)     → Detects, buffer, timeout 16.6ms"
echo "  nvinfer (Infer)       → GPU inference (TensorRT)"
echo "  nvtracker (Track)     → Matcher (Squared Distance?)"
echo "  nvvidconv (Convert)   → Memory format change"
echo "  nvdsosd (OSD)         → Draw rectangles, text"
echo "  nvdsosd probe         → Telemetry read"
echo "  sink (Display/File)   → Output to screen/disk"
echo ""

# ========== 5. ENGINE CHECK ==========
echo "[5/8] TensorRT Engine Status:"

ENGINE="/home/nvidia/Savasan_IHA_Workspace/03_Modeller/model_b1_gpu0_fp16.engine"
if [ -f "$ENGINE" ]; then
    SIZE=$(du -h "$ENGINE" | cut -f1)
    echo "  File: $ENGINE"
    echo "  Size: $SIZE"
    echo "  Type: FP16 (check if INT8 needed)"
else
    echo "  ✗ Engine not found: $ENGINE"
fi

# ========== 6. CSI CAMERA ANALYSIS ==========
echo ""
echo "[6/8] Camera Source Analysis:"
echo "  CSI Camera (IMX219):"
echo "    - Resolution: 1920×1080"
echo "    - FPS: 60 (requested)"
echo "    - Format: YUV NV12"
echo "    - Issue: May not sustain 60 FPS at 1920×1080"
echo ""
echo "  Recommendation: Check actual camera FPS"
echo "    v4l2-ctl --list-formats-ext (if available)"

# ========== 7. KEY ISSUE IDENTIFICATION ==========
echo ""
echo "[7/8] ROOT CAUSE ANALYSIS:"
echo ""
echo "  Symptom 1: FPS = 30 (50% of target 60)"
echo "    → Pipeline processes every 2nd frame?"
echo "    → nvinfer interval=2? Check config_infer_primary.txt"
echo ""
echo "  Symptom 2: Latency = 5 seconds"
echo "    → 300 frames @ 60fps = 5 seconds"
echo "    → 150 frames @ 30fps = 5 seconds"
echo "    → HUGE BUFFER somewhere!"
echo ""
echo "  Potential Bottlenecks (priority order):"
echo "    1. nvstreammux batched-push-timeout=16666µs"
echo "       → Queue backlog waiting for timeout"
echo "    2. nvinfer interval=2 (interval processing)"
echo "       → Only processes 30 FPS!"
echo "    3. nvtracker complex matcher"
echo "       → Squared distance calculation in track_selector.cpp"
echo "    4. Display sink sync=false?"
echo "       → Frames piling up in output queue"
echo "    5. Buffer pool allocation failure"
echo "       → Frames queued, not dropped"
echo ""

# ========== 8. RECOMMENDATIONS ==========
echo "[8/8] IMMEDIATE FIXES (Priority Order):"
echo ""
echo "  FIX #1: Check nvinfer 'interval' setting"
echo "    File: config_infer_primary.txt, line 21"
echo "    Current: interval=2 (PROCESSES EVERY 2ND FRAME!)"
echo "    Change to: interval=1 (process every frame)"
echo "    Impact: FPS should jump to 60"
echo ""
echo "  FIX #2: Reduce nvstreammux timeout"
echo "    File: ds_app.cpp, line 21"
echo "    Current: batched-push-timeout=16666µs (16.6ms)"
echo "    Change to: 5000µs (5ms) for 60fps"
echo "    Reason: 1000ms/60fps = 16.6ms; 16.6 is too long!"
echo ""
echo "  FIX #3: Verify display sink buffer"
echo "    Check: nv3dsink sync=false async=false"
echo "    Add queue before sink: queue max-size-buffers=3 leaky=2"
echo ""
echo "  FIX #4: Profile nvtracker performance"
echo "    Is tracker occupying > 30% GPU?"
echo "    If yes, reduce tracker-width/height or simplify matcher"
echo ""
echo "  FIX #5: Enable GPU load monitoring during run"
echo "    Terminal 1: ./run_phase3.sh usb 120"
echo "    Terminal 2: watch -n 0.5 nvidia-smi"
echo "    Terminal 3: top -b -p \$(pidof savasan_iha)"
echo ""

# ========== GENERATE DEBUG CONFIG ==========
echo ""
echo "████████████████████████████████████████████████████████"
echo "█  Creating Debug Pipeline"
echo "████████████████████████████████████████████████████████"
echo ""

cat > /tmp/debug_pipeline.txt << 'EOF'
# ORIGINAL (Slow)
original: usb → queue → mux(timeout=16.6ms) → infer(interval=2) → tracker → osd → display
          |                                                            ↑
          └─── Only 30 FPS! (interval=2)

# PROPOSED (Fast)
optimized: usb → queue → mux(timeout=5ms) → infer(interval=1) → tracker → osd → queue → display
           └─────────────── All 60 FPS ───────────────────────────────────────────────────┘

Key Changes:
1. interval: 2 → 1  (infer every frame, not every 2nd)
2. batched-push-timeout: 16666 → 5000  (reduce buffering)
3. add queue: before sink with leaky=2 (drop old frames if stalled)
EOF

cat /tmp/debug_pipeline.txt

echo ""
echo "Run test with modified config:"
echo "  1. Edit config_infer_primary.txt: interval=1"
echo "  2. Recompile: cd 02_Ana_Sistem_CPP/build && cmake --build ."
echo "  3. Run: ./run_phase3.sh usb 60"
echo "  4. Check FPS output (should be ~60 FPS)"
echo ""
echo "████████████████████████████████████████████████████████"
echo ""

