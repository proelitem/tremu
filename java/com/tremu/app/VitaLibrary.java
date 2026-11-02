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

/** The player's PS Vita folder: a game is a .vpk or .zip holding sce_sys/param.sfo, run by our Vita3K app. */
final class VitaLibrary {
    static final File ROOT = new File("/storage/emulated/0/Download/Roms/vita");

    private VitaLibrary() {}

    /**
     * The param.sfo of an archive, or null when it is not a Vita game: at the root of a .vpk, under
     * app/<title id>/ in a dump of a whole game. Read by hand from the zip's directory: Android's ZipFile
     * refuses some dumps ("invalid CEN header") that Vita3K installs without trouble.
     */
    private static byte[] paramSfo(File archive) {
        try (RandomAccessFile f = new RandomAccessFile(archive, "r")) {
            int tail = (int) Math.min(f.length(), 65557);
            byte[] end = new byte[tail];
            f.seek(f.length() - tail);
            f.readFully(end);
            ByteBuffer e = ByteBuffer.wrap(end).order(ByteOrder.LITTLE_ENDIAN);
            int at = tail - 22;
            while (at >= 0 && e.getInt(at) != 0x06054b50) at--;
            if (at < 0) return null;
            long size = e.getInt(at + 12) & 0xFFFFFFFFL, offset = e.getInt(at + 16) & 0xFFFFFFFFL;
            // Zip64: the record just before holds the real values.
            if (offset == 0xFFFFFFFFL && at >= 20 && e.getInt(at - 20) == 0x07064b50) {
                byte[] record = new byte[56];
                f.seek(e.getLong(at - 12));
                f.readFully(record);
                ByteBuffer r = ByteBuffer.wrap(record).order(ByteOrder.LITTLE_ENDIAN);
                size = r.getLong(40);
                offset = r.getLong(48);
            }
            if (size > 64 << 20) return null;
            byte[] directory = new byte[(int) size];
            f.seek(offset);
            f.readFully(directory);
            ByteBuffer d = ByteBuffer.wrap(directory).order(ByteOrder.LITTLE_ENDIAN);
            for (int p = 0; p + 46 <= directory.length && d.getInt(p) == 0x02014b50;) {
                int nameLength = d.getShort(p + 28) & 0xFFFF, extra = d.getShort(p + 30) & 0xFFFF, comment = d.getShort(p + 32) & 0xFFFF;
                String name = new String(directory, p + 46, nameLength, StandardCharsets.UTF_8).toLowerCase(Locale.ROOT);
                if (name.equals("sce_sys/param.sfo") || (name.startsWith("app/") && name.endsWith("/sce_sys/param.sfo")))
                    return readEntry(f, d, p, extra, nameLength);
                p += 46 + nameLength + extra + comment;
            }
        } catch (IOException | RuntimeException | java.util.zip.DataFormatException e) {
            android.util.Log.w("Tremu", "PS Vita archive " + archive + ": " + e);
        }
        return null;
    }

    private static byte[] readEntry(RandomAccessFile f, ByteBuffer d, int p, int extra, int nameLength)
            throws IOException, java.util.zip.DataFormatException {
        int method = d.getShort(p + 10) & 0xFFFF;
        long packed = d.getInt(p + 20) & 0xFFFFFFFFL, unpacked = d.getInt(p + 24) & 0xFFFFFFFFL, local = d.getInt(p + 42) & 0xFFFFFFFFL;
        // Zip64 extra field: a value kept there replaces each of these three, when it is all ones, in this order.
        for (int x = p + 46 + nameLength, stop = x + extra; x + 4 <= stop;) {
            int id = d.getShort(x) & 0xFFFF, length = d.getShort(x + 2) & 0xFFFF, v = x + 4;
            if (id == 1) {
                if (unpacked == 0xFFFFFFFFL) { unpacked = d.getLong(v); v += 8; }
                if (packed == 0xFFFFFFFFL) { packed = d.getLong(v); v += 8; }
                if (local == 0xFFFFFFFFL) local = d.getLong(v);
            }
            x += 4 + length;
        }
        if (unpacked > 1 << 16 || packed > 1 << 17) return null;
        byte[] header = new byte[30];
        f.seek(local);
        f.readFully(header);
        ByteBuffer h = ByteBuffer.wrap(header).order(ByteOrder.LITTLE_ENDIAN);
        f.seek(local + 30 + (h.getShort(26) & 0xFFFF) + (h.getShort(28) & 0xFFFF));
        byte[] in = new byte[(int) packed], out = new byte[(int) unpacked];
        f.readFully(in);
        if (method == 0) return in;
        java.util.zip.Inflater inflater = new java.util.zip.Inflater(true);
        inflater.setInput(in);
        inflater.inflate(out);
        inflater.end();
        return out;
    }

    /** The Touch mapping of the "console controls" page, as saved in the settings; null before any save. */
    static int[] padMap(File settings) {
        try (java.io.BufferedReader in = new java.io.BufferedReader(new java.io.FileReader(settings))) {
            for (String line; (line = in.readLine()) != null;) {
                if (!line.startsWith("pad.vita=")) continue;
                String[] values = line.substring(9).split(",");
                int[] map = new int[values.length];
                for (int i = 0; i < map.length; i++) map[i] = Integer.parseInt(values[i].trim());
                return map;
            }
        } catch (IOException | RuntimeException e) {
            // No settings yet: Vita3K keeps the same defaults.
        }
        return null;
    }

    static String titleId(File archive) {
        byte[] sfo = paramSfo(archive);
        return sfo == null ? null : Ps3Library.sfoValue(sfo, "TITLE_ID");
    }

    /** Path, title and id of each game, three entries per game, sorted by title. */
    static String[] carouselRows() {
        File[] entries = ROOT.listFiles();
        if (entries == null) return new String[0];
        List<String[]> games = new ArrayList<>();
        for (File e : entries) {
            String name = e.getName(), lower = name.toLowerCase(Locale.ROOT);
            if (!e.isFile() || !(lower.endsWith(".vpk") || lower.endsWith(".zip"))) continue;
            byte[] sfo = paramSfo(e);
            if (sfo == null) continue;
            String title = Ps3Library.sfoValue(sfo, "STITLE"), id = Ps3Library.sfoValue(sfo, "TITLE_ID");
            if (title == null || title.isEmpty()) title = name.substring(0, name.length() - 4);
            games.add(new String[] {e.getPath(), title, id == null ? "" : id});
        }
        games.sort((a, b) -> a[1].compareToIgnoreCase(b[1]));
        List<String> rows = new ArrayList<>();
        for (String[] g : games) rows.addAll(Arrays.asList(g));
        return rows.toArray(new String[0]);
    }
}
