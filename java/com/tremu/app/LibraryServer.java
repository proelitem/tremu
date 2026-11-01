package com.tremu.app;

import android.content.Context;
import android.util.Log;
import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.URL;
import java.net.URLDecoder;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Enumeration;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * "Ajouter des jeux": a small web page served on the local network while Tremu runs.
 * From a PC or phone browser the player drops ROMs, BIOS and posters, or pastes a link that
 * the headset downloads itself. Files are checked (no programs, no web pages disguised as
 * games), sorted into Tremu/Roms, bios or Banner, then the carousel rescans.
 * Every request but the page itself needs the 4-digit code shown in the headset.
 */
final class LibraryServer {
    private static final String TAG = "Tremu-library";
    static final int PORT = 8080;
    private static final File ROOT = new File("/storage/emulated/0/Tremu");
    /** Archives the cores load next to a game; they belong in bios, not in the carousel. */
    private static final String[] SUPPORT = {"cchip.zip", "neogeo.zip", "pgm.zip", "decocass.zip", "isgsm.zip",
        "naomi.zip", "naomi2.zip", "awbios.zip", "hod2bios.zip", "scph1001.bin", "scph5501.bin", "scph7001.bin",
        "dc_boot.bin", "dc_flash.bin", "mpr-17933.bin", "sega_101.bin", "bios_CD_U.bin", "bios_CD_E.bin",
        "bios_CD_J.bin"};
    private static final String[] IMAGES = {".png", ".jpg", ".jpeg", ".webp"};
    /** Never stored, alone or inside an archive: programs and scripts. */
    private static final String[] DANGEROUS = {".exe", ".apk", ".msi", ".bat", ".cmd", ".com", ".scr", ".pif",
        ".js", ".jse", ".vbs", ".vbe", ".wsf", ".ps1", ".dll", ".lnk", ".jar", ".sh", ".hta", ".reg", ".url"};

    private static native void nativeLibraryChanged();

    private static ServerSocket server;
    private static String code;
    private static final Map<String, String> jobs = Collections.synchronizedMap(new HashMap<String, String>());

    /** Quick saves and high scores live in the app's private storage: listed as "Sauvegardes". */
    private static File savesDir;
    private static final String[] FOLDERS = {"Roms", "bios", "Banner", "Sauvegardes"};

    private LibraryServer() {}

    /** The folder behind a name the page sends, or null for anything else (no path tricks). */
    private static File folder(String name) {
        if ("Sauvegardes".equals(name)) return savesDir;
        for (String f : FOLDERS) if (f.equals(name)) return new File(ROOT, f);
        return null;
    }

    /** An existing file of one of our folders, or null. */
    private static File existing(String dir, String name) {
        File folder = folder(dir);
        String clean = cleanName(name);
        if (folder == null || clean.isEmpty() || !clean.equals(name)) return null;
        File f = new File(folder, clean);
        return f.isFile() ? f : null;
    }

    /* Transfers keep the headset awake (CPU and Wi-Fi) even when it sleeps on the table. */
    private static Context appContext;
    private static android.os.PowerManager.WakeLock wakeLock;
    private static android.net.wifi.WifiManager.WifiLock wifiLock;
    private static int transfers;

    private static synchronized void transferBegin() {
        if (transfers++ > 0 || appContext == null) return;
        try {
            android.os.PowerManager power = (android.os.PowerManager) appContext.getSystemService(Context.POWER_SERVICE);
            wakeLock = power.newWakeLock(android.os.PowerManager.PARTIAL_WAKE_LOCK, "tremu:transfer");
            wakeLock.acquire(6 * 3600 * 1000L);
            android.net.wifi.WifiManager wifi = (android.net.wifi.WifiManager) appContext.getSystemService(Context.WIFI_SERVICE);
            wifiLock = wifi.createWifiLock(android.net.wifi.WifiManager.WIFI_MODE_FULL_HIGH_PERF, "tremu:transfer");
            wifiLock.acquire();
        } catch (RuntimeException e) {
            Log.w(TAG, "No wake lock: " + e);
        }
    }

