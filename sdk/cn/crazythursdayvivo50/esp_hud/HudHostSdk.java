package cn.crazythursdayvivo50.esp_hud;

import java.io.IOException;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.Executors;
import java.util.concurrent.PriorityBlockingQueue;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

/**
 * ESP HUD 上位机核心 SDK。
 * <p>
 * 负责接收车身数据和 GPS 数据，按策略编码为 MSGF/IMGF 帧并通过 {@link HudTransport} 发送。
 * 该类线程安全，可在多线程环境下调用公开写入接口。
 */
public final class HudHostSdk implements AutoCloseable {
    private static final double EARTH_RADIUS_M = 6371000.0;
    private static final double DEG_TO_RAD = Math.PI / 180.0;
    private static final int RDP_BISECTION_STEPS = 12;
    private static final int PRIORITY_CONTROL = 0;
    private static final int PRIORITY_MSG = 1;
    private static final int PRIORITY_IMG = 2;
    private static final int CMD_REBOOT = 0x01;
    private static final int CMD_BRIGHTNESS = 0x02;
    private static final int CMD_OFFSET_ROTATION = 0x03;

    private final HudTransport transport;
    private final MapImageProvider mapImageProvider;
    private final HudSdkConfig config;
    private volatile HudSdkListener listener;

    private final VehicleStateStore stateStore = new VehicleStateStore();
    private ScheduledExecutorService scheduler = Executors.newScheduledThreadPool(2);
    private final BlockingQueue<OutboundFrame> sendQueue = new PriorityBlockingQueue<OutboundFrame>();

    private final AtomicInteger seq = new AtomicInteger(1);
    private final AtomicLong queueOrder = new AtomicLong(0);

    private final AtomicLong sentMsg = new AtomicLong(0);
    private final AtomicLong sentImg = new AtomicLong(0);
    private final AtomicLong sentCmd = new AtomicLong(0);
    private final AtomicLong dropped = new AtomicLong(0);
    private final AtomicLong errors = new AtomicLong(0);

    private final Object gpsLock = new Object();
    private final ArrayDeque<GpsPoint> track = new ArrayDeque<GpsPoint>();
    private enum MapFetchTriggerReason {
        NORMAL,
        INITIAL,
        PERIODIC
    }
    private enum MapFrameKind {
        NONE,
        INITIAL,
        PERIODIC
    }

    /**
     * 显示 offset_rotation 预设。
     * <p>
     * 对应固件协议允许值：1 / 3 / 5 / 7。
     */
    public enum DisplayOffsetRotation {
        /** 协议值 1。 */
        ROT_1(1),
        /** 协议值 3。 */
        ROT_3(3),
        /** 协议值 5。 */
        ROT_5(5),
        /** 协议值 7。 */
        ROT_7(7);

        final int value;

        DisplayOffsetRotation(int value) {
            this.value = value;
        }
    }
    private GpsPoint lastAcceptedPoint;
    private long lastGpsIngestMs;
    private long lastMapFetchMs;
    private long lastPeriodicMapTriggerMs;
    private int acceptedSinceLastMap;
    private double distanceSinceLastMapM;
    private boolean mapFetchInFlight;
    private boolean mapFetchPending;
    private boolean initialMapFrameTriggered;
    private MapFetchTriggerReason retryReason = MapFetchTriggerReason.NORMAL;
    private boolean mapRetryScheduled;
    private long nextMapRetryAtMs;
    private long currentBackoffMs;

    private volatile boolean running;
    private volatile boolean writerRunning;
    private Thread writerThread;
    private long lastMsgSentMs;

    /**
     * 构造 SDK 实例。
     *
     * @param transport 发送通道实现，不能为空
     * @param mapImageProvider 地图图像提供器，可为 {@code null}（此时仅发送 MSGF，不触发地图请求）
     * @param config SDK 配置，可为 {@code null}（将使用默认配置）
     * @throws IllegalArgumentException 当 {@code transport} 为空时抛出
     */
    public HudHostSdk(HudTransport transport, MapImageProvider mapImageProvider, HudSdkConfig config) {
        if (transport == null) {
            throw new IllegalArgumentException("transport must not be null");
        }
        this.transport = transport;
        this.mapImageProvider = mapImageProvider;
        this.config = (config != null) ? config : HudSdkConfig.newBuilder().build();
        this.currentBackoffMs = this.config.mapRetryBackoffInitialMs;
    }

