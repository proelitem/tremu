package com.tremu.app;

import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.BitmapShader;
import android.graphics.BlurMaskFilter;
import android.graphics.Canvas;
import android.graphics.LightingColorFilter;
import android.graphics.LinearGradient;
import android.graphics.Matrix;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffXfermode;
import android.graphics.RectF;
import android.graphics.Shader;
import android.graphics.Typeface;
import java.io.File;
import java.util.HashMap;

/** The game carousel, drawn for the native side as ARGB pixels. */
final class Carousel {
    private static final int POSTER_W = 300, POSTER_H = 420;
    private static final HashMap<String, Bitmap> posters = new HashMap<>();
    private static final String[] TABS = {"Tous", "Arcade", "Sega", "Nintendo", "PS", "Xbox"};
    private static final int RED = 0xFFFF1E28;
    /* Card shape at a distance of 0, 1, 2 and 3 places from the centre, in pixels of the 1280 x 720 picture. */
    private static final float[] CARD_X = {0, 265, 470, 620}, CARD_W = {275, 205, 170, 150},
            CARD_NEAR = {385, 340, 300, 270}, CARD_FAR = {385, 300, 265, 240}, CARD_SHADE = {255, 215, 165, 110}, CARD_ALPHA = {255, 255, 255, 0};
    private static final float CARD_Y = 302;
    private static Bitmap canvasBitmap, backdrop, glow;

    private Carousel() {}

    /** Decodes a banner once, scaled to fit the poster box; null if missing. */
    private static Bitmap poster(String path) {
        if (path == null || path.isEmpty()) return null;
        if (posters.containsKey(path)) return posters.get(path);
        Bitmap result = null;
        if (new File(path).isFile()) {
            BitmapFactory.Options bounds = new BitmapFactory.Options();
            bounds.inJustDecodeBounds = true;
            BitmapFactory.decodeFile(path, bounds);
            BitmapFactory.Options opts = new BitmapFactory.Options();
            opts.inSampleSize = Math.max(1, Math.min(bounds.outWidth / POSTER_W, bounds.outHeight / POSTER_H));
            Bitmap decoded = BitmapFactory.decodeFile(path, opts);
            if (decoded != null) result = Bitmap.createScaledBitmap(decoded, POSTER_W, POSTER_H, true);
        }
        posters.put(path, result);
        return result;
    }

    private static void placeholder(Canvas c, RectF r, String title, Paint p) {
        p.setShader(new LinearGradient(r.left, r.top, r.right, r.bottom, 0xFF34363C, 0xFF0E0E11, Shader.TileMode.CLAMP));
        c.drawRoundRect(r, 18, 18, p);
        p.setShader(null);
        p.setColor(0xFFE2E8F0);
        p.setTextSize(r.width() * 0.11f);
        p.setTextAlign(Paint.Align.CENTER);
        String[] words = title.split(" ");
        float y = r.centerY() - (words.length - 1) * p.getTextSize() * 0.6f;
        for (String w : words) { c.drawText(w, r.centerX(), y, p); y += p.getTextSize() * 1.2f; }
        p.setTextAlign(Paint.Align.LEFT);
    }

    /**
     * Shown while a game starts, where its screen will be: the poster, the title, a turning wheel
     * (angle in degrees) and a status line. Returns ARGB pixels, top row first.
     */
    static int[] renderLoading(String title, String banner, String status, float angle, int width, int height) {
        if (Showcase.on()) { title = "Dreamcast"; banner = null; }
        if (canvasBitmap == null || canvasBitmap.getWidth() != width || canvasBitmap.getHeight() != height)
            canvasBitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(canvasBitmap);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        c.drawColor(0, PorterDuff.Mode.CLEAR);
        c.drawBitmap(backdrop(width, height), 0, 0, null);

        RectF r = new RectF(110, (height - POSTER_H) / 2f, 110 + POSTER_W, (height + POSTER_H) / 2f);
        Bitmap b = poster(banner);
        if (b != null) c.drawBitmap(b, null, r, p);
        else placeholder(c, r, title, p);

        float left = r.right + 90, right = width - 80;
        p.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
        p.setColor(0xFFFFFFFF);
        p.setTextSize(54);
        TremuActivity.fitText(p, title, right - left);
        c.drawText(title, left, height * 0.36f, p);

        // The wheel: a faint ring and a bright arc going round.
        float radius = 70, wx = left + radius, wy = height * 0.58f;
        RectF ring = new RectF(wx - radius, wy - radius, wx + radius, wy + radius);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(14);
        p.setStrokeCap(Paint.Cap.ROUND);
        p.setColor(0x33FFFFFF);
        c.drawOval(ring, p);
        p.setColor(RED);
        c.drawArc(ring, angle, 100, false, p);
        p.setColor(0xFFE5E7EB);
        c.drawArc(ring, angle + 180, 40, false, p);
        p.setStyle(Paint.Style.FILL);

        p.setTypeface(Typeface.DEFAULT);
        p.setColor(0xFFD1D5DB);
        p.setTextSize(32);
        float textLeft = wx + radius + 50;
        TremuActivity.fitText(p, status, right - textLeft);
        c.drawText(status, textLeft, wy + 12, p);

        Showcase.save("loading", canvasBitmap);
        int[] pixels = new int[width * height];
        canvasBitmap.getPixels(pixels, 0, width, 0, 0, width, height);
        return pixels;
    }