    private static synchronized void transferEnd() {
        if (--transfers > 0) return;
        transfers = 0;
        if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        if (wifiLock != null && wifiLock.isHeld()) wifiLock.release();
        wakeLock = null;
        wifiLock = null;
    }

    static synchronized void start(Context context) {
        if (server != null) return;
        if (context != null) appContext = context.getApplicationContext();
        if (context != null) savesDir = new File(context.getFilesDir(), "saves");
        if (code == null) code = String.format(Locale.ROOT, "%04d", new SecureRandom().nextInt(10000));
        try {
            server = new ServerSocket(PORT);
        } catch (IOException e) {
            Log.w(TAG, "Cannot listen on " + PORT + ": " + e);
            return;
        }
        final ServerSocket listening = server;
        Thread t = new Thread(() -> {
            while (!listening.isClosed()) {
                try {
                    Socket s = listening.accept();
                    Thread client = new Thread(() -> serve(s), "tremu-library-client");
                    client.setDaemon(true);
                    client.start();
                } catch (IOException e) {
                    if (!listening.isClosed()) Log.w(TAG, "accept: " + e);
                }
            }
        }, "tremu-library");
        t.setDaemon(true);
        t.start();
        Log.i(TAG, "Library page on " + address() + " (code " + code + ")");
    }

    static synchronized void stop() {
        if (server == null) return;
        try { server.close(); } catch (IOException ignored) {}
        server = null;
    }

    /** "http://192.168.1.20:8080" or null without a network; code() is asked by the page. */
    static String address() {
        try {
            for (Enumeration<NetworkInterface> e = NetworkInterface.getNetworkInterfaces(); e.hasMoreElements(); ) {
                NetworkInterface n = e.nextElement();
                if (!n.isUp() || n.isLoopback()) continue;
                for (Enumeration<InetAddress> a = n.getInetAddresses(); a.hasMoreElements(); ) {
                    InetAddress ip = a.nextElement();
                    if (ip instanceof Inet4Address && ip.isSiteLocalAddress())
                        return "http://" + ip.getHostAddress() + (PORT == 80 ? "" : ":" + PORT);
                }
            }
        } catch (Exception ignored) {}
        return null;
    }

    static String code() { return code; }

    // ---- HTTP ----

    private static void serve(Socket s) {
        try (Socket socket = s) {
            socket.setSoTimeout(60000);
            InputStream in = new BufferedInputStream(socket.getInputStream());
            OutputStream out = socket.getOutputStream();
            String requestLine = readLine(in);
            if (requestLine == null) return;
            Map<String, String> headers = new HashMap<>();
            for (String line; (line = readLine(in)) != null && !line.isEmpty(); ) {
                int colon = line.indexOf(':');
                if (colon > 0) headers.put(line.substring(0, colon).trim().toLowerCase(Locale.ROOT), line.substring(colon + 1).trim());
            }
            String[] parts = requestLine.split(" ");
            if (parts.length < 2) return;
            String method = parts[0], target = parts[1];
            String path = target.contains("?") ? target.substring(0, target.indexOf('?')) : target;
            Map<String, String> query = parseQuery(target.contains("?") ? target.substring(target.indexOf('?') + 1) : "");

            if (method.equals("GET") && path.equals("/")) {
                reply(out, 200, "text/html; charset=utf-8", LibraryPage.html());
                return;
            }
            /* Links and images cannot send headers: the code may come as ?k= instead. */
            if (!code.equals(headers.get("x-code")) && !code.equals(query.get("k"))) {
                reply(out, 403, "application/json", result(false, Lang.t("Code incorrect")));
                return;
            }
            if (method.equals("GET") && path.equals("/list")) {
                reply(out, 200, "application/json", listJson());
            } else if (method.equals("GET") && path.equals("/download")) {
                File f = existing(query.get("dir"), query.get("name"));
                if (f == null) reply(out, 404, "application/json", result(false, Lang.t("Fichier introuvable")));
                else sendFile(out, f, !"1".equals(query.get("inline")));
            } else if (method.equals("POST") && path.equals("/delete")) {
                File f = existing(query.get("dir"), query.get("name"));
                boolean ok = f != null && f.delete();
                if (ok) changed();
                reply(out, 200, "application/json", result(ok, ok ? Lang.f("%s supprimé", f.getName()) : Lang.t("Suppression impossible")));
            } else if (method.equals("POST") && path.equals("/rename")) {
                reply(out, 200, "application/json", rename(query.get("dir"), query.get("name"), query.get("todir"), query.get("to")));
            } else if (method.equals("GET") && path.equals("/wiiu")) {
                reply(out, 200, "application/json", wiiuJson());
            } else if (method.equals("POST") && path.equals("/wiiu/delete")) {
                boolean ok = WiiuLibrary.delete(query.get("path"));
                if (ok) changed();
                reply(out, 200, "application/json", result(ok, ok ? Lang.t("Supprimé") : Lang.t("Suppression impossible")));
            } else if (method.equals("PUT") && path.equals("/file")) {
                long length = Long.parseLong(headers.containsKey("content-length") ? headers.get("content-length") : "-1");
                String choice = "wiiu".equals(query.get("dir")) ? "wiiu/" + cleanName(query.get("sub")) : query.get("dir");
                String result = receive(in, length, query.get("name"), choice);
                reply(out, 200, "application/json", result);
            } else if (method.equals("POST") && path.equals("/fetch")) {
                reply(out, 200, "application/json", fetch(query.get("url"), query.get("dir")));
            } else {
                reply(out, 404, "application/json", result(false, Lang.t("Inconnu")));
            }
        } catch (Exception e) {
            Log.w(TAG, "request failed: " + e);
        }
    }

