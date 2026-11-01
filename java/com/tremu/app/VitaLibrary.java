package com.tremu.app;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipFile;

/** The player's PS Vita folder: a game is a .vpk or .zip holding sce_sys/param.sfo, run by our Vita3K app. */
final class VitaLibrary {
    static final File ROOT = new File("/storage/emulated/0/Download/Roms/vita");

    private VitaLibrary() {}

    /** The param.sfo of an archive, or null when it is not a Vita game. */
    private static byte[] paramSfo(File archive) {
        try (ZipFile zip = new ZipFile(archive)) {
            ZipEntry entry = zip.getEntry("sce_sys/param.sfo");
            if (entry == null || entry.getSize() > 1 << 16) return null;
            byte[] bytes = new byte[(int) entry.getSize()];
            try (InputStream in = zip.getInputStream(entry)) {
                int done = 0, n;
                while (done < bytes.length && (n = in.read(bytes, done, bytes.length - done)) > 0) done += n;
            }
            return bytes;
        } catch (IOException | RuntimeException e) {
            return null;
        }
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
