package com.tremu.app;

import android.app.NativeActivity;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import android.os.Bundle;

/**
 * NativeActivity host for Tremu. Adds what the native side cannot do alone:
 * Bluetooth access to the WonderFitter gun and text rendering for the options menu.
 */
public final class TremuActivity extends NativeActivity {
    static {
        // NativeActivity loads the library without registering it with this class loader;
        // loading it here lets WonderFitterGun's native methods resolve.
        System.loadLibrary("tremu");
    }

    private static final String[] GUN_PERMISSIONS = {
        "android.permission.BLUETOOTH_SCAN", "android.permission.BLUETOOTH_CONNECT"};

    private static TremuActivity instance;

    @Override protected void onCreate(Bundle state) {
        moveEarlierFiles();  // before the native side starts and creates the folder
        super.onCreate(state);
        instance = this;
        intentGame = getIntent().getStringExtra("tremu_game");
        if (!gunPermitted()) requestPermissions(GUN_PERMISSIONS, 0x5746);
        askForFiles();
    }

    private boolean askedForFiles;

    /** Tremu/ (BIOS, posters, saves) needs access to all files, which a plain install does not give. */
    private void askForFiles() {
        if (android.os.Environment.isExternalStorageManager()) return;
        askedForFiles = true;
        android.net.Uri app = android.net.Uri.parse("package:" + getPackageName());
        try {
            startActivity(new android.content.Intent(
                    android.provider.Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, app));
        } catch (android.content.ActivityNotFoundException e) {
            android.util.Log.w("Tremu", "no screen to ask for access to all files: " + e);
        }
    }

    /** Back from that screen with the access: start again, the native side has found no Tremu/. */
    private void restartIfGivenFiles() {
        if (!askedForFiles || !android.os.Environment.isExternalStorageManager()) return;
        askedForFiles = false;
        restartAfterExit();
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    /**
     * Moves what an earlier version (named WFGEmu) left: the shared folder, which on the same storage a
     * rename moves at once, and the settings file in the app's private files.
     */
    private void moveEarlierFiles() {
        java.io.File old = new java.io.File("/storage/emulated/0/WFGEmu");
        java.io.File now = new java.io.File("/storage/emulated/0/Tremu");
        if (old.isDirectory() && !now.exists())
            android.util.Log.i("Tremu", "shared folder renamed to Tremu: " + old.renameTo(now));
        java.io.File oldCfg = new java.io.File(getFilesDir(), "wfgemu.cfg");
        java.io.File newCfg = new java.io.File(getFilesDir(), "tremu.cfg");
        if (oldCfg.isFile() && !newCfg.exists())
            android.util.Log.i("Tremu", "settings file renamed to tremu.cfg: " + oldCfg.renameTo(newCfg));
        java.io.File oldDriver = new java.io.File(getFilesDir(), "drivers/WFG-Turnip");
        java.io.File newDriver = new java.io.File(getFilesDir(), "drivers/Tremu-Turnip");
        if (oldDriver.isDirectory() && !newDriver.exists()) oldDriver.renameTo(newDriver);
    }

    /** Before quitting: RestartActivity opens the app again once this process has ended. */
    public static void restartAfterExit() {
        instance.startActivity(new android.content.Intent(instance, RestartActivity.class)
                .putExtra("pid", android.os.Process.myPid())
                .addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK));
    }

    private static String intentGame;

    /** Game to start right after the splash, from "am start ... --es tremu_game <path>" (tests without
     *  the headset on); returned once. */
    public static String takeIntentGame() {
        String game = intentGame;
        intentGame = null;
        return game;
    }

    /** Decodes an image packaged in the APK assets, scaled to width x height. ARGB, top row first. */
    public static int[] loadAsset(String name, int width, int height) {
        try (java.io.InputStream in = instance.getAssets().open(name)) {
            Bitmap decoded = android.graphics.BitmapFactory.decodeStream(in);
            Bitmap scaled = Bitmap.createScaledBitmap(decoded, width, height, true);
            int[] pixels = new int[width * height];
            scaled.getPixels(pixels, 0, width, 0, 0, width, height);
            return pixels;
        } catch (Exception e) {
            return null;
        }
    }