    /**
     * 启动 SDK。
     * <p>
     * 启动后会拉起发送线程和 MSG 调度任务；重复调用安全。
     */
    public synchronized void start() {
        if (running) {
            return;
        }
        if (scheduler.isShutdown() || scheduler.isTerminated()) {
            scheduler = Executors.newScheduledThreadPool(2);
        }
        running = true;
        writerRunning = true;
        synchronized (gpsLock) {
            track.clear();
            lastAcceptedPoint = null;
            lastGpsIngestMs = 0;
            lastMapFetchMs = 0;
            acceptedSinceLastMap = 0;
            distanceSinceLastMapM = 0;
            initialMapFrameTriggered = false;
            lastPeriodicMapTriggerMs = 0;
            mapFetchPending = false;
            mapFetchInFlight = false;
            mapRetryScheduled = false;
            retryReason = MapFetchTriggerReason.NORMAL;
            nextMapRetryAtMs = 0;
            currentBackoffMs = config.mapRetryBackoffInitialMs;
        }
        startWriterThread();
        long periodMs = Math.max(1L, 1000L / Math.max(1, config.msgRateHz));
        scheduler.scheduleAtFixedRate(new Runnable() {
            @Override
            public void run() {
                safeMsgTick();
            }
        }, 0, periodMs, TimeUnit.MILLISECONDS);
    }

    /**
     * 停止 SDK。
     * <p>
     * 停止后不再进行定时发送与地图调度；重复调用安全。
     */
    public synchronized void stop() {
        if (!running) {
            return;
        }
        running = false;
        if (!scheduler.isShutdown()) {
            scheduler.shutdownNow();
        }
        writerRunning = false;
        if (writerThread != null) {
            writerThread.interrupt();
            try {
                writerThread.join(1000);
            } catch (InterruptedException ignored) {
                Thread.currentThread().interrupt();
            }
            writerThread = null;
        }
    }

    /**
     * 关闭 SDK 并释放传输资源。
     * <p>
     * 该方法会先调用 {@link #stop()}，再调用 {@link HudTransport#close()}。
     */
    @Override
    public synchronized void close() {
        stop();
        try {
            transport.close();
        } catch (IOException e) {
            emitError("transport.close", e);
        }
    }

    /**
     * 设置事件监听器。
     *
     * @param listener 监听器实例，传 {@code null} 表示移除监听器
     */
    public void setListener(HudSdkListener listener) {
        this.listener = listener;
    }

    /**
     * 获取统计信息快照。
     *
     * @return 当前统计对象
     */
    public HudStats getStats() {
        return new HudStats(
                sentMsg.get(),
                sentImg.get(),
                sentCmd.get(),
                dropped.get(),
                errors.get(),
                sendQueue.size());
    }

    /**
     * 设置车速。
     *
     * @param value 车速，单位 km/h
     */
    public void setSpeedKmh(int value) {
        stateStore.setSpeedKmh(value);
        maybeBurstMsg();
    }

    /**
     * 设置发动机转速。
     *
     * @param value 转速，单位 rpm
     */
    public void setEngineRpm(int value) {
        stateStore.setEngineRpm(value);
        maybeBurstMsg();
    }

    /**
     * 设置总里程。
     *
     * @param value 总里程，单位 m
     */
    public void setOdoMeters(int value) {
        stateStore.setOdoMeters(value);
        maybeBurstMsg();
    }

    /**
     * 设置小计里程。
     *
     * @param value 小计里程，单位 m
     */
    public void setTripOdoMeters(int value) {
        stateStore.setTripOdoMeters(value);
        maybeBurstMsg();
    }

    /**
     * 设置外部温度。
     *
     * @param value 外部温度，单位 0.1 摄氏度
     */
    public void setOutsideTempDeciC(int value) {
        stateStore.setOutsideTempDeciC(value);
        maybeBurstMsg();
    }

    /**
     * 设置内部温度。
     *
     * @param value 内部温度，单位 0.1 摄氏度
     */
    public void setInsideTempDeciC(int value) {
        stateStore.setInsideTempDeciC(value);
        maybeBurstMsg();
    }

    /**
     * 设置电池电压。
     *
     * @param value 电压，单位 mV
     */
    public void setBatteryMv(int value) {
        stateStore.setBatteryMv(value);
        maybeBurstMsg();
    }

    /**
     * 设置当前时间分钟值。
     *
     * @param value 分钟值，建议范围 0..1439
     */
    public void setCurrentTimeMinutes(int value) {
        stateStore.setCurrentTimeMinutes(value);
        maybeBurstMsg();
    }

    /**
     * 设置行程时间分钟值。
     *
     * @param value 分钟值
     */
    public void setTripTimeMinutes(int value) {
        stateStore.setTripTimeMinutes(value);
        maybeBurstMsg();
    }

    /**
     * 设置剩余油量。
     *
     * @param value 油量，单位 0.1L
     */
    public void setFuelLeftDeciL(int value) {
        stateStore.setFuelLeftDeciL(value);
        maybeBurstMsg();
    }

    /**
     * 设置油箱总量。
     *
     * @param value 油量，单位 0.1L
     */
    public void setFuelTotalDeciL(int value) {
        stateStore.setFuelTotalDeciL(value);
        maybeBurstMsg();
    }

    /**
     * 一次性更新整包车身快照。
     *
     * @param snapshot 车身快照；为 {@code null} 时忽略
     */
    public void updateSnapshot(VehicleSnapshot snapshot) {
        if (snapshot == null) {
            return;
        }
        stateStore.update(snapshot);
        maybeBurstMsg();
    }