    private static String readLine(InputStream in) throws IOException {
        ByteArrayOutputStream line = new ByteArrayOutputStream();
        for (int c; (c = in.read()) != -1; ) {
            if (c == '\n') return line.toString("UTF-8").replace("\r", "");
            if (line.size() > 8192) return null;
            line.write(c);
        }
        return line.size() > 0 ? line.toString("UTF-8") : null;
    }

    private static Map<String, String> parseQuery(String q) throws IOException {
        Map<String, String> map = new HashMap<>();
        for (String pair : q.split("&")) {
            int eq = pair.indexOf('=');
            if (eq > 0) map.put(pair.substring(0, eq), URLDecoder.decode(pair.substring(eq + 1), "UTF-8"));
        }
        return map;
    }

    private static void reply(OutputStream out, int status, String type, String body) throws IOException {
        byte[] data = body.getBytes("UTF-8");
        String head = "HTTP/1.1 " + status + (status == 200 ? " OK" : " Error") + "\r\nContent-Type: " + type
            + "\r\nContent-Length: " + data.length + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
        out.write(head.getBytes("UTF-8"));
        out.write(data);
        out.flush();
    }

    private static String json(String s) {
        StringBuilder b = new StringBuilder("\"");
        for (char c : s.toCharArray()) {
            if (c == '"' || c == '\\') b.append('\\').append(c);
            else if (c < 0x20) b.append(String.format(Locale.ROOT, "\\u%04x", (int) c));
            else b.append(c);
        }
        return b.append('"').toString();
    }

    private static String result(boolean ok, String message) {
        return "{\"ok\":" + ok + ",\"message\":" + json(message) + "}";
    }

    // ---- Sorting and checks ----

    private static boolean endsWithAny(String name, String[] suffixes) {
        String lower = name.toLowerCase(Locale.ROOT);
        for (String s : suffixes) if (lower.endsWith(s)) return true;
        return false;
    }

    /** Keeps the base name only, without folders or odd characters. */
    private static String cleanName(String name) {
        if (name == null) return "";
        name = name.replace('\\', '/');
        name = name.substring(name.lastIndexOf('/') + 1).trim();
        return name.replaceAll("[^A-Za-z0-9 ._()\\[\\]&,'!+-]", "_");
    }

