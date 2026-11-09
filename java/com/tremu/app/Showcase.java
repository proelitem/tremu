package com.tremu.app;

import android.graphics.Bitmap;
import java.io.File;
import java.io.FileOutputStream;
import java.util.HashMap;

/**
 * Store screenshots. While the file "showcase" exists in the app's private files folder (written with
 * adb run-as, so only on a debuggable build), the carousel shows console names on plain cards instead
 * of the player's games and posters (they belong to their publishers), the footer never shows the
 * library page's local address, and the pictures Tremu draws are also saved, flat, as PNG files in
 * Tremu/captures (one per kind every few seconds, numbered).
 */
final class Showcase {
    static final String[] TITLES = {"Arcade", "NES", "Mega Drive", "PlayStation", "Nintendo 64", "Dreamcast",
            "PlayStation 2", "Wii", "Xbox", "Xbox 360", "PSP"};
    private static final long CHECK_NS = 1_000_000_000L, SAVE_NS = 4_000_000_000L;
    private static final int MAX_PER_KIND = 25;
    private static long nextCheck;
    private static volatile boolean on;
    private static final HashMap<String, Long> lastSave = new HashMap<>();
    private static final HashMap<String, Integer> count = new HashMap<>();

    private Showcase() {}

    static boolean on() {
        long now = System.nanoTime();
        if (now > nextCheck) {
            nextCheck = now + CHECK_NS;
            File dir = TremuActivity.filesDir();
            on = dir != null && new File(dir, "showcase").exists();
        }
        return on;
    }

    /** A footer fit for a public picture: nothing that says where this headset is on the network. */
    static String footer(String footer) {
        return footer != null && footer.contains("://") ? "" : footer;
    }

    static synchronized void save(String kind, Bitmap bitmap) {
        if (!on || bitmap == null) return;
        long now = System.nanoTime();
        Long last = lastSave.get(kind);
        int n = count.getOrDefault(kind, 0);
        if ((last != null && now - last < SAVE_NS) || n >= MAX_PER_KIND) return;
        lastSave.put(kind, now);
        count.put(kind, n + 1);
        File dir = new File("/storage/emulated/0/Tremu/captures");
        if (!dir.isDirectory() && !dir.mkdirs()) return;
        File out = new File(dir, String.format(java.util.Locale.ROOT, "%s-%02d.png", kind, n + 1));
        try (FileOutputStream f = new FileOutputStream(out)) {
            bitmap.compress(Bitmap.CompressFormat.PNG, 100, f);
        } catch (Exception e) {
            android.util.Log.w("Tremu", "showcase: " + out + ": " + e);
        }
    }
}