    /** Native code can only reach this class (its loader), so carousel drawing is exposed here. */
    public static int[] renderCarousel(String[] titles, String[] banners, float position, int selected, String console, String footer,
                                       int tab, int barItem, String search, int width, int height) {
        return Carousel.render(titles, banners, position, selected, console, footer, tab, barItem, search, width, height);
    }

    /** Full title of an arcade set from the bundled table, or null. */
    public static int[] renderLoading(String title, String banner, String status, float angle, int width, int height) {
        return Carousel.renderLoading(title, banner, status, angle, width, height);
    }

    public static String arcadeTitle(String name) {
        return Posters.title(instance, name);
    }

    /**
     * Unpacks a zip of the APK assets into dir (Dolphin's Sys folder, Play!'s arcade definitions):
     * the first time, and again after each APK update.
     */
    public static boolean unpackAsset(String asset, String dir) {
        java.io.File sys = new java.io.File(dir);
        java.io.File stamp = new java.io.File(sys, ".tremu-version");
        String version = Long.toString(instance.getApplicationInfo().sourceDir.length() > 0
                ? new java.io.File(instance.getApplicationInfo().sourceDir).lastModified() : 0);
        try {
            if (stamp.exists() && version.equals(new String(java.nio.file.Files.readAllBytes(stamp.toPath()), "UTF-8")))
                return true;
            int files = 0;
            try (java.util.zip.ZipInputStream zip = new java.util.zip.ZipInputStream(instance.getAssets().open(asset))) {
                byte[] buf = new byte[65536];
                for (java.util.zip.ZipEntry e; (e = zip.getNextEntry()) != null; ) {
                    java.io.File out = new java.io.File(sys, e.getName());
                    if (!out.getCanonicalPath().startsWith(sys.getCanonicalPath())) continue;
                    if (e.isDirectory()) { out.mkdirs(); continue; }
                    out.getParentFile().mkdirs();
                    try (java.io.OutputStream o = new java.io.FileOutputStream(out)) {
                        for (int n; (n = zip.read(buf)) > 0; ) o.write(buf, 0, n);
                    }
                    files++;
                }
            }
            java.nio.file.Files.write(stamp.toPath(), version.getBytes("UTF-8"));
            android.util.Log.i("Tremu", asset + " unpacked: " + files + " files in " + sys);
            return true;
        } catch (Exception e) {
            android.util.Log.w("Tremu", asset + " unpack failed: " + e);
            return false;
        }
    }

    /**
     * FBNeo only reads arcade sets as archives: an unzipped set folder (Roms/fbneo/opwolf3/) is
     * packed, uncompressed, into zip, again only when a file of the folder is newer.
     */
    public static boolean zipFolder(String folder, String zip) {
        java.io.File dir = new java.io.File(folder), out = new java.io.File(zip);
        java.io.File[] files = dir.listFiles();
        // Without access to all files the folder reads as empty: such a zip must not stay as the game.
        if (files == null || files.length == 0) return false;
        long newest = 0;
        for (java.io.File f : files) newest = Math.max(newest, f.lastModified());
        if (out.exists() && out.length() > 22 && out.lastModified() >= newest) return true;
        java.io.File partial = new java.io.File(zip + ".part");
        out.getParentFile().mkdirs();
        try (java.util.zip.ZipOutputStream z = new java.util.zip.ZipOutputStream(new java.io.FileOutputStream(partial))) {
            z.setLevel(0);
            byte[] buf = new byte[65536];
            for (java.io.File f : files) {
                if (!f.isFile()) continue;
                z.putNextEntry(new java.util.zip.ZipEntry(f.getName()));
                try (java.io.InputStream in = new java.io.FileInputStream(f)) {
                    for (int n; (n = in.read(buf)) > 0; ) z.write(buf, 0, n);
                }
                z.closeEntry();
            }
        } catch (Exception e) {
            android.util.Log.w("Tremu", "Cannot zip " + folder + ": " + e);
            partial.delete();
            return false;
        }
        return partial.renameTo(out) || (out.delete() && partial.renameTo(out));
    }

    /** Downloads the depth network of the 3D view in the background (see DepthModel). */
    public static void fetchDepthModel(String path) {
        DepthModel.fetch(path);
    }