    /**
     * 发送重启命令（MSGF CMD=0x01）。
     */
    public void sendReboot() {
        int nextSeq = seq.getAndIncrement();
        byte[] frame = FrameEncoder.encodeMsgCommandOnly(nextSeq, CMD_REBOOT, config.enableCrc32);
        enqueueControlFrame(new OutboundFrame(PRIORITY_CONTROL, queueOrder.incrementAndGet(), "CMD", nextSeq, frame, MapFrameKind.NONE));
    }

    /**
     * 设置屏幕亮度（MSGF CMD=0x02）。
     *
     * @param brightness 亮度值，范围 0..255
     * @throws IllegalArgumentException 当参数超出范围时抛出
     */
    public void sendBrightness(int brightness) {
        if (brightness < 0 || brightness > 255) {
            throw new IllegalArgumentException("brightness must be in range 0..255");
        }
        int nextSeq = seq.getAndIncrement();
        byte[] frame = FrameEncoder.encodeMsgCommandWithU8Arg(nextSeq, CMD_BRIGHTNESS, brightness, config.enableCrc32);
        enqueueControlFrame(new OutboundFrame(PRIORITY_CONTROL, queueOrder.incrementAndGet(), "CMD", nextSeq, frame, MapFrameKind.NONE));
    }

    /**
     * 设置显示翻转（MSGF CMD=0x03）。
     *
     * @param rotation 旋转枚举，不能为空
     * @throws IllegalArgumentException 当参数为空时抛出
     */
    public void sendDisplayOffsetRotation(DisplayOffsetRotation rotation) {
        if (rotation == null) {
            throw new IllegalArgumentException("rotation must not be null");
        }
        sendDisplayOffsetRotationRaw(rotation.value);
    }

    /**
     * 设置显示翻转（MSGF CMD=0x03）。
     * <p>
     * 协议层允许值仅为 1/3/5/7。建议优先使用 {@link #sendDisplayOffsetRotation(DisplayOffsetRotation)}。
     *
     * @param offsetRotation 原始 offset_rotation 值（仅允许 1/3/5/7）
     * @throws IllegalArgumentException 当参数非法时抛出
     */
    public void sendDisplayOffsetRotationRaw(int offsetRotation) {
        if (offsetRotation != 1 && offsetRotation != 3 && offsetRotation != 5 && offsetRotation != 7) {
            throw new IllegalArgumentException("offsetRotation must be one of 1,3,5,7");
        }
        int nextSeq = seq.getAndIncrement();
        byte[] frame = FrameEncoder.encodeMsgCommandWithU8Arg(nextSeq, CMD_OFFSET_ROTATION, offsetRotation, config.enableCrc32);
        enqueueControlFrame(new OutboundFrame(PRIORITY_CONTROL, queueOrder.incrementAndGet(), "CMD", nextSeq, frame, MapFrameKind.NONE));
    }

    /**
     * 发送 PNG 图像（IMGF）。
     *
     * @param pngBytes PNG 字节数组，不能为空，且不能超过 {@code imgMaxBytes}
     */
    public void sendPng(byte[] pngBytes) {
        sendPngInternal(pngBytes, MapFrameKind.NONE);
    }

    private void sendPngInternal(byte[] pngBytes, MapFrameKind kind) {
        if (pngBytes == null || pngBytes.length == 0) {
            emitDrop("IMGF", "empty image");
            return;
        }
        if (pngBytes.length > config.imgMaxBytes) {
            emitDrop("IMGF", "image too large: " + pngBytes.length);
            return;
        }
        int nextSeq = seq.getAndIncrement();
        byte[] frame = FrameEncoder.encodeImgPng(nextSeq, pngBytes, config.enableCrc32);
        enqueueImgFrame(new OutboundFrame(PRIORITY_IMG, queueOrder.incrementAndGet(), "IMGF", nextSeq, frame, kind));
        emitImageEnqueued("IMGF", nextSeq, frame.length);
    }

    /**
     * 写入 GPS 点（基础参数）。
     *
     * @param latitude 纬度，范围 [-90, 90]
     * @param longitude 经度，范围 [-180, 180]
     * @param timestampMs 采样时间戳（毫秒）
     */
    public void pushGpsPoint(double latitude, double longitude, long timestampMs) {
        pushGpsPoint(new GpsPoint(latitude, longitude, timestampMs));
    }