    /** The background and logo, which never change; rounded corners are transparent. */
    private static Bitmap backdrop(int width, int height) {
        if (backdrop != null && backdrop.getWidth() == width && backdrop.getHeight() == height) return backdrop;
        backdrop = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(backdrop);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        int[] pixels = TremuActivity.loadAsset("carousel_bg.jpg", width, height);
        if (pixels != null) {
            p.setShader(new BitmapShader(Bitmap.createBitmap(pixels, width, height, Bitmap.Config.ARGB_8888),
                                         Shader.TileMode.CLAMP, Shader.TileMode.CLAMP));
        } else {
            p.setColor(0xFF08080A);
        }
        c.drawRoundRect(new RectF(0, 0, width, height), 36, 36, p);
        p.setShader(null);
        // The logo's metal plate covers the corner: SRC_ATOP keeps the page's rounded edge.
        int[] logo = TremuActivity.loadAsset("carousel_logo.png", 340, 116);
        if (logo != null) {
            p.setXfermode(new PorterDuffXfermode(PorterDuff.Mode.SRC_ATOP));
            c.drawBitmap(Bitmap.createBitmap(logo, 340, 116, Bitmap.Config.ARGB_8888), 0, 0, p);
            p.setXfermode(null);
        }
        return backdrop;
    }

    /** Red halo around the chosen card, blurred once (too slow to do for each picture). */
    private static Bitmap glow() {
        if (glow != null) return glow;
        int margin = 40;
        glow = Bitmap.createBitmap(POSTER_W + 2 * margin, POSTER_H + 2 * margin, Bitmap.Config.ARGB_8888);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setColor(RED);
        p.setMaskFilter(new BlurMaskFilter(24, BlurMaskFilter.Blur.NORMAL));
        new Canvas(glow).drawRect(margin - 6, margin - 6, margin + POSTER_W + 6, margin + POSTER_H + 6, p);
        return glow;
    }

    /** A game's poster, or a card with its title when it has none. */
    private static Bitmap card(String banner, String title) {
        Bitmap b = poster(banner);
        if (b != null) return b;
        String key = "title:" + title;
        b = posters.get(key);
        if (b == null) {
            b = Bitmap.createBitmap(POSTER_W, POSTER_H, Bitmap.Config.ARGB_8888);
            placeholder(new Canvas(b), new RectF(0, 0, POSTER_W, POSTER_H), title, new Paint(Paint.ANTI_ALIAS_FLAG));
            posters.put(key, b);
        }
        return b;
    }

    private static float lerpTable(float[] table, float d) {
        int i = Math.min((int) d, table.length - 2);
        return table[i] + (table[i + 1] - table[i]) * (d - i);
    }

