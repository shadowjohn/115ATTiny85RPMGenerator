# 驗證紀錄（2026-10-15）

## 基準與修正

基準：`main` commit `4988eff80007ecd15078d7ac1ee49bee217a5416`。原始工作目錄乾淨；修正使用獨立本機分支 `fix/timer-vr-response`。

本機核心為 damellis attiny 1.0.2 + Arduino AVR 1.8.6，FQBN `attiny:avr:ATtinyX5:cpu=attiny85,clock=internal16`；時鐘由使用者確認。TM1637 使用官方 1.2.0，壓縮包 SHA256：`ece036479f5d983d1656ff0a1b1d750a1289e231db6dfec8939473185bf80eb1`。函式庫只下載至本次任務工作區，沒有安裝到全域 sketchbook。

實際 AVR 編譯：基準 Flash 3210 bytes / SRAM 123 bytes；第一輪修正為 2612 / 125 bytes；使用者同意固定 ×10 RPM 後，再直接編譯 `D:\mytools\115ATTiny85RPMGenerator`，最新 Flash **2602 bytes / SRAM 121 bytes**（上限 8192 / 512）。編譯成功；各次都有 Arduino 核心 Tone.cpp、WInterrupts.c、new.cpp 的既有警告，修正 sketch 沒有 AVR 編譯警告。最新來源 SHA256 為 `1de33d44802c8d6c2fd9e3a23e3c0f75b16c75e8e57954e58401375009eec095`，與測試記錄及 `artifacts/build-evidence.json` 相符。

## 回歸測試

`tests/run.ps1` 使用 MSVC 編譯實際 `.ino`，以外設替身模擬 20.8 ms TM1637 傳輸及 Timer1。

基準先重現失敗：VR 全幅變動 150 ms 後只到 3000 RPM；500 RPM 強制顯示傳輸跨越邊緣時，週期最大誤差 10804 µs；14000 RPM 最大誤差 19990.286 µs。

修正後 15 / 15 測試通過：

- VR 從已穩定的 0 轉到滿幅，50 ms 時觀察到 13500 RPM 的要求值；濾波器四筆樣本後達 ADC 959 / 1023。
- 顯示阻塞期間 500 RPM 週期最大誤差 0 µs、14000 RPM 1.714 µs。
- 500～14000 RPM 的 28 個階梯，理想時鐘下週期／HIGH／LOW 最大誤差 1.714 / 2.609 / 2.000 µs。
- 0 RPM、負值停止、重新啟動完整 LOW、同轉速重複呼叫、升降速的完整週期交界、VR 單調性、±3 ADC 邊界雜訊、millis 溢位、Timer0／PLL 設定保留與四位顯示均通過。
- 使用者同意固定 ×10 RPM 後，顯示測試改用獨立固定預期值：0→0、500→50、9500→950、10000→1000、10500→1050、14000→1400。先確認舊預設除數 1 會失敗，再以除數 10 重跑；測試同時確認原轉速要求未被縮小，28 階梯仍檢查原 RPM / 60 脈波。

這些微秒數值是 **host 理想排程模擬**，不代表實體波形公差。硬體模型與限制請見 `tests/README.md`。

## 反組譯審查

實際編譯 ELF 中 Timer1 COMPA 為 `__vector_3`，Timer0 overflow 為 `__vector_5`。手動按 AVR 指令週期估算：Timer1 最慢上升沿路徑約 156 cycles（9.75 µs），Timer0 最慢路徑約 102 cycles（6.4 µs）。正常運轉提交參數的原子區塊小於 1 µs；兩個中斷相撞的保守估算小於 20 µs，低於最短 256 µs 排程窗。此為靜態估算，沒有新增其他中斷來源；不可當作實體抖動驗收。

## 產物與可重跑方式

本次任務工作區：`C:\Users\johnho\Documents\Codex\2026-10-15\task`。

- `build.ps1`：純編譯，所有編譯產物與 cache 留在工作區。
- `arduino-core-evidence.md`：本機核心／設定來源與限制。
- `build/compile.log`：修正編譯完整紀錄。
- `artifacts/`：修正 HEX、ELF 及含來源 SHA256 的 `build-evidence.json`。
- `build-baseline/`、`artifacts-baseline/`、`baseline-sketch/`：基準紀錄與原始快照。

重跑 host 測試：在專案執行 `powershell -ExecutionPolicy Bypass -File tests/run.ps1`（需要本機 MSVC）。重跑 firmware 編譯：在任務工作區執行 `./build.ps1 -SketchPath 'D:\mytools\115ATTiny85RPMGenerator'`。32-bit AVR 工具在受限沙箱會有 DLL 載入錯誤，本次在核准的擴大執行環境完成純編譯。

未 commit、未 push、未燒錄、未更動 fuse／核心全域設定。尚未接示波器或實體儀表驗收；實機測項請依 README 執行。B0505S 已由使用者回報可驅動，但全轉速範圍仍待確認。使用者已同意全範圍固定 ×10 RPM，預設除數為 10，面板單位應標示「×10 RPM」。
