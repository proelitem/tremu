package com.tremu.app;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;

/**
 * The player's PS3 folder: a game is a disc image (.iso) or a folder dump holding
 * PS3_GAME/PARAM.SFO, which also gives its title and id. ARMSX3 boots either as it is.
 */
final class Ps3Library {
    static final File ROOT = new File("/storage/emulated/0/Download/Roms/ps3");

    private static final int SFO_MAGIC = 0x46535000;  // "\0PSF", read little-endian

    private Ps3Library() {}

    /** A file or folder of dir named name, whatever its case ("PS3_GAME" is as likely "ps3_game"). */
    private static File child(File dir, String name) {
        File[] all = dir.listFiles();
        if (all != null) for (File f : all) if (f.getName().equalsIgnoreCase(name)) return f;
        return null;
    }

    /** The text value of a key of a PARAM.SFO (TITLE, TITLE_ID), or null. */
    private static String readSfo(File sfo, String key) {
        try (RandomAccessFile f = new RandomAccessFile(sfo, "r")) {
            byte[] bytes = new byte[(int) Math.min(f.length(), 1 << 16)];
            f.readFully(bytes);
            return sfoValue(bytes, key);
        } catch (IOException e) {
            // A damaged or truncated file: the folder name stands for the title.
        }
        return null;
    }

    /** The text value of a key of PARAM.SFO's bytes (the PS Vita's format is the same), or null. */
    static String sfoValue(byte[] bytes, String key) {
        try {
            ByteBuffer b = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN);
            if (b.getInt(0) != SFO_MAGIC) return null;
            int keys = b.getInt(8), data = b.getInt(12), count = b.getInt(16);
            for (int i = 0; i < count; i++) {
                int entry = 20 + i * 16;
                int at = keys + (b.getShort(entry) & 0xFFFF);
                int end = at;
                while (end < bytes.length && bytes[end] != 0) end++;
                if (!key.equals(new String(bytes, at, end - at, StandardCharsets.US_ASCII))) continue;
                int length = b.getInt(entry + 4), offset = data + b.getInt(entry + 12);
                while (length > 0 && bytes[offset + length - 1] == 0) length--;
                return new String(bytes, offset, length, StandardCharsets.UTF_8).replace('\n', ' ').trim();
            }
        } catch (RuntimeException e) {
            // Damaged: the caller falls back on the file's name.
        }
        return null;
    }

    /** Path, title and id (or "") of each game, three entries per game, sorted by title. */
    static String[] carouselRows() {
        File[] entries = ROOT.listFiles();
        if (entries == null) return new String[0];
        List<String[]> games = new ArrayList<>();
        for (File e : entries) {
            String name = e.getName();
            if (name.startsWith(".")) continue;
            if (e.isFile() && name.toLowerCase(Locale.ROOT).endsWith(".iso")) {
                games.add(new String[] {e.getPath(), name.substring(0, name.length() - 4), ""});
            } else if (e.isDirectory()) {
                File game = child(e, "PS3_GAME");
                File sfo = game == null ? null : child(game, "PARAM.SFO");
                if (sfo == null) continue;
                String title = readSfo(sfo, "TITLE"), id = readSfo(sfo, "TITLE_ID");
                games.add(new String[] {e.getPath(), title == null || title.isEmpty() ? name : title, id == null ? "" : id});
            }
        }
        games.sort((a, b) -> a[1].compareToIgnoreCase(b[1]));
        List<String> rows = new ArrayList<>();
        for (String[] g : games) rows.addAll(Arrays.asList(g));
        return rows.toArray(new String[0]);
    }
}
