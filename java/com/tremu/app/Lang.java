package com.tremu.app;

import java.util.Locale;

/** Texts in the language chosen in the headset menu (translations in tools/lang.py). */
final class Lang {
    private Lang() {}

    private static volatile int current;

    static void set(int lang) { current = lang; }

    /** Index of the system language in LangTable.CODES, English when not translated. */
    static int system() {
        String code = Locale.getDefault().getLanguage();
        for (int i = 0; i < LangTable.CODES.length; i++) if (LangTable.CODES[i].equals(code)) return i;
        return 1;
    }

    static String code() { return LangTable.CODES[current]; }

    /** The French text in the current language; unknown texts come back unchanged. */
    static String t(String french) {
        int lang = current;
        if (lang == 0) return french;
        for (String[] row : LangTable.TABLE) if (row[0].equals(french)) return row[lang];
        return french;
    }

    /** t() as a format: the translation keeps the %s of the French text. */
    static String f(String french, Object... args) { return String.format(Locale.ROOT, t(french), args); }

    /** t() as a JavaScript string literal, for the web page's script. */
    static String js(String french) {
        String s = t(french);
        StringBuilder out = new StringBuilder("\"");
        for (char c : s.toCharArray()) {
            if (c == '"' || c == '\\') out.append('\\').append(c);
            else if (c < 32 || c > 126 || c == '<' || c == '\'') out.append(String.format("\\u%04x", (int) c));
            else out.append(c);
        }
        return out.append('"').toString();
    }

    /** t() for HTML text. */
    static String h(String french) {
        return t(french).replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace("'", "&#39;");
    }
}
