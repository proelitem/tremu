package com.tremu.app;

import android.content.Context;
import android.util.Log;
import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.net.URLEncoder;
import java.util.HashMap;
import java.util.Map;

/**
 * Arcade titles and missing posters. assets/arcade.tsv (built by tools/make_artmap.py) maps
 * each FBNeo set to its full title and to an image of the free libretro thumbnail server.
 * A game without a poster in Tremu/Banner gets one downloaded once. Consoles: the box, then
 * Wikipedia, then the title screen. Arcade: Wikipedia, whose lead image is the flyer's front,
 * then the title screen; the server's "boxart" comes last because it is often the flyer's back.
 */
final class Posters {
    private static final String TAG = "Tremu-posters";
    private static final String SERVER = "https://thumbnails.libretro.com/";
    private static final String WIKIPEDIA = "https://en.wikipedia.org/w/api.php";
    private static final String USER_AGENT = "Tremu/0.1 (game poster lookup)";

    /** Tells the native carousel that a new poster file exists. */
    private static native void nativePosterReady();

    private static Map<String, String[]> table;
    private static Thread worker;

    private Posters() {}

    private static synchronized Map<String, String[]> table(Context context) {
        if (table != null) return table;
        table = new HashMap<>();
        try (InputStream in = context.getAssets().open("arcade.tsv");
             BufferedReader reader = new BufferedReader(new InputStreamReader(in, "UTF-8"))) {
            for (String line; (line = reader.readLine()) != null; ) {
                String[] f = line.split("\t", -1);
                if (f.length == 3) table.put(f[0], new String[] {f[1], f[2]});
            }
        } catch (Exception e) {
            Log.w(TAG, "No arcade table: " + e);
        }
        return table;
    }

    /** Display title of an arcade set ("Terminator 2 - Judgment Day"), or null when unknown. */
    static String title(Context context, String name) {
        String[] entry = table(context).get(name.toLowerCase());
        if (entry == null) return null;
        int paren = entry[0].indexOf(" (");
        return paren > 0 ? entry[0].substring(0, paren) : entry[0];
    }

    /**
     * Downloads, in the background, the posters missing from dir. Each name is
     * "libretro-thumbnails system\tfile name\ttitle", with an empty system for an arcade set
     * ("Sony - PlayStation\tTime Crisis (USA)\tTime Crisis (PS1)", "\tterm2\tTerminator 2").
     */
    static synchronized void fetchMissing(Context context, String[] names, String dir) {
        if (worker != null && worker.isAlive()) return;
        Map<String, String[]> map = table(context);
        worker = new Thread(() -> {
            for (String name : names) {
                String[] request = name.split("\t", -1);
                if (request.length != 3) continue;
                String system = request[0], file = request[1], title = request[2];
                boolean arcade = system.isEmpty();
                File target = new File(dir, file + ".png");
                if (target.exists()) continue;
                boolean found = false;
                if (!arcade) found = fromServer(boxart(system, file), file, target);
                if (!found) found = fromWikipedia(title, arcade, file, dir);
                if (!found && !arcade) found = fromServer(boxart(system, file).replace("/Named_Boxarts/", "/Named_Titles/"), file, target);
                if (!found && arcade) {
                    String[] entry = map.get(file.toLowerCase());
                    /* Sets missing from the table (MAME's) may be named after their title. */
                    String box = entry != null && !entry[1].isEmpty() ? entry[1] : "MAME/Named_Boxarts/" + title + ".png";
                    found = fromServer(box.replace("/Named_Boxarts/", "/Named_Titles/"), file, target) || fromServer(box, file, target);
                }
                if (found) try { nativePosterReady(); } catch (UnsatisfiedLinkError ignored) {}
            }
        }, "tremu-posters");
        worker.start();
    }

    /** The server names images like the No-Intro / Redump files, with its reserved characters as _. */
    private static String boxart(String system, String file) {
        String image = file.replaceAll("\\.xiso$", "").replaceAll("[&*/:`<>?\\\\|\"]", "_");
        return system + "/Named_Boxarts/" + image + ".png";
    }

    private static boolean fromServer(String path, String file, File target) {
        try {
            StringBuilder url = new StringBuilder(SERVER);
            for (String segment : path.split("/")) {
                if (url.length() > SERVER.length()) url.append('/');
                url.append(URLEncoder.encode(segment, "UTF-8").replace("+", "%20"));
            }
            if (!download(url.toString(), target)) return false;
            Log.i(TAG, "Poster for " + file + ": " + path);
            return true;
        } catch (java.io.UnsupportedEncodingException e) {
            return false;
        }
    }

