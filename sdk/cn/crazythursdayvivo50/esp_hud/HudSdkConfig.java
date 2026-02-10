package cn.crazythursdayvivo50.esp_hud;

/**
 * HUD SDK 运行配置。
 * <p>
 * 通过 {@link Builder} 构建实例，未设置项将使用默认值。
 */
public final class HudSdkConfig {
    /** MSGF 正常发送频率（Hz）。默认 24。 */
    public final int msgRateHz;
    /** 数据无变化时的保活发送频率（Hz）。默认 2。 */
    public final int msgIdleRateHz;
    /** 数据变化时是否触发一次加急发送。默认开启。 */
    public final boolean burstOnVehicleDataChange;

    /** GPS 最小保留距离（米）。默认 5。 */
    public final double gpsMinDistanceM;
    /** GPS 最小采样间隔（毫秒）。默认 250。 */
    public final long gpsMinIntervalMs;
    /** 触发“转向保留点”的最小角度（度）。默认 20。 */
    public final double gpsTurnAngleDeg;
    /** GPS 精度过滤阈值（米）。大于该值的点会被过滤。默认 30。 */
    public final float gpsAccuracyThresholdM;

    /** 地图触发：新增有效点数量阈值。默认 5。 */
    public final int mapTriggerPointCount;
    /** 地图触发：最小时间间隔阈值（毫秒）。默认 2000。 */
    public final long mapTriggerIntervalMs;
    /** 地图触发：累计位移阈值（米）。默认 30。 */
    public final double mapTriggerDistanceM;
    /** 轨迹缓存最大点数。默认 200。 */
    public final int trackMaxPoints;
    /** 单张图像允许的最大字节数。默认 128KB。 */
    public final int imgMaxBytes;

    /** 是否启用 CRC32。默认关闭（与当前下位机默认设置一致）。 */
    public final boolean enableCrc32;
    /** MSG 队列容量。默认 1（仅保留最新快照）。 */
    public final int msgQueueCapacity;
    /** IMG 队列容量。默认 2（丢旧保新）。 */
    public final int imgQueueCapacity;

    /** 地图请求失败初始退避时间（毫秒）。默认 1000。 */
    public final long mapRetryBackoffInitialMs;
    /** 地图请求失败最大退避时间（毫秒）。默认 15000。 */
    public final long mapRetryBackoffMaxMs;

    private HudSdkConfig(Builder b) {
        this.msgRateHz = b.msgRateHz;
        this.msgIdleRateHz = b.msgIdleRateHz;
        this.burstOnVehicleDataChange = b.burstOnVehicleDataChange;
        this.gpsMinDistanceM = b.gpsMinDistanceM;
        this.gpsMinIntervalMs = b.gpsMinIntervalMs;
        this.gpsTurnAngleDeg = b.gpsTurnAngleDeg;
        this.gpsAccuracyThresholdM = b.gpsAccuracyThresholdM;
        this.mapTriggerPointCount = b.mapTriggerPointCount;
        this.mapTriggerIntervalMs = b.mapTriggerIntervalMs;
        this.mapTriggerDistanceM = b.mapTriggerDistanceM;
        this.trackMaxPoints = b.trackMaxPoints;
        this.imgMaxBytes = b.imgMaxBytes;
        this.enableCrc32 = b.enableCrc32;
        this.msgQueueCapacity = b.msgQueueCapacity;
        this.imgQueueCapacity = b.imgQueueCapacity;
        this.mapRetryBackoffInitialMs = b.mapRetryBackoffInitialMs;
        this.mapRetryBackoffMaxMs = b.mapRetryBackoffMaxMs;
    }

    /**
     * 创建配置构建器。
     *
     * @return 新的 {@link Builder} 实例
     */
    public static Builder newBuilder() {
        return new Builder();
    }

    /**
     * 配置构建器。
     */
    public static final class Builder {
        private int msgRateHz = 24;
        private int msgIdleRateHz = 2;
        private boolean burstOnVehicleDataChange = true;

        private double gpsMinDistanceM = 5.0;
        private long gpsMinIntervalMs = 250;
        private double gpsTurnAngleDeg = 20.0;
        private float gpsAccuracyThresholdM = 30.0f;

        private int mapTriggerPointCount = 5;
        private long mapTriggerIntervalMs = 2000;
        private double mapTriggerDistanceM = 30.0;
        private int trackMaxPoints = 200;
        private int imgMaxBytes = 128 * 1024;

        private boolean enableCrc32 = false;
        private int msgQueueCapacity = 1;
        private int imgQueueCapacity = 2;

        private long mapRetryBackoffInitialMs = 1000;
        private long mapRetryBackoffMaxMs = 15000;

        /**
         * 设置 MSGF 正常发送频率。
         *
         * @param value 发送频率（Hz），必须大于 0
         * @return 当前 Builder
         */
        public Builder setMsgRateHz(int value) {
            this.msgRateHz = value;
            return this;
        }

        /**
         * 设置空闲保活频率。
         *
         * @param value 保活频率（Hz），必须大于 0
         * @return 当前 Builder
         */
        public Builder setMsgIdleRateHz(int value) {
            this.msgIdleRateHz = value;
            return this;
        }

        /**
         * 设置是否在车身数据变化时立即触发一次发送。
         *
         * @param value 是否启用
         * @return 当前 Builder
         */
        public Builder setBurstOnVehicleDataChange(boolean value) {
            this.burstOnVehicleDataChange = value;
            return this;
        }

