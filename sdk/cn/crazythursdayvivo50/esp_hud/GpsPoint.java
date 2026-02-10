package cn.crazythursdayvivo50.esp_hud;

/**
 * 单个定位点数据模型。
 */
public final class GpsPoint {
    /** 纬度，范围 [-90, 90]。 */
    public final double latitude;
    /** 经度，范围 [-180, 180]。 */
    public final double longitude;
    /** 采样时间戳（毫秒）。 */
    public final long timestampMs;
    /** 定位精度（米），未知时可为 {@link Float#NaN}。 */
    public final float accuracyM;
    /** 地面速度（米/秒），未知时可为 {@link Float#NaN}。 */
    public final float speedMps;
    /** 航向角（度），未知时可为 {@link Float#NaN}。 */
    public final float bearingDeg;

    /**
     * 构造基础定位点。
     *
     * @param latitude 纬度，范围 [-90, 90]
     * @param longitude 经度，范围 [-180, 180]
     * @param timestampMs 采样时间戳（毫秒）
     */
    public GpsPoint(double latitude, double longitude, long timestampMs) {
        this(latitude, longitude, timestampMs, Float.NaN, Float.NaN, Float.NaN);
    }

    /**
     * 构造完整定位点。
     *
     * @param latitude 纬度，范围 [-90, 90]
     * @param longitude 经度，范围 [-180, 180]
     * @param timestampMs 采样时间戳（毫秒）
     * @param accuracyM 定位精度（米），未知时可传 {@link Float#NaN}
     * @param speedMps 地面速度（米/秒），未知时可传 {@link Float#NaN}
     * @param bearingDeg 航向角（度），未知时可传 {@link Float#NaN}
     */
    public GpsPoint(double latitude, double longitude, long timestampMs, float accuracyM, float speedMps, float bearingDeg) {
        this.latitude = latitude;
        this.longitude = longitude;
        this.timestampMs = timestampMs;
        this.accuracyM = accuracyM;
        this.speedMps = speedMps;
        this.bearingDeg = bearingDeg;
    }
}
