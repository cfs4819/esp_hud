package cn.crazythursdayvivo50.esp_hud;

import java.util.List;

/**
 * 地图图像提供器接口。
 * <p>
 * SDK 在 GPS 触发条件满足时调用此接口，将轨迹点转换为 PNG 图片，再通过 IMGF 通道发送给下位机。
 */
public interface MapImageProvider {
    /**
     * 创建默认 HTTP 轨迹图提供器。
     * <p>
     * 默认参数对齐 Python 示例：
     * URL=`http://azurehk.crazythursdayvivo50.cn:8123/track/image`，超时 10 秒，最大图片 200KB。
     *
     * @return 默认 HTTP 提供器实例
     */
    static MapImageProvider defaultProvider() {
        return new HttpTrackMapImageProvider();
    }

    /**
     * 创建自定义 HTTP 轨迹图提供器。
     *
     * @param url 轨迹图接口地址
     * @param timeoutMs 连接和读取超时（毫秒）
     * @param maxPngBytes 允许的最大 PNG 字节数
     * @return HTTP 提供器实例
     */
    static MapImageProvider httpProvider(String url, int timeoutMs, int maxPngBytes) {
        return new HttpTrackMapImageProvider(url, timeoutMs, maxPngBytes);
    }

    /**
     * 创建带 Basic Auth 的 HTTP 轨迹图提供器。
     *
     * @param url 轨迹图接口地址
     * @param timeoutMs 连接和读取超时（毫秒）
     * @param maxPngBytes 允许的最大 PNG 字节数
     * @param username Basic Auth 用户名
     * @param password Basic Auth 密码
     * @return HTTP 提供器实例
     */
    static MapImageProvider httpProviderWithBasicAuth(
            String url, int timeoutMs, int maxPngBytes, String username, String password) {
        return new HttpTrackMapImageProvider(url, timeoutMs, maxPngBytes, username, password);
    }

    /**
     * 根据轨迹点列表生成 PNG 图片。
     *
     * @param points 轨迹点列表，按时间升序，至少包含一个点
     * @return PNG 字节数组；返回 {@code null} 或空数组会被 SDK 视为无效结果
     * @throws Exception 地图请求、渲染或编码失败时抛出
     */
    byte[] fetchTrackImage(List<GpsPoint> points) throws Exception;
}