    /** Draws a card leaning back: its side away from the centre is further and shorter. */
    private static void drawCard(Canvas c, Paint p, Bitmap b, float offset, boolean chosen) {
        float d = Math.abs(offset), side = Math.signum(offset);
        float x = 640 + side * lerpTable(CARD_X, d), w = lerpTable(CARD_W, d);
        float near = lerpTable(CARD_NEAR, d) / 2, far = lerpTable(CARD_FAR, d) / 2;
        float xNear = x - side * w / 2, xFar = x + side * w / 2;
        if (side == 0) { xNear = x - w / 2; xFar = x + w / 2; }
        float left = Math.min(xNear, xFar), right = Math.max(xNear, xFar);
        float leftH = xNear < xFar ? near : far, rightH = xNear < xFar ? far : near;
        float[] from = {0, 0, POSTER_W, 0, POSTER_W, POSTER_H, 0, POSTER_H};
        float[] to = {left, CARD_Y - leftH, right, CARD_Y - rightH, right, CARD_Y + rightH, left, CARD_Y + leftH};
        Matrix m = new Matrix();
        m.setPolyToPoly(from, 0, to, 0, 4);
        c.save();
        c.concat(m);
        int alpha = (int) lerpTable(CARD_ALPHA, d);
        if (chosen && d < .5f) {
            p.setAlpha((int) (255 * (1 - 2 * d)));
            c.drawBitmap(glow(), -40, -40, p);
        }
        // Cards further away are darker, not see-through.
        int shade = (int) lerpTable(CARD_SHADE, d);
        p.setAlpha(alpha);
        p.setColorFilter(new LightingColorFilter(shade << 16 | shade << 8 | shade, 0));
        c.drawBitmap(b, 0, 0, p);
        p.setColorFilter(null);
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeWidth(chosen ? 6 : 3);
        p.setColor(chosen ? RED : 0xFFB4B8C0);
        p.setAlpha(alpha);
        c.drawRect(0, 0, POSTER_W, POSTER_H, p);
        p.setStyle(Paint.Style.FILL);
        p.setAlpha(255);
        c.restore();
    }

    /** Console tabs and the magnifier; barItem is the one under the stick, -1 when it is on the games. */
    private static void drawBar(Canvas c, Paint p, int tab, int barItem, boolean searching) {
        float w = 122, gap = 8, left = 346, top = 40, bottom = 84;
        p.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
        p.setTextSize(22);
        p.setTextAlign(Paint.Align.CENTER);
        p.setLetterSpacing(.08f);
        for (int i = 0; i < TABS.length; i++) {
            RectF r = new RectF(left + i * (w + gap), top, left + i * (w + gap) + w, bottom);
            if (i == tab) {
                p.setShader(new LinearGradient(0, top, 0, bottom, 0xFFE3121C, 0xFF7A0910, Shader.TileMode.CLAMP));
                c.drawRoundRect(r, 22, 22, p);
                p.setShader(null);
            }
            if (i == barItem) {
                p.setStyle(Paint.Style.STROKE);
                p.setStrokeWidth(3);
                p.setColor(0xFFFFFFFF);
                c.drawRoundRect(r, 22, 22, p);
                p.setStyle(Paint.Style.FILL);
            }
            p.setColor(i == tab || i == barItem ? 0xFFFFFFFF : 0xFFA8ACB4);
            c.drawText(Lang.t(TABS[i]).toUpperCase(java.util.Locale.ROOT), r.centerX(), r.centerY() + 8, p);
        }
        p.setLetterSpacing(0);
        p.setTextAlign(Paint.Align.LEFT);

        float mx = left + TABS.length * (w + gap) + 38, my = (top + bottom) / 2;
        boolean on = barItem == TABS.length;
        p.setStyle(Paint.Style.STROKE);
        if (on || searching) {
            p.setStrokeWidth(3);
            p.setColor(on ? 0xFFFFFFFF : RED);
            c.drawCircle(mx, my, 24, p);
        }
        p.setStrokeWidth(4);
        p.setStrokeCap(Paint.Cap.ROUND);
        p.setColor(on || searching ? 0xFFFFFFFF : 0xFFA8ACB4);
        c.drawCircle(mx - 3, my - 3, 9, p);
        c.drawLine(mx + 4, my + 4, mx + 12, my + 12, p);
        p.setStyle(Paint.Style.FILL);
    }

