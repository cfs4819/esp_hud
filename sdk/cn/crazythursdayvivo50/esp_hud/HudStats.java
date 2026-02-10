package cn.crazythursdayvivo50.esp_hud;

/**
 * SDK 运行时统计快照。
 */
public final class HudStats {
    /** 已发送 MSGF 帧数量。 */
    public final long msgSent;
    /** 已发送 IMGF 帧数量。 */
    public final long imgSent;
    /** 已发送控制帧数量（例如重启命令）。 */
    public final long cmdSent;
    /** 丢弃事件总数。 */
    public final long dropped;
    /** 错误事件总数。 */
    public final long errors;
    /** 当前发送队列深度。 */
    public final int queueDepth;

    HudStats(long msgSent, long imgSent, long cmdSent, long dropped, long errors, int queueDepth) {
        this.msgSent = msgSent;
        this.imgSent = imgSent;
        this.cmdSent = cmdSent;
        this.dropped = dropped;
        this.errors = errors;
        this.queueDepth = queueDepth;
    }
}
