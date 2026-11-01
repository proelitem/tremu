package com.tremu.app;

import java.io.File;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * The player's Wii U folder: games, updates and DLC, recognized from their contents and grouped by
 * game. A title is a .wua archive (which can hold several) or a folder with a title.tmd; the kind
 * comes from its title id: 00050000 game, 0005000E update, 0005000C DLC.
 */
final class WiiuLibrary {
    static final File ROOT = new File("/storage/emulated/0/Download/Roms/wiiu");

    private static final int KIND_GAME = 0x00050000, KIND_UPDATE = 0x0005000E, KIND_DLC = 0x0005000C;

    private static final int WUA_FOOTER = 144, WUA_MAGIC = 0x169F52D6, WUA_ENTRY = 16;

    /** One title found in a file or folder. */
    private static final class Title {
        final long id;
        final int version;
        Title(long id, int version) { this.id = id; this.version = version; }
        int kind() { return (int) (id >>> 32); }
        int game() { return (int) id; }
    }

    /** A game and what belongs to it. file is null when only its update or DLC is there. */
    static final class Game {
        File file, updateFile, dlcFile;
        int updateVersion = -1;
    }

    private WiiuLibrary() {}

    /** Title id and version of a title.tmd: its header follows the 0x140-byte RSA-2048 signature. */
    private static Title readTmd(File tmd) throws IOException {
        try (RandomAccessFile f = new RandomAccessFile(tmd, "r")) {
            f.seek(0x18C);
            long id = f.readLong();
            f.seek(0x1DC);
            return new Title(id, f.readUnsignedShort());
        }
    }

    /** The titles of a .wua (a ZArchive): its top-level folders are named "<title id>_v<version>". */
    private static List<Title> readWua(File wua) throws IOException {
        List<Title> titles = new ArrayList<>();
        try (RandomAccessFile f = new RandomAccessFile(wua, "r")) {
            f.seek(f.length() - WUA_FOOTER);
            long[][] sections = new long[6][2];
            for (long[] s : sections) {
                s[0] = f.readLong();
                s[1] = f.readLong();
            }
            f.skipBytes(32 + 8 + 4);
            if (f.readInt() != WUA_MAGIC) return titles;
            long names = sections[2][0], tree = sections[3][0];
            f.seek(tree + 4);
            long first = f.readInt() & 0xFFFFFFFFL, count = f.readInt() & 0xFFFFFFFFL;
            for (long i = 0; i < count; i++) {
                f.seek(tree + (first + i) * WUA_ENTRY);
                int nameOffset = f.readInt();
                if (nameOffset < 0) continue;  /* the top bit marks a file, not a folder */
                f.seek(names + nameOffset);
                int length = f.readUnsignedByte();
                if ((length & 0x80) != 0) length = (length & 0x7F) | f.readUnsignedByte() << 7;
                byte[] name = new byte[length];
                f.readFully(name);
                Title t = parseFolderName(new String(name, StandardCharsets.UTF_8));
                if (t != null) titles.add(t);
            }
        }
        return titles;
    }

    private static Title parseFolderName(String name) {
        int v = name.indexOf("_v");
        if (v != 16) return null;
        try {
            return new Title(Long.parseUnsignedLong(name.substring(0, 16), 16), Integer.parseInt(name.substring(18)));
        } catch (NumberFormatException e) {
            return null;
        }
    }

    private static void note(Map<Integer, Game> games, Title t, File file) {
        Game g = games.get(t.game());
        if (g == null) {
            g = new Game();
            games.put(t.game(), g);
        }
        if (t.kind() == KIND_GAME) {
            g.file = file;
        } else if (t.kind() == KIND_UPDATE && t.version > g.updateVersion) {
            g.updateVersion = t.version;
            g.updateFile = file;
        } else if (t.kind() == KIND_DLC) {
            g.dlcFile = file;
        }
    }

