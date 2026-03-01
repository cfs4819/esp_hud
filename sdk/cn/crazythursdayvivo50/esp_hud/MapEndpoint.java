package cn.crazythursdayvivo50.esp_hud;

import java.net.URI;

/**
 * 地图轨迹接口地址统一入口。
 */
public final class MapEndpoint {
    public static final String DEFAULT_URL = "https://azurehk.crazythursdayvivo50.cn/trace/track/image";

    private MapEndpoint() {
    }
    public static String normalize(String raw) {
        if (raw == null || raw.trim().isEmpty()) {
            return DEFAULT_URL;
        }
        String url = raw.trim();
        try {
            URI uri = URI.create(url);
            if (uri.getPort() == 8123) {
                return DEFAULT_URL;
            }
        } catch (Exception ignored) {
        }

        if (url.endsWith("/trace/track/image")) {
            return url;
        }
        if (url.endsWith("/track/image")) {
            return url;
        }
        if (url.endsWith("/trace")) {
            return url + "/track/image";
        }
        if (url.endsWith("/trace/")) {
            return url + "track/image";
        }
        return url;
    }
}
