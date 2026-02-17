package cn.crazythursdayvivo50.esp_hud;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.PriorityQueue;

/**
 * Streaming Visvalingam-Whyatt simplifier with fixed-capacity output.
 *
 * <p>The structure is optimized for append-only GPS streams:
 * add a point, update local importance, and evict least-important points
 * while keeping size <= maxPoints.
 */
final class OnlineVwTrackSimplifier {
    private static final double INF_AREA = Double.MAX_VALUE;

    private static final class Node {
        final GpsPoint point;
        Node prev;
        Node next;
        double area = INF_AREA;
        boolean removed;
        int version;

        Node(GpsPoint point) {
            this.point = point;
        }
    }

    private static final class HeapEntry {
        final Node node;
        final int version;

        HeapEntry(Node node) {
            this.node = node;
            this.version = node.version;
        }
    }

    private final int maxPoints;
    private final PriorityQueue<HeapEntry> minHeap = new PriorityQueue<HeapEntry>(
            new Comparator<HeapEntry>() {
                @Override
                public int compare(HeapEntry a, HeapEntry b) {
                    return Double.compare(a.node.area, b.node.area);
                }
            });

    private Node head;
    private Node tail;
    private int size;

    OnlineVwTrackSimplifier(int maxPoints) {
        this.maxPoints = Math.max(2, maxPoints);
    }

    void clear() {
        head = null;
        tail = null;
        size = 0;
        minHeap.clear();
    }

    int size() {
        return size;
    }

    void add(GpsPoint point) {
        Node node = new Node(point);
        if (tail == null) {
            head = node;
            tail = node;
            size = 1;
            return;
        }

        tail.next = node;
        node.prev = tail;
        tail = node;
        size++;

        // Only the old tail might become an interior point and needs area update.
        Node maybeInterior = node.prev;
        if (maybeInterior != null && maybeInterior.prev != null) {
            updateAreaAndOffer(maybeInterior);
        }

        while (size > maxPoints) {
            if (!removeLeastImportant()) {
                break;
            }
        }
    }

    List<GpsPoint> snapshot() {
        List<GpsPoint> out = new ArrayList<GpsPoint>(size);
        Node cur = head;
        while (cur != null) {
            if (!cur.removed) {
                out.add(cur.point);
            }
            cur = cur.next;
        }
        return out;
    }

    private boolean removeLeastImportant() {
        Node node = pollValidRemovableNode();
        if (node == null) {
            return false;
        }
        Node prev = node.prev;
        Node next = node.next;

        node.removed = true;
        node.prev = null;
        node.next = null;
        size--;

        if (prev != null) {
            prev.next = next;
        } else {
            head = next;
        }
        if (next != null) {
            next.prev = prev;
        } else {
            tail = prev;
        }

        if (prev != null && prev.prev != null && prev.next != null) {
            updateAreaAndOffer(prev);
        }
        if (next != null && next.prev != null && next.next != null) {
            updateAreaAndOffer(next);
        }
        return true;
    }

    private Node pollValidRemovableNode() {
        while (!minHeap.isEmpty()) {
            HeapEntry e = minHeap.poll();
            Node n = e.node;
            if (n.removed) {
                continue;
            }
            if (e.version != n.version) {
                continue;
            }
            if (n.prev == null || n.next == null) {
                continue;
            }
            return n;
        }
        return null;
    }

    private void updateAreaAndOffer(Node node) {
        node.version++;
        node.area = triangleArea(node.prev.point, node.point, node.next.point);
        minHeap.offer(new HeapEntry(node));
    }

    private static double triangleArea(GpsPoint a, GpsPoint b, GpsPoint c) {
        // For local trajectory simplification, planar area over lat/lon degrees is sufficient.
        return Math.abs(
                a.longitude * (b.latitude - c.latitude)
                        + b.longitude * (c.latitude - a.latitude)
                        + c.longitude * (a.latitude - b.latitude)
        ) * 0.5;
    }
}