    /**
     * 写入 GPS 点（完整参数）。
     *
     * @param point GPS 点对象；为 {@code null} 时忽略
     */
    public void pushGpsPoint(GpsPoint point) {
        if (point == null) {
            return;
        }
        String filterReason = filterGps(point);
        if (filterReason != null) {
            emitGpsFiltered(point, filterReason);
            return;
        }

        synchronized (gpsLock) {
            if (lastAcceptedPoint != null) {
                double d = distanceMeters(lastAcceptedPoint.latitude, lastAcceptedPoint.longitude, point.latitude, point.longitude);
                boolean turnKeep = shouldKeepTurnPoint(lastAcceptedPoint, point, d);
                boolean bypassMinDistanceForBootstrap = track.size() < 2;
                if (!bypassMinDistanceForBootstrap && d < config.gpsMinDistanceM && !turnKeep) {
                    emitGpsFiltered(point, "distance<" + config.gpsMinDistanceM + "m");
                    return;
                }
                distanceSinceLastMapM += d;
            }

            track.addLast(point);
            if (track.size() > config.trackMaxPoints) {
                sparsifyTrackLocked();
            }
            lastAcceptedPoint = point;
            lastGpsIngestMs = point.timestampMs;
            acceptedSinceLastMap++;
            emitGpsAccepted(point);

            maybeTriggerMapFetchLocked(System.currentTimeMillis());
        }
    }

    private void maybeBurstMsg() {
        if (!config.burstOnVehicleDataChange || !running) {
            return;
        }
        safeMsgTick();
    }

    private void safeMsgTick() {
        try {
            msgTick();
        } catch (Exception e) {
            emitError("msg.tick", e);
        }
    }

    private void msgTick() {
        if (!running) {
            return;
        }
        VehicleStateStore.SnapshotWithDirty s = stateStore.snapshot();
        long now = System.currentTimeMillis();
        boolean shouldSend = s.dirty;
        if (!shouldSend) {
            long idleIntervalMs = Math.max(1L, 1000L / Math.max(1, config.msgIdleRateHz));
            shouldSend = (now - lastMsgSentMs) >= idleIntervalMs;
        }
        if (!shouldSend) {
            return;
        }
        int nextSeq = seq.getAndIncrement();
        byte[] frame = FrameEncoder.encodeMsgSnapshot(nextSeq, s.snapshot, config.enableCrc32);
        enqueueMsgFrame(new OutboundFrame(PRIORITY_MSG, queueOrder.incrementAndGet(), "MSGF", nextSeq, frame, MapFrameKind.NONE));
        lastMsgSentMs = now;
    }

    private void maybeTriggerMapFetchLocked(long nowMs) {
        if (mapImageProvider == null || track.size() < 2) {
            return;
        }

        boolean triggerInitial = false;
        if (!initialMapFrameTriggered
                && config.initialFramePolicy == HudSdkConfig.InitialFramePolicy.ON_TWO_POINTS
                && track.size() >= 2) {
            triggerInitial = true;
        }

        boolean triggerPeriodic = false;
        if (config.periodicRefreshIntervalMs > 0
                && initialMapFrameTriggered
                && (nowMs - lastPeriodicMapTriggerMs) >= config.periodicRefreshIntervalMs) {
            triggerPeriodic = true;
        }

        if (nowMs < nextMapRetryAtMs) {
            mapFetchPending = true;
            scheduleRetryLocked(nowMs);
            return;
        }
        boolean triggerByPoints = acceptedSinceLastMap >= config.mapTriggerPointCount;
        boolean triggerByTime = (nowMs - lastMapFetchMs) >= config.mapTriggerIntervalMs;
        boolean triggerByDistance = distanceSinceLastMapM >= config.mapTriggerDistanceM;
        if (!(triggerInitial || triggerPeriodic || triggerByPoints || triggerByTime || triggerByDistance)) {
            return;
        }
        MapFetchTriggerReason reason = MapFetchTriggerReason.NORMAL;
        if (triggerInitial) {
            reason = MapFetchTriggerReason.INITIAL;
        } else if (triggerPeriodic) {
            reason = MapFetchTriggerReason.PERIODIC;
        }
        requestMapFetchLocked(nowMs, reason);
    }

    private void requestMapFetchLocked(long nowMs, final MapFetchTriggerReason reason) {
        if (!running) {
            return;
        }
        if (mapFetchInFlight) {
            mapFetchPending = true;
            return;
        }
        mapFetchInFlight = true;
        mapFetchPending = false;
        lastMapFetchMs = nowMs;
        if (reason == MapFetchTriggerReason.INITIAL) {
            initialMapFrameTriggered = true;
            emitInitialMapFrameTriggered();
        } else if (reason == MapFetchTriggerReason.PERIODIC) {
            lastPeriodicMapTriggerMs = nowMs;
        }
        final List<GpsPoint> points = new ArrayList<GpsPoint>(track);

        try {
            scheduler.execute(new Runnable() {
                @Override
                public void run() {
                    doMapFetch(points, reason);
                }
            });
        } catch (RejectedExecutionException e) {
            mapFetchInFlight = false;
            mapFetchPending = true;
            emitError("map.schedule", e);
        }
    }

