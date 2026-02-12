# ESP HUD Java SDK

用于向 ESP HUD 固件发送 `MSGF/IMGF` 帧的纯 Java SDK。

包名根路径：

`cn.crazythursdayvivo50.esp_hud`

## SDK 功能

- 支持按字段写入车身数据（如 `setSpeedKmh`、`setEngineRpm`）。
- 线程安全地暂存最新状态。
- 按固定调度发送 `MSGF` 快照帧（默认 `24Hz`）。
- 接收 GPS 输入（`pushGpsPoint`）并进行去重/过滤。
- 通过可插拔 `MapImageProvider` 拉取轨迹图片。
- 通过可插拔 `HudTransport` 发送 `IMGF` 图像帧。

## Android 集成说明

该 SDK 不依赖 Android API，可直接嵌入 Android 项目。

在 Android 工程中：

1. 实现 `HudTransport`（底层可接 USB/BLE/Wi-Fi）。
2. 地图模式可直接使用 `MapImageProvider.defaultProvider()`，也可自行实现 `MapImageProvider`。
3. 将 CAN 转换后的采集回调喂给 `setXxx(...)` 接口。
4. 将定位回调喂给 `pushGpsPoint(...)` 接口。

## 最小使用示例

```java
HudTransport transport = new MyUsbTransport();
MapImageProvider mapProvider = MapImageProvider.defaultProvider();

HudSdkConfig config = HudSdkConfig.newBuilder().build();
HudHostSdk sdk = new HudHostSdk(transport, mapProvider, config);
sdk.start();

sdk.setSpeedKmh(80);
sdk.setEngineRpm(2200);
sdk.pushGpsPoint(31.2304, 121.4737, System.currentTimeMillis());

// ...
sdk.stop();
sdk.close();
```

## 公开接口（集成方必读）

只列出让 ESP HUD 正常刷新所需的公开接口。  
内部实现细节与非必需能力不在本节展开。

### 1) 必须实现的输出接口

#### `HudTransport`

- `void write(byte[] data)`
  - 参数: `data` 已编码完成的整帧字节（含 20 字节协议头）。
  - 返回: 无。
  - 说明: 负责把帧写到底层链路（USB/BLE/Wi-Fi 等）。
- `void flush()`
  - 参数: 无。
  - 返回: 无。
  - 说明: 刷新底层发送缓冲区。
- `void close()`
  - 参数: 无。
  - 返回: 无。
  - 说明: 关闭通道并释放资源。

以上方法均允许抛出 `IOException`。

### 2) 必须使用的 SDK 入口

#### `HudHostSdk(HudTransport transport, MapImageProvider mapImageProvider, HudSdkConfig config)`

- `transport`: 必填，`HudTransport` 实现。
- `mapImageProvider`: 选填，不需要地图图层可传 `null`。
- `config`: 选填，传 `null` 使用默认配置。
- 返回: `HudHostSdk` 实例。

#### 生命周期

- `void start()`
  - 参数: 无。
  - 返回: 无。
  - 说明: 启动发送线程与调度器，之后才会真正下发数据。
- `void stop()`
  - 参数: 无。
  - 返回: 无。
  - 说明: 停止调度与发送。
- `void close()`
  - 参数: 无。
  - 返回: 无。
  - 说明: 停止 SDK 并关闭传输层资源。

### 3) 车身数据输入接口（MSGF 刷新必需）

两种用法二选一或混用：

- 字段级写入（回调来一项写一项）：
  - `setSpeedKmh(int value)`
  - `setEngineRpm(int value)`
  - `setOdoMeters(int value)`
  - `setTripOdoMeters(int value)`
  - `setOutsideTempDeciC(int value)`
  - `setInsideTempDeciC(int value)`
  - `setBatteryMv(int value)`
  - `setCurrentTimeMinutes(int value)`
  - `setTripTimeMinutes(int value)`
  - `setFuelLeftDeciL(int value)`
  - `setFuelTotalDeciL(int value)`
  - 参数: 对应字段最新值。
  - 返回: 无。
- 快照写入（回调一次给全量时推荐）：
  - `updateSnapshot(VehicleSnapshot snapshot)`
  - 参数: `snapshot` 全量车身快照，传 `null` 会被忽略。
  - 返回: 无。

### 4) 地图刷新接口（需要地图时必需）

如果需要 HUD 地图层刷新，需同时接入 GPS 输入和地图提供器。

#### GPS 输入

- `pushGpsPoint(double latitude, double longitude, long timestampMs)`
  - 参数: 纬度、经度、毫秒时间戳。
  - 返回: 无。
- `pushGpsPoint(GpsPoint point)`
  - 参数: `GpsPoint`（可包含精度/速度/航向）。
  - 返回: 无。

#### 地图图片提供器

`MapImageProvider`（需要地图时必须实现，或使用默认实现）：

- `byte[] fetchTrackImage(List<GpsPoint> points)`
  - 参数: 去重过滤后的轨迹点列表（时间升序，至少两个点）。
  - 返回: PNG 字节数组（非空）。
  - 异常: 请求或渲染失败可抛异常，SDK 会按退避策略重试。

默认实现（对齐 Python 示例）：

- `MapImageProvider.defaultProvider()`
  - 请求地址：`https://azurehk.crazythursdayvivo50.cn/trace/track/image`
  - 请求方法：`POST`
  - 请求头：`Content-Type: application/json`，`accept: application/json`，可选 `Authorization: Basic ...`
  - 请求体：`{"points":[[lon,lat], ...]}`
  - 默认超时：10 秒
  - 默认最大 PNG 大小：200KB

若服务开启 Basic Auth，可使用：

- `MapImageProvider.httpProviderWithBasicAuth(url, timeoutMs, maxPngBytes, username, password)`

### 5) 可选但非“显示必需”的公开接口

- `sendPng(byte[] pngBytes)`：手动直接下发 PNG（绕过 `MapImageProvider`）。
- `sendReboot()`：发送下位机重启命令。
- `setListener(HudSdkListener listener)`：接收运行事件回调。
- `HudStats getStats()`：读取统计信息。
