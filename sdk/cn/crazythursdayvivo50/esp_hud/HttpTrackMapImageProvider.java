package cn.crazythursdayvivo50.esp_hud;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Base64;
import java.util.List;

/**
 * 默认 HTTP 轨迹地图图片提供器。
 * <p>
 * 请求协议与 Python 示例保持一致：POST JSON 到 `/track/image`，请求体为
 * <code>{"points":[[lon,lat], ...]}</code>，响应体为 PNG 字节。
 */
public final class HttpTrackMapImageProvider implements MapImageProvider {
    /** 与 Python 示例一致的默认接口地址。 */
    public static final String DEFAULT_URL = "https://azurehk.crazythursdayvivo50.cn/trace/track/image";
    /** 与 Python 示例一致的默认超时（毫秒）。 */
    public static final int DEFAULT_TIMEOUT_MS = 10_000;
    /** 与 Python 示例一致的默认最大图片大小（字节）。 */
    public static final int DEFAULT_MAX_PNG_BYTES = 200 * 1024;

    private final String url;
    private final int timeoutMs;
    private final int maxPngBytes;
    private final String basicAuthHeader;

    /**
     * 使用默认参数构造。
     */
    public HttpTrackMapImageProvider() {
        this(DEFAULT_URL, DEFAULT_TIMEOUT_MS, DEFAULT_MAX_PNG_BYTES);
    }

    /**
     * 使用自定义参数构造。
     *
     * @param url 轨迹图接口地址
     * @param timeoutMs 连接/读取超时（毫秒），必须大于 0
     * @param maxPngBytes 最大 PNG 字节数，必须大于 0
     */
    public HttpTrackMapImageProvider(String url, int timeoutMs, int maxPngBytes) {
        this(url, timeoutMs, maxPngBytes, null, null);
    }

    /**
     * 使用自定义参数构造（含 Basic Auth）。
     *
     * @param url 轨迹图接口地址
     * @param timeoutMs 连接/读取超时（毫秒），必须大于 0
     * @param maxPngBytes 最大 PNG 字节数，必须大于 0
     * @param username Basic Auth 用户名
     * @param password Basic Auth 密码
     */
    public HttpTrackMapImageProvider(String url, int timeoutMs, int maxPngBytes, String username, String password) {
        if (url == null || url.trim().isEmpty()) {
            throw new IllegalArgumentException("url must not be empty");
        }
        if (timeoutMs <= 0) {
            throw new IllegalArgumentException("timeoutMs must be > 0");
        }
        if (maxPngBytes <= 0) {
            throw new IllegalArgumentException("maxPngBytes must be > 0");
        }
        this.url = url;
        this.timeoutMs = timeoutMs;
        this.maxPngBytes = maxPngBytes;
        this.basicAuthHeader = buildBasicAuthHeader(username, password);
    }

    @Override
    public byte[] fetchTrackImage(List<GpsPoint> points) throws Exception {
        if (points == null || points.isEmpty()) {
            throw new IllegalArgumentException("points must not be empty");
        }

        String body = buildRequestJson(points);
        HttpURLConnection conn = null;
        try {
            conn = (HttpURLConnection) new URL(url).openConnection();
            conn.setRequestMethod("POST");
            conn.setConnectTimeout(timeoutMs);
            conn.setReadTimeout(timeoutMs);
            conn.setDoOutput(true);
            conn.setRequestProperty("Content-Type", "application/json");
            conn.setRequestProperty("accept", "application/json");
            if (basicAuthHeader != null) {
                conn.setRequestProperty("Authorization", basicAuthHeader);
            }

            byte[] requestBytes = body.getBytes(StandardCharsets.UTF_8);
            conn.setFixedLengthStreamingMode(requestBytes.length);
            try (OutputStream os = conn.getOutputStream()) {
                os.write(requestBytes);
                os.flush();
            }

            int code = conn.getResponseCode();
            if (code < 200 || code >= 300) {
                String errBody = readAllAsString(safeErrorStream(conn), 4096);
                throw new IOException("http status=" + code + ", body=" + errBody);
            }

            byte[] png = readAllWithLimit(conn.getInputStream(), maxPngBytes);
            if (png.length == 0) {
                throw new IOException("empty png response");
            }
            return png;
        } finally {
            if (conn != null) {
                conn.disconnect();
            }
        }
    }

    private String buildRequestJson(List<GpsPoint> points) {
        StringBuilder sb = new StringBuilder();
        sb.append("{\"points\":[");
        for (int i = 0; i < points.size(); i++) {
            if (i > 0) {
                sb.append(',');
            }
            GpsPoint p = points.get(i);
            // 与 Python 示例保持一致：[lon, lat]
            sb.append('[')
              .append(Double.toString(p.longitude))
              .append(',')
              .append(Double.toString(p.latitude))
              .append(']');
        }
        sb.append("]}");
        return sb.toString();
    }

    private static byte[] readAllWithLimit(InputStream in, int maxBytes) throws IOException {
        if (in == null) {
            return new byte[0];
        }
        try (InputStream is = in; ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] buf = new byte[4096];
            int total = 0;
            int n;
            while ((n = is.read(buf)) != -1) {
                total += n;
                if (total > maxBytes) {
                    throw new IOException("PNG too large: " + total + " bytes");
                }
                out.write(buf, 0, n);
            }
            return out.toByteArray();
        }
    }

    private static InputStream safeErrorStream(HttpURLConnection conn) {
        try {
            return conn.getErrorStream();
        } catch (Exception ignored) {
            return null;
        }
    }

    private static String readAllAsString(InputStream in, int maxBytes) throws IOException {
        if (in == null) {
            return "";
        }
        byte[] data = readAllWithLimit(in, maxBytes);
        return new String(data, StandardCharsets.UTF_8);
    }

    private static String buildBasicAuthHeader(String username, String password) {
        if (username == null && password == null) {
            return null;
        }
        if (username == null || username.trim().isEmpty()) {
            throw new IllegalArgumentException("username must not be empty when using basic auth");
        }
        if (password == null || password.isEmpty()) {
            throw new IllegalArgumentException("password must not be empty when using basic auth");
        }
        String raw = username + ":" + password;
        String encoded = Base64.getEncoder().encodeToString(raw.getBytes(StandardCharsets.UTF_8));
        return "Basic " + encoded;
    }
}