    /** The significant words of a file name, without the (USA) and [id] tags. */
    private static java.util.Set<String> words(String name) {
        java.util.Set<String> set = new java.util.HashSet<>();
        for (String w : name.toLowerCase(Locale.ROOT).replaceAll("\\(.*?\\)|\\[.*?\\]|\\.wua$", " ").split("[^a-z0-9]+"))
            if (w.length() > 1 && !w.equals("the") && !w.equals("of")) set.add(w);
        return set;
    }

    /**
     * Some .wua files name their title 0000000000000000: its real id is in a compressed meta.xml.
     * Cemu reads it, so the game runs with its update anyway; here the name tells them apart.
     */
    private static void addUnnamed(Map<Integer, Game> games, List<File> unnamed) {
        int key = 0;
        for (File f : unnamed) {
            java.util.Set<String> mine = words(f.getName());
            Game match = null;
            for (Game g : games.values()) {
                if (g.file != null) continue;
                java.util.Set<String> theirs = words(g.updateFile != null ? g.updateFile.getName() : g.dlcFile.getName());
                if (mine.size() >= 2 && mine.equals(theirs)) match = g;
            }
            if (match == null) {
                match = new Game();
                games.put(--key, match);
            }
            match.file = f;
        }
    }

    private static void scan(File dir, int depth, Map<Integer, Game> games, List<File> unnamed) {
        File[] files = dir.listFiles();
        if (files == null) return;
        Arrays.sort(files);
        for (File f : files) {
            if (f.getName().startsWith(".")) continue;
            try {
                if (f.isDirectory()) {
                    File tmd = new File(f, "title.tmd");
                    if (tmd.isFile()) note(games, readTmd(tmd), f);
                    else if (depth < 2) scan(f, depth + 1, games, unnamed);
                } else if (f.getName().toLowerCase(Locale.ROOT).endsWith(".wua")) {
                    for (Title t : readWua(f)) {
                        if (t.id == 0) unnamed.add(f);
                        else note(games, t, f);
                    }
                }
            } catch (IOException e) {
                android.util.Log.w("Tremu", "Wii U title unreadable: " + f + ": " + e);
            }
        }
    }

    static List<Game> games() {
        Map<Integer, Game> games = new LinkedHashMap<>();
        List<File> unnamed = new ArrayList<>();
        scan(ROOT, 0, games, unnamed);
        addUnnamed(games, unnamed);
        return new ArrayList<>(games.values());
    }

    /** "<name>" of a game or, without its file, of what we have for it. */
    static String displayName(Game g) {
        File f = g.file != null ? g.file : g.updateFile != null ? g.updateFile : g.dlcFile;
        String name = f.getName();
        return f.isDirectory() ? name : name.substring(0, name.lastIndexOf('.'));
    }

    /** What the player sees for a game: "Mise à jour v208 + DLC", or that the update is missing. */
    static String status(Game g) {
        String update = g.updateVersion >= 0 ? Lang.f("Mise à jour v%d", g.updateVersion) : Lang.t("Mise à jour manquante");
        return g.dlcFile != null ? update + " + DLC" : update;
    }

    /** Carousel tiles, as path, name, status triples. */
    static String[] carouselRows() {
        List<String> rows = new ArrayList<>();
        for (Game g : games()) {
            if (g.file == null) continue;
            rows.add(g.file.getPath());
            rows.add(displayName(g));
            rows.add(status(g));
        }
        return rows.toArray(new String[0]);
    }

    /** Deletes a title of the Wii U folder, a whole folder for one in .app files. False if it is elsewhere. */
    static boolean delete(String path) {
        try {
            File f = new File(path).getCanonicalFile();
            if (!f.getPath().startsWith(ROOT.getCanonicalPath() + File.separator)) return false;
            return deleteTree(f);
        } catch (IOException e) {
            return false;
        }
    }

    private static boolean deleteTree(File f) {
        File[] children = f.isDirectory() ? f.listFiles() : null;
        if (children != null) for (File c : children) deleteTree(c);
        return f.delete();
    }

    /** The target of an upload: a .wua, or one file of a title folder (folder is its name). */
    static File uploadTarget(String folder, String name) {
        File dir = folder == null || folder.isEmpty() ? ROOT : new File(ROOT, folder);
        dir.mkdirs();
        return new File(dir, name);
    }
}