    /** "auto" picks Banner for images, bios for known support archives, else Roms. */
    private static String folderFor(String name, String choice) {
        if ("Roms".equals(choice) || "bios".equals(choice) || "Banner".equals(choice)) return choice;
        if (endsWithAny(name, IMAGES)) return "Banner";
        for (String s : SUPPORT) if (s.equalsIgnoreCase(name)) return "bios";
        return "Roms";
    }

    /** Null when the file may be kept, else the reason it is refused. */
    private static String check(File file, String name) {
        if (endsWithAny(name, DANGEROUS)) return Lang.t("Fichier refusé : c'est un programme, pas un jeu");
        byte[] head = new byte[512];
        int n = 0;
        try (InputStream in = new java.io.FileInputStream(file)) { n = Math.max(0, in.read(head)); } catch (IOException e) {
            return Lang.t("Lecture impossible");
        }
        String start = new String(head, 0, n, java.nio.charset.StandardCharsets.ISO_8859_1).trim().toLowerCase(Locale.ROOT);
        if (start.startsWith("<!doctype html") || start.startsWith("<html") || start.contains("<head"))
            return Lang.t("Refusé : c'est une page web, pas un fichier de jeu (le lien mène à une page de téléchargement)");
        if (n >= 2 && head[0] == 'M' && head[1] == 'Z') return Lang.t("Refusé : c'est un programme Windows déguisé");
        if (name.toLowerCase(Locale.ROOT).endsWith(".zip")) {
            int entries = 0;
            try (ZipInputStream zip = new ZipInputStream(new java.io.FileInputStream(file))) {
                for (ZipEntry e; (e = zip.getNextEntry()) != null; entries++)
                    if (endsWithAny(e.getName(), DANGEROUS)) return Lang.f("Refusé : l'archive contient un programme (%s)", cleanName(e.getName()));
            } catch (IOException e) {
                return Lang.t("Archive .zip illisible ou incomplète");
            }
            if (entries == 0) return Lang.t("Archive .zip vide ou illisible");
        }
        return null;
    }

    /** Disc images (PlayStation) the emulator reads directly; the .bin tracks come with a .cue. */
    private static final String[] DISCS = {".cue", ".chd", ".pbp", ".iso", ".cso", ".m3u", ".gdi", ".cdi"};
    /** Disc images of Namco System 246/256 sets: MAME layout, in a folder named after the set. */
    private static final String[][] NAMCO_DISCS = {{"tst1dvd0.chd", "timecrs3"}, {"tsf1-ha.chd", "timecrs4"},
        {"vpn1cd0.chd", "vnight"}, {"cbr1-ha.chd", "cobrata"}};
    /** Dreamcast / Naomi BIOS files: Flycast reads them from bios/dc. */
    private static final String[] DC_BIOS = {"naomi.zip", "naomi2.zip", "awbios.zip", "hod2bios.zip", "dc_boot.bin",
        "dc_flash.bin"};

    private static boolean isDiscArchive(File file, String name) {
        if (!name.toLowerCase(Locale.ROOT).endsWith(".zip")) return false;
        try (ZipInputStream zip = new ZipInputStream(new java.io.FileInputStream(file))) {
            for (ZipEntry e; (e = zip.getNextEntry()) != null; )
                if (endsWithAny(e.getName(), DISCS)) return true;
        } catch (IOException e) {
            return false;
        }
        return false;
    }

    /** Unpacks every file of a disc archive into dir; returns the game file name, or null. */
    private static String unzipDisc(File file, File dir) {
        String game = null;
        java.util.List<File> written = new java.util.ArrayList<>();
        try (ZipInputStream zip = new ZipInputStream(new java.io.FileInputStream(file))) {
            byte[] buf = new byte[65536];
            for (ZipEntry e; (e = zip.getNextEntry()) != null; ) {
                if (e.isDirectory()) continue;
                String name = cleanName(e.getName());
                if (name.isEmpty()) continue;
                File out = new File(dir, name);
                written.add(out);
                try (OutputStream o = new FileOutputStream(out)) {
                    for (int n; (n = zip.read(buf)) > 0; ) o.write(buf, 0, n);
                }
                if (endsWithAny(name, DISCS) && (game == null || !game.toLowerCase(Locale.ROOT).endsWith(".m3u"))) game = name;
            }
        } catch (IOException e) {
            Log.w(TAG, "Unzip failed: " + e);
            for (File f : written) f.delete();
            return null;
        }
        Log.i(TAG, "Unpacked " + written.size() + " files from " + file.getName() + " into " + dir);
        return game;
    }

