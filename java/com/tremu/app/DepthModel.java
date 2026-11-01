package com.tremu.app;

import android.util.Log;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * The depth network for the 3D view of games without layers: MiDaS v2 (MIT licence), as
 * TensorFlow Lite, from Qualcomm AI Hub's public files. Downloaded once (about 60 MB) when
 * the 3D view first needs it, then kept in Tremu/models.
 */
final class DepthModel {
    private static final String TAG = "Tremu-depth";
    private static final String URL_ZIP =
        "https://qaihub-public-assets.s3.us-west-2.amazonaws.com/qai-hub-models/models/midas/releases/v0.65.0/midas-tflite-float.zip";
    private static Thread worker;

    private DepthModel() {}

    static synchronized void fetch(String path) {
        File target = new File(path);
        if (target.exists() || (worker != null && worker.isAlive())) return;
        worker = new Thread(() -> {
            File partial = new File(path + ".part");
            HttpURLConnection http = null;
            try {
                target.getParentFile().mkdirs();
                http = (HttpURLConnection) new URL(URL_ZIP).openConnection();
                http.setConnectTimeout(10000);
                http.setReadTimeout(30000);
                if (http.getResponseCode() != 200) throw new java.io.IOException("HTTP " + http.getResponseCode());
                boolean found = false;
                try (ZipInputStream zip = new ZipInputStream(http.getInputStream())) {
                    for (ZipEntry e; !found && (e = zip.getNextEntry()) != null; ) {
                        if (!e.getName().endsWith(".tflite")) continue;
                        try (OutputStream out = new FileOutputStream(partial)) {
                            byte[] buf = new byte[65536];
                            for (int n; (n = zip.read(buf)) > 0; ) out.write(buf, 0, n);
                        }
                        found = true;
                    }
                }
                if (!found || !partial.renameTo(target)) throw new java.io.IOException("no model in the archive");
                Log.i(TAG, "Depth model ready: " + target + " (" + target.length() / 1000000 + " MB)");
            } catch (Exception e) {
                Log.w(TAG, "Depth model download failed: " + e);
                partial.delete();
            } finally {
                if (http != null) http.disconnect();
            }
        }, "tremu-depth-model");
        worker.start();
    }
}
