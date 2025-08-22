# 📦 simple-tcp-over-udp

模擬 TCP 功能的 UDP 通訊框架，支援三次握手、算式請求、檔案傳輸與 ACK 回應。
適合用於學習網路協定、封包設計與 client-server 架構。

---

## 🚀 功能特色

- ✅ **三次握手**：模擬 TCP 的 SYN → SYN-ACK → ACK 流程，建立可靠連線
- 🧮 **算式處理**：client 傳送算式字串，server 回傳計算結果
- 📁 **檔案傳輸**：client 請求檔案，server 分段傳送並支援 ACK 回報
- 📦 **封包序列化**：自訂封包格式，支援序列號、確認號、視窗大小等欄位
- 🧠 **狀態管理**：server 追蹤每個 client 的連線狀態與握手進度