    /** A Wii U title: its .wua, or one file of its folder of .app files. */
    private static String storeWiiu(File partial, String name, String folder) {
        if (!name.toLowerCase(Locale.ROOT).matches("title\\.(tmd|tik|cert)|[0-9a-f]{8}\\.(app|h3)|.*\\.wua")) {
            partial.delete();
            return result(false, Lang.f("%s : ce n'est pas un fichier de titre Wii U", name));
        }
        File target = WiiuLibrary.uploadTarget(folder, name);
        if (target.exists()) target.delete();
        if (!partial.renameTo(target)) {
            partial.delete();
            return result(false, Lang.f("%s : enregistrement impossible", name));
        }
        changed();
        return result(true, Lang.f("%s ajouté dans %s", name, "Download/Roms/wiiu"));
    }

    private static String wiiuJson() {
        StringBuilder b = new StringBuilder("{\"games\":[");
        boolean first = true;
        for (WiiuLibrary.Game g : WiiuLibrary.games()) {
            if (!first) b.append(',');
            first = false;
            b.append("{\"name\":").append(json(WiiuLibrary.displayName(g)))
                .append(",\"status\":").append(json(g.file == null ? Lang.t("Jeu manquant") + " — " + WiiuLibrary.status(g) : WiiuLibrary.status(g)))
                .append(",\"game\":").append(json(g.file == null ? "" : g.file.getPath()))
                .append(",\"update\":").append(json(g.updateFile == null ? "" : g.updateFile.getPath()))
                .append(",\"dlc\":").append(json(g.dlcFile == null ? "" : g.dlcFile.getPath())).append('}');
        }
        return b.append("]}").toString();
    }

    /** Checks a downloaded or uploaded .part file, then moves it into place. */
    private static String store(File partial, String name, String choice) {
        if (choice != null && choice.startsWith("wiiu/")) return storeWiiu(partial, name, choice.substring(5));
        if (name.toLowerCase(Locale.ROOT).endsWith(".wua")) return storeWiiu(partial, name, "");
        String reason = check(partial, name);
        if (reason != null) {
            partial.delete();
            Log.i(TAG, name + ": " + reason);
            return result(false, name + " : " + reason);
        }
        String folder = folderFor(name, choice);
        File dir = new File(ROOT, folder);
        for (String b : DC_BIOS) if ("bios".equals(folder) && b.equalsIgnoreCase(name)) dir = new File(dir, "dc");
        // A PS2 BIOS (4 to 8 MB, "SCPH-..." dumps) goes where LRPS2 looks; PS1 ones are 512 KB.
        long size = partial.length();
        if (!"Banner".equals(folder) && name.toLowerCase(Locale.ROOT).matches("(scph|ps2).*\\.(bin|rom0?)")
                && size >= 4L << 20 && size <= 8L << 20) {
            folder = "bios";
            dir = new File(new File(ROOT, "bios"), "pcsx2/bios");
        }
        for (String[] d : NAMCO_DISCS) if ("Roms".equals(folder) && d[0].equalsIgnoreCase(name)) dir = new File(dir, d[1]);
        dir.mkdirs();
        if ("Roms".equals(folder) && isDiscArchive(partial, name)) {
            // CD games cannot run zipped: unpacked here once (.cue + .bin tracks) instead of at each start.
            String unpacked = unzipDisc(partial, dir);
            partial.delete();
            changed();
            return unpacked == null ? result(false, Lang.f("%s : décompression impossible (espace libre ?)", name))
                    : result(true, Lang.f("%s décompressé dans Roms : %s", name, unpacked));
        }
        File target = new File(dir, name);
        if (target.exists()) target.delete();
        if (!partial.renameTo(target)) {
            partial.delete();
            return result(false, Lang.f("%s : enregistrement impossible", name));
        }
        Log.i(TAG, "Stored " + target);
        changed();
        return result(true, Lang.f("%s ajouté dans %s", name, folder));
    }