    /**
     * position is the (animated, fractional) index at the centre; selected the chosen entry, console its
     * system. footer is a status line (hints or an error). Returns ARGB pixels, top row first.
     */
    static int[] render(String[] titles, String[] banners, float position, int selected, String console, String footer,
                        int tab, int barItem, String search, int width, int height) {
        if (Showcase.on()) {
            titles = Showcase.TITLES;
            banners = new String[titles.length];
            selected = Math.max(0, Math.min(titles.length - 1, Math.round(position)));
            position = selected;
            console = "";
            footer = Showcase.footer(footer);
        }
        if (canvasBitmap == null || canvasBitmap.getWidth() != width || canvasBitmap.getHeight() != height)
            canvasBitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
        Canvas c = new Canvas(canvasBitmap);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
        c.drawColor(0, PorterDuff.Mode.CLEAR);
        c.drawBitmap(backdrop(width, height), 0, 0, null);
        drawBar(c, p, tab, barItem, !search.isEmpty());
        float cx = width / 2f;
        p.setTypeface(Typeface.DEFAULT);
        if (!search.isEmpty()) {
            p.setTextSize(18);
            p.setColor(0xFFFF8A8E);
            p.setTextAlign(Paint.Align.RIGHT);
            c.drawText(Lang.f("Recherche : %s     B : effacer", search), width - 40, 106, p);
            p.setTextAlign(Paint.Align.LEFT);
        }

        // Far cards first so the centre one is drawn on top.
        Integer[] order = new Integer[titles.length];
        for (int i = 0; i < order.length; i++) order[i] = i;
        final float at = position;
        java.util.Arrays.sort(order, (a, b) -> Float.compare(Math.abs(b - at), Math.abs(a - at)));
        for (int i : order) {
            float offset = i - position;
            if (Math.abs(offset) >= 3) continue;
            drawCard(c, p, card(banners[i], titles[i]), offset, i == selected);
        }

        p.setTextAlign(Paint.Align.CENTER);
        p.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
        boolean noGames = titles.length == 0 && tab == 0 && search.isEmpty();
        if (titles.length > 0) {
            // Console titles end in " (console)", shown on its own line here.
            String title = titles[selected], tag = " (" + console + ")";
            if (title.endsWith(tag)) title = title.substring(0, title.length() - tag.length());
            p.setColor(0xFFFFFFFF);
            p.setTextSize(38);
            p.setLetterSpacing(.04f);
            TremuActivity.fitText(p, title, 1000);
            c.drawText(title, cx, 532, p);
            p.setLetterSpacing(0);
            p.setTypeface(Typeface.DEFAULT);
            p.setTextSize(20);
            p.setColor(0xFFE5E7EB);
            c.drawText(console, cx, 572, p);

            RectF play = new RectF(cx - 112, 586, cx + 112, 634);
            p.setShader(new LinearGradient(0, play.top, 0, play.bottom, 0xFFE3121C, 0xFF7A0910, Shader.TileMode.CLAMP));
            c.drawRoundRect(play, 12, 12, p);
            p.setShader(null);
            p.setStyle(Paint.Style.STROKE);
            p.setStrokeWidth(2);
            p.setColor(0xFFFF5A60);
            c.drawRoundRect(play, 12, 12, p);
            p.setStyle(Paint.Style.FILL);
            p.setColor(0xFFFFFFFF);
            p.setTypeface(Typeface.create(Typeface.DEFAULT, Typeface.BOLD));
            p.setTextSize(24);
            p.setLetterSpacing(.1f);
            String playText = Lang.t("Jouer").toUpperCase(java.util.Locale.ROOT);
            float textW = p.measureText(playText), start = cx - (textW + 30) / 2;
            Path arrow = new Path();
            arrow.moveTo(start, 600);
            arrow.lineTo(start + 18, 610);
            arrow.lineTo(start, 620);
            arrow.close();
            c.drawPath(arrow, p);
            p.setTextAlign(Paint.Align.LEFT);
            c.drawText(playText, start + 30, 619, p);
            p.setLetterSpacing(0);
            p.setTextAlign(Paint.Align.CENTER);
        } else if (!noGames) {
            p.setColor(0xFFFFFFFF);
            p.setTextSize(38);
            c.drawText(Lang.t("Aucun résultat"), cx, 320, p);
        }
        p.setTypeface(Typeface.DEFAULT);
        p.setTextSize(noGames ? 28 : 20);
        p.setColor(0xFFA8ACB4);
        TremuActivity.fitText(p, footer, width - 96);
        c.drawText(footer, cx, noGames ? 340 : 676, p);
        p.setTextAlign(Paint.Align.LEFT);

        Showcase.save("carousel", canvasBitmap);
        int[] pixels = new int[width * height];
        canvasBitmap.getPixels(pixels, 0, width, 0, 0, width, height);
        return pixels;
    }
}