        /**
         * 设置 GPS 去重点最小距离。
         *
         * @param value 距离阈值（米），必须大于等于 0
         * @return 当前 Builder
         */
        public Builder setGpsMinDistanceM(double value) {
            this.gpsMinDistanceM = value;
            return this;
        }

        /**
         * 设置 GPS 最小采样间隔。
         *
         * @param value 间隔阈值（毫秒），必须大于等于 0
         * @return 当前 Builder
         */
        public Builder setGpsMinIntervalMs(long value) {
            this.gpsMinIntervalMs = value;
            return this;
        }

        /**
         * 设置转弯点保留角度阈值。
         *
         * @param value 角度阈值（度）
         * @return 当前 Builder
         */
        public Builder setGpsTurnAngleDeg(double value) {
            this.gpsTurnAngleDeg = value;
            return this;
        }

        /**
         * 设置 GPS 精度过滤阈值。
         *
         * @param value 精度阈值（米）
         * @return 当前 Builder
         */
        public Builder setGpsAccuracyThresholdM(float value) {
            this.gpsAccuracyThresholdM = value;
            return this;
        }

        /**
         * 设置地图触发的最小新增点数阈值。
         *
         * @param value 点数阈值，必须大于 0
         * @return 当前 Builder
         */
        public Builder setMapTriggerPointCount(int value) {
            this.mapTriggerPointCount = value;
            return this;
        }

        /**
         * 设置地图触发的最小时间间隔阈值。
         *
         * @param value 时间阈值（毫秒），必须大于 0
         * @return 当前 Builder
         */
        public Builder setMapTriggerIntervalMs(long value) {
            this.mapTriggerIntervalMs = value;
            return this;
        }

        /**
         * 设置地图触发的累计位移阈值。
         *
         * @param value 距离阈值（米）
         * @return 当前 Builder
         */
        public Builder setMapTriggerDistanceM(double value) {
            this.mapTriggerDistanceM = value;
            return this;
        }

        /**
         * 设置轨迹缓存上限。
         *
         * @param value 最大点数，必须大于等于 2
         * @return 当前 Builder
         */
        public Builder setTrackMaxPoints(int value) {
            this.trackMaxPoints = value;
            return this;
        }

        /**
         * 设置 IMGF 单帧最大字节数。
         *
         * @param value 最大字节数，必须大于 0
         * @return 当前 Builder
         */
        public Builder setImgMaxBytes(int value) {
            this.imgMaxBytes = value;
            return this;
        }

        /**
         * 设置是否启用 CRC32。
         *
         * @param value 是否启用
         * @return 当前 Builder
         */
        public Builder setEnableCrc32(boolean value) {
            this.enableCrc32 = value;
            return this;
        }

        /**
         * 设置 MSG 队列容量。
         *
         * @param value 队列容量，必须大于 0
         * @return 当前 Builder
         */
        public Builder setMsgQueueCapacity(int value) {
            this.msgQueueCapacity = value;
            return this;
        }

        /**
         * 设置 IMG 队列容量。
         *
         * @param value 队列容量，必须大于 0
         * @return 当前 Builder
         */
        public Builder setImgQueueCapacity(int value) {
            this.imgQueueCapacity = value;
            return this;
        }

        /**
         * 设置地图请求失败初始退避时间。
         *
         * @param value 退避时间（毫秒），必须大于 0
         * @return 当前 Builder
         */
        public Builder setMapRetryBackoffInitialMs(long value) {
            this.mapRetryBackoffInitialMs = value;
            return this;
        }

        /**
         * 设置地图请求失败最大退避时间。
         *
         * @param value 退避时间（毫秒），必须大于 0
         * @return 当前 Builder
         */
        public Builder setMapRetryBackoffMaxMs(long value) {
            this.mapRetryBackoffMaxMs = value;
            return this;
        }

        /**
         * 构建不可变配置对象。
         *
         * @return 配置实例
         * @throws IllegalArgumentException 当任一参数不满足约束时抛出
         */
        public HudSdkConfig build() {
            if (msgRateHz <= 0) {
                throw new IllegalArgumentException("msgRateHz must be > 0");
            }
            if (msgIdleRateHz <= 0) {
                throw new IllegalArgumentException("msgIdleRateHz must be > 0");
            }
            if (gpsMinDistanceM < 0) {
                throw new IllegalArgumentException("gpsMinDistanceM must be >= 0");
            }
            if (gpsMinIntervalMs < 0) {
                throw new IllegalArgumentException("gpsMinIntervalMs must be >= 0");
            }
            if (mapTriggerPointCount <= 0) {
                throw new IllegalArgumentException("mapTriggerPointCount must be > 0");
            }
            if (mapTriggerIntervalMs <= 0) {
                throw new IllegalArgumentException("mapTriggerIntervalMs must be > 0");
            }
            if (trackMaxPoints < 2) {
                throw new IllegalArgumentException("trackMaxPoints must be >= 2");
            }
            if (imgMaxBytes <= 0) {
                throw new IllegalArgumentException("imgMaxBytes must be > 0");
            }
            if (msgQueueCapacity <= 0) {
                throw new IllegalArgumentException("msgQueueCapacity must be > 0");
            }
            if (imgQueueCapacity <= 0) {
                throw new IllegalArgumentException("imgQueueCapacity must be > 0");
            }
            if (mapRetryBackoffInitialMs <= 0 || mapRetryBackoffMaxMs <= 0) {
                throw new IllegalArgumentException("map retry backoff must be > 0");
            }
            if (mapRetryBackoffInitialMs > mapRetryBackoffMaxMs) {
                throw new IllegalArgumentException("initial backoff cannot exceed max backoff");
            }
            return new HudSdkConfig(this);
        }
    }
}