    private static String receive(InputStream in, long length, String rawName, String choice) throws IOException {
        transferBegin();
        try {
            return receiveFile(in, length, rawName, choice);
        } finally {
            transferEnd();
        }
    }

    private static String receiveFile(InputStream in, long length, String rawName, String choice) throws IOException {
        String name = cleanName(rawName);
        if (name.isEmpty() || length < 0) return result(false, Lang.t("Nom ou taille manquant"));
        File partial = new File(ROOT, "." + name + ".part");
        ROOT.mkdirs();
        try (OutputStream file = new FileOutputStream(partial)) {
            byte[] buf = new byte[65536];
            long left = length;
            while (left > 0) {
                int n = in.read(buf, 0, (int) Math.min(buf.length, left));
                if (n < 0) break;
                file.write(buf, 0, n);
                left -= n;
            }
            if (left > 0) {
                partial.delete();
                return result(false, Lang.f("%s : envoi interrompu", name));
            }
        }
        return store(partial, name, choice);
    }

    // ---- Links: the headset downloads, the PC never visits the site ----

    private static String fetch(String link, String choice) {
        if (link == null || !(link.startsWith("http://") || link.startsWith("https://")))
            return result(false, Lang.t("Lien invalide (http:// ou https://)"));
        String id = Long.toString(System.nanoTime());
        jobs.put(id, Lang.f("Démarrage : %s", link));
        Thread t = new Thread(() -> {
            transferBegin();
            try {
                jobs.put(id, download(link, choice, id));
            } finally {
                transferEnd();
            }
        }, "tremu-fetch");
        t.setDaemon(true);
        t.start();
        return result(true, Lang.t("Téléchargement lancé sur le casque"));
    }

    private static String download(String link, String choice, String id) {
        HttpURLConnection http = null;
        File partial = null;
        try {
            URL url = new URL(link);
            for (int redirects = 0; ; redirects++) {
                http = (HttpURLConnection) url.openConnection();
                http.setInstanceFollowRedirects(false);
                http.setConnectTimeout(10000);
                http.setReadTimeout(30000);
                http.setRequestProperty("User-Agent", "Tremu");
                int status = http.getResponseCode();
                if (status / 100 == 3 && redirects < 5 && http.getHeaderField("Location") != null) {
                    url = new URL(url, http.getHeaderField("Location"));
                    http.disconnect();
                    continue;
                }
                if (status != 200) return "✗ " + link + " : erreur " + status;
                break;
            }
            String type = http.getContentType() == null ? "" : http.getContentType().toLowerCase(Locale.ROOT);
            if (type.startsWith("text/html"))
                return "✗ Ce lien mène à une page web, pas à un fichier : copie le lien direct du fichier";
            String name = null, disposition = http.getHeaderField("Content-Disposition");
            if (disposition != null && disposition.contains("filename=")) {
                name = disposition.substring(disposition.indexOf("filename=") + 9).replace("\"", "");
                if (name.contains(";")) name = name.substring(0, name.indexOf(';'));
            }
            if (name == null || name.trim().isEmpty()) name = URLDecoder.decode(url.getPath(), "UTF-8");
            name = cleanName(name);
            if (name.isEmpty()) return "✗ " + Lang.t("Nom de fichier introuvable dans le lien");
            if (endsWithAny(name, DANGEROUS)) return "✗ " + Lang.f("%s : refusé, c'est un programme", name);
            long total = http.getContentLengthLong();
            partial = new File(ROOT, "." + name + ".part");
            ROOT.mkdirs();
            try (InputStream in = http.getInputStream(); OutputStream out = new FileOutputStream(partial)) {
                byte[] buf = new byte[65536];
                long done = 0, lastReport = 0;
                for (int n; (n = in.read(buf)) > 0; ) {
                    out.write(buf, 0, n);
                    done += n;
                    if (done - lastReport > 1 << 20) {
                        lastReport = done;
                        jobs.put(id, name + " : " + (total > 0 ? done * 100 / total + " %" : done / (1 << 20) + " " + Lang.t("Mo")));
                    }
                }
                if (total > 0 && done < total) return "✗ " + Lang.f("%s : téléchargement interrompu", name);
            }
            String stored = store(partial, name, choice);
            partial = null;
            boolean ok = stored.startsWith("{\"ok\":true");
            String message = stored.substring(stored.indexOf("\"message\":\"") + 11, stored.length() - 2);
            return (ok ? "✓ " : "✗ ") + message;
        } catch (Exception e) {
            return "✗ " + link + " : " + e.getMessage();
        } finally {
            if (http != null) http.disconnect();
            if (partial != null) partial.delete();
        }
    }