    private void doMapFetch(List<GpsPoint> points, MapFetchTriggerReason reason) {
        boolean ok = false;
        long t0 = System.currentTimeMillis();
        if (!points.isEmpty()) {
            emitMapFetchStart(points.size(), points.get(0).timestampMs, points.get(points.size() - 1).timestampMs);
        }
        try {
            byte[] png = mapImageProvider.fetchTrackImage(points);
            if (png != null && png.length > 0) {
                sendPngInternal(png, mapFrameKindFromReason(reason));
                emitMapFetchSuccess(System.currentTimeMillis() - t0, png.length);
                ok = true;
            } else {
                emitDrop("IMGF", "map provider returned empty image");
                emitMapFetchError("map.fetch", "map provider returned empty image");
            }
        } catch (Exception e) {
            emitMapFetchError("map.fetch", String.valueOf(e.getMessage()));
            emitError("map.fetch", e);
        }

        synchronized (gpsLock) {
            if (ok) {
                acceptedSinceLastMap = 0;
                distanceSinceLastMapM = 0;
                currentBackoffMs = config.mapRetryBackoffInitialMs;
                nextMapRetryAtMs = 0;
                retryReason = MapFetchTriggerReason.NORMAL;
            } else {
                nextMapRetryAtMs = System.currentTimeMillis() + currentBackoffMs;
                currentBackoffMs = Math.min(currentBackoffMs * 2, config.mapRetryBackoffMaxMs);
                mapFetchPending = true;
                retryReason = reason;
                scheduleRetryLocked(System.currentTimeMillis());
            }
            mapFetchInFlight = false;

            if (mapFetchPending && nextMapRetryAtMs == 0) {
                requestMapFetchLocked(System.currentTimeMillis(), retryReason);
            }
        }
    }

    private void scheduleRetryLocked(long nowMs) {
        if (!running) {
            return;
        }
        if (mapRetryScheduled) {
            return;
        }
        long delayMs = Math.max(1L, nextMapRetryAtMs - nowMs);
        mapRetryScheduled = true;
        try {
            scheduler.schedule(new Runnable() {
                @Override
                public void run() {
                    synchronized (gpsLock) {
                        mapRetryScheduled = false;
                        if (!mapFetchPending || mapFetchInFlight) {
                            return;
                        }
                        if (System.currentTimeMillis() < nextMapRetryAtMs) {
                            scheduleRetryLocked(System.currentTimeMillis());
                            return;
                        }
                        requestMapFetchLocked(System.currentTimeMillis(), retryReason);
                    }
                }
            }, delayMs, TimeUnit.MILLISECONDS);
        } catch (RejectedExecutionException e) {
            mapRetryScheduled = false;
            emitError("map.retry.schedule", e);
        }
    }

    private String filterGps(GpsPoint p) {
        if (Double.isNaN(p.latitude) || Double.isNaN(p.longitude)) {
            return "nan";
        }
        if (p.latitude < -90.0 || p.latitude > 90.0 || p.longitude < -180.0 || p.longitude > 180.0) {
            return "latlon out of range";
        }
        synchronized (gpsLock) {
            // Bootstrap 首帧阶段允许更宽松的时间过滤，尽快凑齐 2 个点触发首帧地图。
            boolean bypassTimeFilterForBootstrap = track.size() < 2;
            if (!bypassTimeFilterForBootstrap && lastGpsIngestMs > 0 && p.timestampMs <= lastGpsIngestMs) {
                return "timestamp not monotonic";
            }
            if (!bypassTimeFilterForBootstrap && lastGpsIngestMs > 0
                    && (p.timestampMs - lastGpsIngestMs) < config.gpsMinIntervalMs) {
                return "interval<" + config.gpsMinIntervalMs + "ms";
            }
        }
        if (!Float.isNaN(p.accuracyM) && p.accuracyM > config.gpsAccuracyThresholdM) {
            return "accuracy>" + config.gpsAccuracyThresholdM;
        }
        return null;
    }

    private boolean shouldKeepTurnPoint(GpsPoint last, GpsPoint current, double distanceMeters) {
        if (distanceMeters < 3.0) {
            return false;
        }
        if (Float.isNaN(last.bearingDeg) || Float.isNaN(current.bearingDeg)) {
            return false;
        }
        double diff = Math.abs(last.bearingDeg - current.bearingDeg);
        diff = Math.min(diff, 360.0 - diff);
        return diff >= config.gpsTurnAngleDeg;
    }

    private void sparsifyTrackLocked() {
        if (track.size() <= config.trackMaxPoints) {
            return;
        }
        List<GpsPoint> dense = new ArrayList<GpsPoint>(track);
        List<GpsPoint> sparse = simplifyTrackToMaxPoints(dense, config.trackMaxPoints);
        track.clear();
        track.addAll(sparse);
    }

    private static List<GpsPoint> simplifyTrackToMaxPoints(List<GpsPoint> points, int maxPoints) {
        if (points.size() <= maxPoints) {
            return points;
        }
        double low = 0.0;
        double high = 2.0;
        List<GpsPoint> best = simplifyRdp(points, high);
        while (best.size() > maxPoints && high < 50000.0) {
            low = high;
            high *= 2.0;
            best = simplifyRdp(points, high);
        }
        if (best.size() > maxPoints) {
            return best;
        }

        for (int i = 0; i < RDP_BISECTION_STEPS; i++) {
            double mid = (low + high) * 0.5;
            List<GpsPoint> candidate = simplifyRdp(points, mid);
            if (candidate.size() > maxPoints) {
                low = mid;
            } else {
                high = mid;
                best = candidate;
            }
        }
        return best;
    }