    /** The significant words of a title, without its (tags): the same for "Legend of Zelda, The" and "The Legend of Zelda". */
    private static java.util.Set<String> words(String title) {
        java.util.Set<String> set = new java.util.HashSet<>();
        for (String w : title.toLowerCase().replaceAll("\\(.*?\\)", " ").split("[^a-z0-9]+"))
            if (!w.isEmpty() && !w.equals("the") && !w.equals("of") && !w.equals("a")) set.add(w);
        return set;
    }

    /**
     * The lead image of the game's Wikipedia page: only pages with a video game infobox whose title
     * has exactly the game's words, so a place or a film of the same name is never taken.
     */
    private static boolean fromWikipedia(String title, boolean arcade, String file, String dir) {
        HttpURLConnection http = null;
        try {
            String words = title.replaceAll("\\(.*?\\)", " ").replaceAll("[^\\p{L}\\p{N}]+", " ").trim();
            String search = words + " hastemplate:\"Infobox video game\"";
            String query = WIKIPEDIA + "?action=query&format=json&generator=search&gsrnamespace=0&gsrlimit=15&redirects=1"
                + "&prop=pageimages&piprop=thumbnail&pithumbsize=768&pilicense=any&gsrsearch=" + URLEncoder.encode(search, "UTF-8");
            http = (HttpURLConnection) new URL(query).openConnection();
            http.setRequestProperty("User-Agent", USER_AGENT);
            http.setConnectTimeout(8000);
            http.setReadTimeout(15000);
            if (http.getResponseCode() != 200) return false;
            StringBuilder body = new StringBuilder();
            try (BufferedReader reader = new BufferedReader(new InputStreamReader(http.getInputStream(), "UTF-8"))) {
                for (String line; (line = reader.readLine()) != null; ) body.append(line);
            }
            org.json.JSONObject result = new org.json.JSONObject(body.toString()).optJSONObject("query");
            org.json.JSONObject pages = result == null ? null : result.optJSONObject("pages");
            if (pages == null) return false;
            java.util.Set<String> mine = words(title);
            String best = null;
            int bestScore = Integer.MAX_VALUE;
            for (java.util.Iterator<String> it = pages.keys(); it.hasNext(); ) {
                org.json.JSONObject page = pages.getJSONObject(it.next());
                org.json.JSONObject thumbnail = page.optJSONObject("thumbnail");
                String name = page.optString("title");
                if (thumbnail == null || !words(name).equals(mine)) continue;
                /* An arcade game's own page first, then the oldest; a console game's plain title first. */
                java.util.regex.Matcher tag = java.util.regex.Pattern.compile("\\((.*?)\\)").matcher(name);
                String paren = tag.find() ? tag.group(1).toLowerCase() : "";
                int score = page.optInt("index", 99);
                /* A page about a home version ("SNES video game") shows the wrong game. */
                if (arcade && !paren.isEmpty() && !paren.contains("arcade") && !paren.matches("^((19|20)[0-9]{2} )?video game$")) continue;
                if (arcade && paren.contains("arcade")) score -= 2000;
                else if (arcade && paren.matches("^(19[0-9]{2})\\b.*")) score -= 1000;
                else if (!arcade && paren.isEmpty()) score -= 2000;
                if (score < bestScore) {
                    bestScore = score;
                    best = thumbnail.optString("source");
                }
            }
            if (best == null) return false;
            String path = best.contains("?") ? best.substring(0, best.indexOf('?')) : best;
            String extension = path.toLowerCase().endsWith(".png") ? ".png" : ".jpg";
            if (!download(best, new File(dir, file + extension))) return false;
            Log.i(TAG, "Poster for " + file + ": " + best);
            return true;
        } catch (Exception e) {
            Log.w(TAG, "Wikipedia lookup failed for " + title + ": " + e);
            return false;
        } finally {
            if (http != null) http.disconnect();
        }
    }

    private static boolean download(String url, File target) {
        File partial = new File(target.getPath() + ".part");
        HttpURLConnection http = null;
        try {
            http = (HttpURLConnection) new URL(url).openConnection();
            http.setRequestProperty("User-Agent", USER_AGENT);
            http.setConnectTimeout(8000);
            http.setReadTimeout(15000);
            if (http.getResponseCode() != 200) {
                Log.i(TAG, "No poster at " + url + " (" + http.getResponseCode() + ")");
                return false;
            }
            target.getParentFile().mkdirs();
            try (InputStream in = http.getInputStream(); OutputStream out = new FileOutputStream(partial)) {
                byte[] buf = new byte[16384];
                for (int n; (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
            }
            return partial.renameTo(target);
        } catch (Exception e) {
            Log.w(TAG, "Poster download failed: " + e);
            partial.delete();
            return false;
        } finally {
            if (http != null) http.disconnect();
        }
    }
}
