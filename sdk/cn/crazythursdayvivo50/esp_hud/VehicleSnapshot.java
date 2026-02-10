package cn.crazythursdayvivo50.esp_hud;

/**
 * 车身快照数据模型。
 * <p>
 * 所有单位与下位机协议保持一致。
 */
public final class VehicleSnapshot {
    /** 车速，单位 km/h。 */
    public final int speedKmh;
    /** 发动机转速，单位 rpm。 */
    public final int engineRpm;
    /** 总里程，单位 m。 */
    public final int odoMeters;
    /** 小计里程，单位 m。 */
    public final int tripOdoMeters;
    /** 外部温度，单位 0.1 摄氏度。 */
    public final int outsideTempDeciC;
    /** 内部温度，单位 0.1 摄氏度。 */
    public final int insideTempDeciC;
    /** 电池电压，单位 mV。 */
    public final int batteryMv;
    /** 当前时间，单位分钟，期望范围 0..1439。 */
    public final int currentTimeMinutes;
    /** 行程时间，单位分钟。 */
    public final int tripTimeMinutes;
    /** 剩余油量，单位 0.1L。 */
    public final int fuelLeftDeciL;
    /** 油箱总量，单位 0.1L。 */
    public final int fuelTotalDeciL;

    /**
     * 构造车身快照。
     *
     * @param speedKmh 车速，单位 km/h
     * @param engineRpm 发动机转速，单位 rpm
     * @param odoMeters 总里程，单位 m
     * @param tripOdoMeters 小计里程，单位 m
     * @param outsideTempDeciC 外部温度，单位 0.1 摄氏度
     * @param insideTempDeciC 内部温度，单位 0.1 摄氏度
     * @param batteryMv 电池电压，单位 mV
     * @param currentTimeMinutes 当前时间，单位分钟（0..1439）
     * @param tripTimeMinutes 行程时间，单位分钟
     * @param fuelLeftDeciL 剩余油量，单位 0.1L
     * @param fuelTotalDeciL 油箱总量，单位 0.1L
     */
    public VehicleSnapshot(
            int speedKmh,
            int engineRpm,
            int odoMeters,
            int tripOdoMeters,
            int outsideTempDeciC,
            int insideTempDeciC,
            int batteryMv,
            int currentTimeMinutes,
            int tripTimeMinutes,
            int fuelLeftDeciL,
            int fuelTotalDeciL) {
        this.speedKmh = speedKmh;
        this.engineRpm = engineRpm;
        this.odoMeters = odoMeters;
        this.tripOdoMeters = tripOdoMeters;
        this.outsideTempDeciC = outsideTempDeciC;
        this.insideTempDeciC = insideTempDeciC;
        this.batteryMv = batteryMv;
        this.currentTimeMinutes = currentTimeMinutes;
        this.tripTimeMinutes = tripTimeMinutes;
        this.fuelLeftDeciL = fuelLeftDeciL;
        this.fuelTotalDeciL = fuelTotalDeciL;
    }
}
