package cn.crazythursdayvivo50.esp_hud;

import java.io.IOException;

/**
 * HUD 数据发送通道抽象。
 * <p>
 * Android 侧可基于 USB CDC、蓝牙串口或网络连接实现该接口，SDK 仅负责调用。
 */
public interface HudTransport {
    /**
     * 写入一段完整的已编码帧字节。
     *
     * @param data 待发送字节，不能为 {@code null}
     * @throws IOException 当底层链路写失败时抛出
     */
    void write(byte[] data) throws IOException;

    /**
     * 刷新底层发送缓冲区。
     *
     * @throws IOException 当刷新失败时抛出
     */
    void flush() throws IOException;

    /**
     * 关闭传输通道并释放资源。
     *
     * @throws IOException 当关闭过程失败时抛出
     */
    void close() throws IOException;
}