    /** Downloads missing posters in the background (see Posters). */
    public static void fetchPosters(String[] names, String dir) {
        Posters.fetchMissing(instance, names, dir);
    }

    /** "http://192.168.1.20:8080   code 1234" for the "add games" page, or null without Wi-Fi. */
    public static String libraryInfo() {
        String address = LibraryServer.address();
        return address == null || LibraryServer.code() == null ? null : address + "   code " + LibraryServer.code();
    }

    public static boolean isPackageInstalled(String name) {
        return instance.getPackageManager().getLaunchIntentForPackage(name) != null;
    }

    public static String[] wiiuRows() {
        return WiiuLibrary.carouselRows();
    }

    /** Opens a Wii U game in our Cemu app, which comes back here when the player quits. */
    public static boolean launchWiiu(String path) {
        if (!isPackageInstalled("info.cemu.cemu")) return false;
        java.io.File game = new java.io.File(path);
        android.content.Intent intent = new android.content.Intent(android.content.Intent.ACTION_VIEW)
                .setClassName("info.cemu.cemu", "info.cemu.cemu.emulation.EmulationActivity")
                .putExtra("info.cemu.cemu.LaunchPath", game.isDirectory() ? new java.io.File(game, "title.tmd").getPath() : path)
                .putExtra("info.cemu.cemu.ReturnPackage", instance.getPackageName())
                .addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK);
        WonderFitterGun.stop();
        instance.startActivity(intent);
        return true;
    }

    public static String[] vitaRows() {
        return VitaLibrary.carouselRows();
    }

    /** Opens a PS Vita game in our Vita3K app, which installs it the first time. */
    public static boolean launchVita(String path) {
        if (!isPackageInstalled("org.vita3k.emulator")) return false;
        android.content.Intent intent = new android.content.Intent(android.content.Intent.ACTION_VIEW)
                .setClassName("org.vita3k.emulator", "org.vita3k.emulator.TremuLaunchActivity")
                .putExtra("tremu_path", path)
                .putExtra("title_id", VitaLibrary.titleId(new java.io.File(path)))
                .putExtra("tremu_pad", VitaLibrary.padMap(new java.io.File(instance.getFilesDir(), "tremu.cfg")))
                .putExtra("tremu_return_package", instance.getPackageName())
                .addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK);
        WonderFitterGun.stop();
        instance.startActivity(intent);
        return true;
    }

    public static String[] ps3Rows() {
        return Ps3Library.carouselRows();
    }

    /** Opens a PS3 game (a disc image or a folder dump) in the ARMSX3 app, as a frontend would. */
    public static boolean launchPs3(String path) {
        if (!isPackageInstalled("com.armsx3")) return false;
        android.content.Intent intent = new android.content.Intent(android.content.Intent.ACTION_VIEW)
                .setClassName("com.armsx3", "com.armsx2.Main")
                .putExtra("path", path)
                .addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK);
        WonderFitterGun.stop();
        instance.startActivity(intent);
        return true;
    }

    /** Hands the WonderFitter gun over (it accepts one client) and starts another app, e.g. Time Crisis VR. */
    public static boolean launchPackage(String name) {
        android.content.Intent intent = instance.getPackageManager().getLaunchIntentForPackage(name);
        if (intent == null) return false;
        WonderFitterGun.stop();
        intent.addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK);
        instance.startActivity(intent);
        return true;
    }

    private android.net.wifi.WifiManager.MulticastLock multicastLock;

    @Override protected void onResume() {
        super.onResume();
        restartIfGivenFiles();
        if (gunPermitted()) WonderFitterGun.start(this);
        LibraryServer.start(this);
        // Android drops broadcast packets unless an app holds this lock: needed for game discovery.
        if (multicastLock == null) {
            android.net.wifi.WifiManager wifi = (android.net.wifi.WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
            if (wifi != null) multicastLock = wifi.createMulticastLock("tremu-discovery");
        }
        if (multicastLock != null && !multicastLock.isHeld()) multicastLock.acquire();
    }

    @Override protected void onPause() {
        if (multicastLock != null && multicastLock.isHeld()) multicastLock.release();
        // The "Mes jeux" page stays reachable while the headset sleeps (taken off to use the PC).
        super.onPause();
    }

    @Override protected void onDestroy() {
        LibraryServer.stop();
        super.onDestroy();
    }

    /** Another app is in front (e.g. the WonderFitter app opened from the library): it gets the gun. */
    @Override protected void onStop() {
        WonderFitterGun.stop();
        super.onStop();
    }

    private boolean gunPermitted() {
        for (String p : GUN_PERMISSIONS)
            if (checkSelfPermission(p) != PackageManager.PERMISSION_GRANTED) return false;
        return true;
    }

    /** Shrinks the paint's text size until text fits in width (translations run longer than French). */
    static void fitText(Paint paint, String text, float width) {
        float w = paint.measureText(text);
        if (w > width) paint.setTextSize(Math.max(14, paint.getTextSize() * width / w));
    }

    /** Language of the headset texts, chosen in the menu (index into LangTable.CODES). */
    public static void setLanguage(int lang) { Lang.set(lang); }

    public static int systemLanguage() { return Lang.system(); }

    /**
     * Draws the options menu. lines[0] is the title, the last line a status footer, and the
     * lines in between are items; selected indexes into lines. Returns ARGB pixels, top row first.
     */
    private static Bitmap menuLogo;

    public static int[] renderMenu(String[] lines, int selected, int width, int height) {
        Bitmap bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(bitmap);
        Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        paint.setShader(new LinearGradient(0, 0, 0, height, 0xF71E1F24, 0xF70A0A0C, Shader.TileMode.CLAMP));
        canvas.drawRoundRect(new RectF(0, 0, width, height), 28, 28, paint);
        // Steel rim, as on the carousel's logo plate.
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(4);
        paint.setShader(new LinearGradient(0, 0, 0, height, 0xFFD2D5DC, 0xFF55585F, Shader.TileMode.CLAMP));
        canvas.drawRoundRect(new RectF(2, 2, width - 2, height - 2), 28, 28, paint);
        paint.setShader(null);
        paint.setStyle(Paint.Style.FILL);

        float row = (height - 150f) / Math.max(1, lines.length - 2);
        paint.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
        if (menuLogo == null) {
            int[] logo = loadAsset("menu_logo.png", 280, 58);
            if (logo != null) menuLogo = Bitmap.createBitmap(logo, 280, 58, Bitmap.Config.ARGB_8888);
        }
        if (menuLogo != null) canvas.drawBitmap(menuLogo, 28, 16, paint);
        paint.setTextSize(28);
        paint.setColor(0xFFE5E7EB);
        fitText(paint, lines[0], width - 356);
        canvas.drawText(lines[0], 324, 56, paint);
        paint.setColor(0xFFE3121C);
        canvas.drawRect(24, 84, width - 24, 87, paint);

        for (int i = 1; i < lines.length - 1; i++) {
            float y = 96 + row * (i - 1);
            if (i == selected) {
                paint.setShader(new LinearGradient(0, y + 4, 0, y + row - 4, 0xFFE3121C, 0xFF7A0910, Shader.TileMode.CLAMP));
                canvas.drawRoundRect(new RectF(24, y + 4, width - 24, y + row - 4), 12, 12, paint);
                paint.setShader(null);
            }
            paint.setColor(i == selected ? 0xFFFFFFFF : 0xFFD6D8DE);
            String[] parts = lines[i].split("\t", 2);
            paint.setTextSize(30);
            fitText(paint, parts.length > 1 ? parts[0] + "   " + parts[1] : parts[0], width - 80);
            canvas.drawText(parts[0], 40, y + row * 0.68f, paint);
            if (parts.length > 1) {
                paint.setColor(i == selected ? 0xFFFFFFFF : 0xFFFF7078);
                canvas.drawText(parts[1], width - 40 - paint.measureText(parts[1]), y + row * 0.68f, paint);
            }
        }
        paint.setTextSize(24);
        paint.setColor(0xFF9A9DA5);
        fitText(paint, lines[lines.length - 1], width - 72);
        canvas.drawText(lines[lines.length - 1], 36, height - 26, paint);

        int[] pixels = new int[width * height];
        bitmap.getPixels(pixels, 0, width, 0, 0, width, height);
        bitmap.recycle();
        return pixels;
    }
}
