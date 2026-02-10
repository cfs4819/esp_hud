package cn.crazythursdayvivo50.esp_hud;

import java.util.zip.CRC32;

final class FrameEncoder {
    static final int MAGIC_MSGF = 0x4647534D;
    static final int MAGIC_IMGF = 0x46474D49;

    private FrameEncoder() {}

    static byte[] encodeMsgSnapshot(int seq, VehicleSnapshot snapshot, boolean enableCrc32) {
        byte[] payload = new byte[1 + 26];
        int p = 0;
        payload[p++] = 0x00;
        p = putInt16LE(payload, p, clampI16(snapshot.speedKmh));
        p = putInt16LE(payload, p, clampI16(snapshot.engineRpm));
        p = putInt32LE(payload, p, snapshot.odoMeters);
        p = putInt32LE(payload, p, snapshot.tripOdoMeters);
        p = putInt16LE(payload, p, clampI16(snapshot.outsideTempDeciC));
        p = putInt16LE(payload, p, clampI16(snapshot.insideTempDeciC));
        p = putInt16LE(payload, p, clampI16(snapshot.batteryMv));
        p = putUInt16LE(payload, p, clampU16(snapshot.currentTimeMinutes, 0, 1439));
        p = putUInt16LE(payload, p, clampU16(snapshot.tripTimeMinutes, 0, 0xFFFF));
        p = putUInt16LE(payload, p, clampU16(snapshot.fuelLeftDeciL, 0, 0xFFFF));
        putUInt16LE(payload, p, clampU16(snapshot.fuelTotalDeciL, 0, 0xFFFF));
        return encodeFrame(MAGIC_MSGF, payload, seq, enableCrc32);
    }

    static byte[] encodeMsgCommandOnly(int seq, int cmd, boolean enableCrc32) {
        byte[] payload = new byte[] { (byte) (cmd & 0xFF) };
        return encodeFrame(MAGIC_MSGF, payload, seq, enableCrc32);
    }

    static byte[] encodeImgPng(int seq, byte[] png, boolean enableCrc32) {
        return encodeFrame(MAGIC_IMGF, png, seq, enableCrc32);
    }

    private static byte[] encodeFrame(int magic, byte[] payload, int seq, boolean enableCrc32) {
        int total = 20 + payload.length;
        byte[] out = new byte[total];
        int h = 0;
        h = putInt32LE(out, h, magic);
        out[h++] = 0;
        out[h++] = 0;
        h = putUInt16LE(out, h, 0);
        h = putInt32LE(out, h, payload.length);
        int crc32 = enableCrc32 ? crc32(payload) : 0;
        h = putInt32LE(out, h, crc32);
        putInt32LE(out, h, seq);
        System.arraycopy(payload, 0, out, 20, payload.length);
        return out;
    }

    private static int crc32(byte[] data) {
        CRC32 crc32 = new CRC32();
        crc32.update(data);
        long value = crc32.getValue();
        return (int) (value & 0xFFFFFFFFL);
    }

    private static int putInt16LE(byte[] dst, int off, int value) {
        int v = value & 0xFFFF;
        dst[off] = (byte) (v & 0xFF);
        dst[off + 1] = (byte) ((v >>> 8) & 0xFF);
        return off + 2;
    }

    private static int putUInt16LE(byte[] dst, int off, int value) {
        dst[off] = (byte) (value & 0xFF);
        dst[off + 1] = (byte) ((value >>> 8) & 0xFF);
        return off + 2;
    }

    private static int putInt32LE(byte[] dst, int off, int value) {
        dst[off] = (byte) (value & 0xFF);
        dst[off + 1] = (byte) ((value >>> 8) & 0xFF);
        dst[off + 2] = (byte) ((value >>> 16) & 0xFF);
        dst[off + 3] = (byte) ((value >>> 24) & 0xFF);
        return off + 4;
    }

    private static int clampI16(int value) {
        if (value < Short.MIN_VALUE) {
            return Short.MIN_VALUE;
        }
        if (value > Short.MAX_VALUE) {
            return Short.MAX_VALUE;
        }
        return value;
    }

    private static int clampU16(int value, int min, int max) {
        if (value < min) {
            return min;
        }
        if (value > max) {
            return max;
        }
        return value;
    }
}