    // ---- Listing ----

    private static String listJson() {
        StringBuilder b = new StringBuilder("{\"folders\":{");
        for (int k = 0; k < FOLDERS.length; k++) {
            File dir = folder(FOLDERS[k]);
            File[] files = dir == null ? null : dir.listFiles();
            List<File> list = new ArrayList<>();
            if (files != null) for (File f : files) if (f.isFile() && !f.getName().startsWith(".")) list.add(f);
            Collections.sort(list, (x, y) -> x.getName().compareToIgnoreCase(y.getName()));
            b.append(k > 0 ? "," : "").append(json(FOLDERS[k])).append(":[");
            for (int i = 0; i < list.size(); i++)
                b.append(i > 0 ? "," : "").append("{\"n\":").append(json(list.get(i).getName()))
                    .append(",\"s\":").append(list.get(i).length()).append('}');
            b.append(']');
        }
        b.append("},\"free\":").append(ROOT.getUsableSpace()).append(",\"total\":").append(ROOT.getTotalSpace());
        b.append(",\"jobs\":[");
        synchronized (jobs) {
            List<String> ids = new ArrayList<>(jobs.keySet());
            Collections.sort(ids, Collections.reverseOrder());
            for (int i = 0; i < ids.size() && i < 10; i++) b.append(i > 0 ? "," : "").append(json(jobs.get(ids.get(i))));
        }
        return b.append("]}").toString();
    }

    private static void changed() {
        try { nativeLibraryChanged(); } catch (UnsatisfiedLinkError ignored) {}
    }

    /** Renames a file and/or moves it to another of our folders. */
    private static String rename(String dir, String name, String toDir, String to) {
        String destination = toDir == null ? dir : toDir;
        File source = existing(dir, name);
        File folder = folder(destination);
        String clean = cleanName(to == null ? name : to);
        if (source == null || folder == null) return result(false, Lang.t("Fichier introuvable"));
        if (clean.isEmpty() || endsWithAny(clean, DANGEROUS)) return result(false, Lang.t("Nom refusé"));
        File target = new File(folder, clean);
        if (target.exists()) return result(false, Lang.f("%s existe déjà dans %s", clean, destination));
        folder.mkdirs();
        if (!source.renameTo(target)) return result(false, Lang.t("Déplacement impossible"));
        changed();
        return result(true, name + " → " + destination + "/" + clean);
    }

    /** Streams a file to the browser (attachment = saved by the browser, else shown, e.g. a poster). */
    private static void sendFile(OutputStream out, File f, boolean attachment) throws IOException {
        String lower = f.getName().toLowerCase(Locale.ROOT);
        String type = lower.endsWith(".png") ? "image/png" : lower.endsWith(".jpg") || lower.endsWith(".jpeg") ? "image/jpeg"
            : lower.endsWith(".webp") ? "image/webp" : "application/octet-stream";
        String head = "HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nContent-Length: " + f.length()
            + (attachment ? "\r\nContent-Disposition: attachment; filename=\"" + f.getName().replace("\"", "") + "\"" : "")
            + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
        out.write(head.getBytes("UTF-8"));
        try (InputStream in = new java.io.FileInputStream(f)) {
            byte[] buf = new byte[65536];
            for (int n; (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
        }
        out.flush();
    }
}