    private static List<GpsPoint> simplifyRdp(List<GpsPoint> points, double epsilonMeters) {
        int n = points.size();
        if (n <= 2) {
            return points;
        }
        boolean[] keep = new boolean[n];
        keep[0] = true;
        keep[n - 1] = true;
        markRdpKeep(points, 0, n - 1, epsilonMeters, keep);

        List<GpsPoint> out = new ArrayList<GpsPoint>(n);
        for (int i = 0; i < n; i++) {
            if (keep[i]) {
                out.add(points.get(i));
            }
        }
        return out;
    }

    private static void markRdpKeep(List<GpsPoint> points, int start, int end, double epsilonMeters, boolean[] keep) {
        if (end - start <= 1) {
            return;
        }
        GpsPoint a = points.get(start);
        GpsPoint b = points.get(end);
        int farthestIdx = -1;
        double farthestDistance = -1.0;
        for (int i = start + 1; i < end; i++) {
            double d = pointToSegmentDistanceMeters(points.get(i), a, b);
            if (d > farthestDistance) {
                farthestDistance = d;
                farthestIdx = i;
            }
        }
        if (farthestIdx > start && farthestDistance > epsilonMeters) {
            keep[farthestIdx] = true;
            markRdpKeep(points, start, farthestIdx, epsilonMeters, keep);
            markRdpKeep(points, farthestIdx, end, epsilonMeters, keep);
        }
    }

    private static double pointToSegmentDistanceMeters(GpsPoint p, GpsPoint a, GpsPoint b) {
        double refLatRad = Math.toRadians((a.latitude + b.latitude + p.latitude) / 3.0);
        double bx = (b.longitude - a.longitude) * DEG_TO_RAD * EARTH_RADIUS_M * Math.cos(refLatRad);
        double by = (b.latitude - a.latitude) * DEG_TO_RAD * EARTH_RADIUS_M;
        double px = (p.longitude - a.longitude) * DEG_TO_RAD * EARTH_RADIUS_M * Math.cos(refLatRad);
        double py = (p.latitude - a.latitude) * DEG_TO_RAD * EARTH_RADIUS_M;
        double segLen2 = bx * bx + by * by;
        if (segLen2 <= 1e-9) {
            return Math.hypot(px, py);
        }
        double t = (px * bx + py * by) / segLen2;
        t = Math.max(0.0, Math.min(1.0, t));
        double dx = px - t * bx;
        double dy = py - t * by;
        return Math.hypot(dx, dy);
    }

    private static double distanceMeters(double lat1, double lon1, double lat2, double lon2) {
        double dLat = Math.toRadians(lat2 - lat1);
        double dLon = Math.toRadians(lon2 - lon1);
        double a = Math.sin(dLat / 2.0) * Math.sin(dLat / 2.0)
                + Math.cos(Math.toRadians(lat1)) * Math.cos(Math.toRadians(lat2))
                * Math.sin(dLon / 2.0) * Math.sin(dLon / 2.0);
        double c = 2.0 * Math.atan2(Math.sqrt(a), Math.sqrt(1.0 - a));
        return EARTH_RADIUS_M * c;
    }

    private void startWriterThread() {
        writerThread = new Thread(new Runnable() {
            @Override
            public void run() {
                while (writerRunning || !sendQueue.isEmpty()) {
                    try {
                        OutboundFrame f = sendQueue.poll(100, TimeUnit.MILLISECONDS);
                        if (f == null) {
                            continue;
                        }
                        transport.write(f.bytes);
                        transport.flush();
                        onFrameSent(f);
                    } catch (InterruptedException e) {
                        Thread.currentThread().interrupt();
                        break;
                    } catch (IOException e) {
                        emitError("transport.write", e);
                    }
                }
            }
        }, "esp-hud-writer");
        writerThread.setDaemon(true);
        writerThread.start();
    }

    private void enqueueControlFrame(OutboundFrame frame) {
        sendQueue.offer(frame);
    }

    private void enqueueMsgFrame(OutboundFrame frame) {
        List<OutboundFrame> stale = new ArrayList<OutboundFrame>();
        for (OutboundFrame queued : sendQueue) {
            if ("MSGF".equals(queued.channel)) {
                stale.add(queued);
            }
        }
        for (OutboundFrame old : stale) {
            if (sendQueue.remove(old)) {
                emitDrop("MSGF", "replace old snapshot");
            }
        }
        sendQueue.offer(frame);
    }

