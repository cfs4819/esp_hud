package cn.crazythursdayvivo50.esp_hud;

/**
 * HUD SDK 事件监听器。
 * <p>
 * 所有方法均提供默认空实现，可按需覆写。
 */
public interface HudSdkListener {
    /**
     * 帧发送成功回调。
     *
     * @param channel 通道名：通常为 {@code MSGF}、{@code IMGF} 或 {@code CMD}
     * @param seq 帧序列号
     * @param bytes 帧总字节数（含头部）
     */
    default void onFrameSent(String channel, int seq, int bytes) {}

    /**
     * 帧被丢弃回调。
     *
     * @param channel 通道名：通常为 {@code MSGF} 或 {@code IMGF}
     * @param reason 丢弃原因
     */
    default void onFrameDropped(String channel, String reason) {}

    /**
     * GPS 点通过过滤并被纳入轨迹回调。
     *
     * @param point 被接受的 GPS 点
     */
    default void onGpsPointAccepted(GpsPoint point) {}

    /**
     * GPS 点被过滤回调。
     *
     * @param point 被过滤的 GPS 点
     * @param reason 过滤原因
     */
    default void onGpsPointFiltered(GpsPoint point, String reason) {}

    /**
     * 地图拉取开始事件。
     *
     * @param pointCount 参与拉取的轨迹点数
     * @param firstTimestampMs 首点时间戳（毫秒）
     * @param lastTimestampMs 末点时间戳（毫秒）
     */
    default void onMapFetchStart(int pointCount, long firstTimestampMs, long lastTimestampMs) {}

    /**
     * 地图拉取成功事件。
     *
     * @param latencyMs 拉取耗时（毫秒）
     * @param bytes 图片字节数
     */
    default void onMapFetchSuccess(long latencyMs, int bytes) {}

    /**
     * 地图拉取失败事件。
     *
     * @param stage 失败阶段
     * @param reason 失败原因
     */
    default void onMapFetchError(String stage, String reason) {}

    /**
     * 图片入队事件。
     *
     * @param channel 通道名（IMGF）
     * @param seq 帧序列号
     * @param bytes 帧总字节数（含头部）
     */
    default void onImageEnqueued(String channel, int seq, int bytes) {}

    /**
     * 首帧地图已触发（开始拉取）回调。
     */
    default void onInitialMapFrameTriggered() {}

    /**
     * 首帧地图已发送回调。
     */
    default void onInitialMapFrameSent() {}

    /**
     * 周期地图帧已发送回调。
     */
    default void onPeriodicMapFrameSent() {}

    /**
     * SDK 内部错误回调。
     *
     * @param stage 错误阶段标识，如 {@code transport.write}、{@code map.fetch}
     * @param error 具体异常对象
     */
    default void onError(String stage, Exception error) {}
}