    private void enqueueImgFrame(OutboundFrame frame) {
        List<OutboundFrame> queuedImgs = new ArrayList<OutboundFrame>();
        for (OutboundFrame queued : sendQueue) {
            if ("IMGF".equals(queued.channel)) {
                queuedImgs.add(queued);
            }
        }
        sendQueue.offer(frame);
        queuedImgs.add(frame);
        int max = Math.max(1, config.imgQueueCapacity);
        while (queuedImgs.size() > max) {
            OutboundFrame oldest = findOldest(queuedImgs);
            if (oldest == null) {
                break;
            }
            queuedImgs.remove(oldest);
            if (sendQueue.remove(oldest)) {
                emitDrop("IMGF", "drop old image");
            }
        }
    }

    private OutboundFrame findOldest(List<OutboundFrame> frames) {
        OutboundFrame oldest = null;
        for (OutboundFrame frame : frames) {
            if (oldest == null || frame.order < oldest.order) {
                oldest = frame;
            }
        }
        return oldest;
    }

    private void onFrameSent(OutboundFrame f) {
        if ("MSGF".equals(f.channel)) {
            sentMsg.incrementAndGet();
        } else if ("IMGF".equals(f.channel)) {
            sentImg.incrementAndGet();
            if (f.mapFrameKind == MapFrameKind.INITIAL) {
                emitInitialMapFrameSent();
            } else if (f.mapFrameKind == MapFrameKind.PERIODIC) {
                emitPeriodicMapFrameSent();
            }
        } else if ("CMD".equals(f.channel)) {
            sentCmd.incrementAndGet();
        }
        HudSdkListener l = listener;
        if (l != null) {
            l.onFrameSent(f.channel, f.seq, f.bytes.length);
        }
    }

    private void emitDrop(String channel, String reason) {
        dropped.incrementAndGet();
        HudSdkListener l = listener;
        if (l != null) {
            l.onFrameDropped(channel, reason);
        }
    }

    private void emitError(String stage, Exception e) {
        errors.incrementAndGet();
        HudSdkListener l = listener;
        if (l != null) {
            l.onError(stage, e);
        }
    }

    private void emitMapFetchStart(int pointCount, long firstTimestampMs, long lastTimestampMs) {
        HudSdkListener l = listener;
        if (l != null) {
            l.onMapFetchStart(pointCount, firstTimestampMs, lastTimestampMs);
        }
    }

    private void emitMapFetchSuccess(long latencyMs, int bytes) {
        HudSdkListener l = listener;
        if (l != null) {
            l.onMapFetchSuccess(latencyMs, bytes);
        }
    }

    private void emitMapFetchError(String stage, String reason) {
        HudSdkListener l = listener;
        if (l != null) {
            l.onMapFetchError(stage, reason);
        }
    }

    private void emitImageEnqueued(String channel, int seq, int bytes) {
        HudSdkListener l = listener;
        if (l != null) {
            l.onImageEnqueued(channel, seq, bytes);
        }
    }

    private void emitInitialMapFrameTriggered() {
        HudSdkListener l = listener;
        if (l != null) {
            l.onInitialMapFrameTriggered();
        }
    }

    private void emitInitialMapFrameSent() {
        HudSdkListener l = listener;
        if (l != null) {
            l.onInitialMapFrameSent();
        }
    }

    private void emitPeriodicMapFrameSent() {
        HudSdkListener l = listener;
        if (l != null) {
            l.onPeriodicMapFrameSent();
        }
    }

    private static MapFrameKind mapFrameKindFromReason(MapFetchTriggerReason reason) {
        if (reason == MapFetchTriggerReason.INITIAL) {
            return MapFrameKind.INITIAL;
        }
        if (reason == MapFetchTriggerReason.PERIODIC) {
            return MapFrameKind.PERIODIC;
        }
        return MapFrameKind.NONE;
    }

    private void emitGpsAccepted(GpsPoint point) {
        HudSdkListener l = listener;
        if (l != null) {
            l.onGpsPointAccepted(point);
        }
    }

    private void emitGpsFiltered(GpsPoint point, String reason) {
        HudSdkListener l = listener;
        if (l != null) {
            l.onGpsPointFiltered(point, reason);
        }
    }

    private static final class OutboundFrame implements Comparable<OutboundFrame> {
        final int priority;
        final long order;
        final String channel;
        final int seq;
        final byte[] bytes;
        final MapFrameKind mapFrameKind;

        OutboundFrame(int priority, long order, String channel, int seq, byte[] bytes, MapFrameKind mapFrameKind) {
            this.priority = priority;
            this.order = order;
            this.channel = channel;
            this.seq = seq;
            this.bytes = bytes;
            this.mapFrameKind = mapFrameKind;
        }

        @Override
        public int compareTo(OutboundFrame other) {
            if (this.priority != other.priority) {
                return this.priority - other.priority;
            }
            if (this.order < other.order) {
                return -1;
            }
            if (this.order > other.order) {
                return 1;
            }
            return 0;
        }
    }

    private static final class VehicleStateStore {
        private final Object lock = new Object();

        private int speedKmh;
        private int engineRpm;
        private int odoMeters;
        private int tripOdoMeters;
        private int outsideTempDeciC;
        private int insideTempDeciC;
        private int batteryMv = 12000;
        private int currentTimeMinutes;
        private int tripTimeMinutes;
        private int fuelLeftDeciL;
        private int fuelTotalDeciL;
        private boolean dirty = true;

        SnapshotWithDirty snapshot() {
            synchronized (lock) {
                VehicleSnapshot snapshot = new VehicleSnapshot(
                        speedKmh,
                        engineRpm,
                        odoMeters,
                        tripOdoMeters,
                        outsideTempDeciC,
                        insideTempDeciC,
                        batteryMv,
                        currentTimeMinutes,
                        tripTimeMinutes,
                        fuelLeftDeciL,
                        fuelTotalDeciL);
                boolean outDirty = dirty;
                dirty = false;
                return new SnapshotWithDirty(snapshot, outDirty);
            }
        }

        void update(VehicleSnapshot snapshot) {
            synchronized (lock) {
                speedKmh = snapshot.speedKmh;
                engineRpm = snapshot.engineRpm;
                odoMeters = snapshot.odoMeters;
                tripOdoMeters = snapshot.tripOdoMeters;
                outsideTempDeciC = snapshot.outsideTempDeciC;
                insideTempDeciC = snapshot.insideTempDeciC;
                batteryMv = snapshot.batteryMv;
                currentTimeMinutes = snapshot.currentTimeMinutes;
                tripTimeMinutes = snapshot.tripTimeMinutes;
                fuelLeftDeciL = snapshot.fuelLeftDeciL;
                fuelTotalDeciL = snapshot.fuelTotalDeciL;
                dirty = true;
            }
        }

        void setSpeedKmh(int value) {
            setField(Field.SPEED, value);
        }

        void setEngineRpm(int value) {
            setField(Field.RPM, value);
        }

        void setOdoMeters(int value) {
            setField(Field.ODO, value);
        }

        void setTripOdoMeters(int value) {
            setField(Field.TRIP_ODO, value);
        }

        void setOutsideTempDeciC(int value) {
            setField(Field.OUT_TEMP, value);
        }

        void setInsideTempDeciC(int value) {
            setField(Field.IN_TEMP, value);
        }

        void setBatteryMv(int value) {
            setField(Field.BATT, value);
        }

        void setCurrentTimeMinutes(int value) {
            setField(Field.CUR_MIN, value);
        }

        void setTripTimeMinutes(int value) {
            setField(Field.TRIP_MIN, value);
        }

        void setFuelLeftDeciL(int value) {
            setField(Field.FUEL_LEFT, value);
        }

        void setFuelTotalDeciL(int value) {
            setField(Field.FUEL_TOTAL, value);
        }

        private void setField(Field field, int value) {
            synchronized (lock) {
                switch (field) {
                    case SPEED:
                        if (speedKmh != value) {
                            speedKmh = value;
                            dirty = true;
                        }
                        break;
                    case RPM:
                        if (engineRpm != value) {
                            engineRpm = value;
                            dirty = true;
                        }
                        break;
                    case ODO:
                        if (odoMeters != value) {
                            odoMeters = value;
                            dirty = true;
                        }
                        break;
                    case TRIP_ODO:
                        if (tripOdoMeters != value) {
                            tripOdoMeters = value;
                            dirty = true;
                        }
                        break;
                    case OUT_TEMP:
                        if (outsideTempDeciC != value) {
                            outsideTempDeciC = value;
                            dirty = true;
                        }
                        break;
                    case IN_TEMP:
                        if (insideTempDeciC != value) {
                            insideTempDeciC = value;
                            dirty = true;
                        }
                        break;
                    case BATT:
                        if (batteryMv != value) {
                            batteryMv = value;
                            dirty = true;
                        }
                        break;
                    case CUR_MIN:
                        if (currentTimeMinutes != value) {
                            currentTimeMinutes = value;
                            dirty = true;
                        }
                        break;
                    case TRIP_MIN:
                        if (tripTimeMinutes != value) {
                            tripTimeMinutes = value;
                            dirty = true;
                        }
                        break;
                    case FUEL_LEFT:
                        if (fuelLeftDeciL != value) {
                            fuelLeftDeciL = value;
                            dirty = true;
                        }
                        break;
                    case FUEL_TOTAL:
                        if (fuelTotalDeciL != value) {
                            fuelTotalDeciL = value;
                            dirty = true;
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        private enum Field {
            SPEED,
            RPM,
            ODO,
            TRIP_ODO,
            OUT_TEMP,
            IN_TEMP,
            BATT,
            CUR_MIN,
            TRIP_MIN,
            FUEL_LEFT,
            FUEL_TOTAL
        }

        static final class SnapshotWithDirty {
            final VehicleSnapshot snapshot;
            final boolean dirty;

            SnapshotWithDirty(VehicleSnapshot snapshot, boolean dirty) {
                this.snapshot = snapshot;
                this.dirty = dirty;
            }
        }
    }
}
