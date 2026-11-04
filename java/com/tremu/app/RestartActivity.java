package com.tremu.app;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;

/**
 * Runs in its own process (":restart"): waits for Tremu's main process to end, then opens it again
 * on the carousel. Xenia can only stop a game by ending its process (KernelState::TerminateTitle
 * calls quick_exit), so leaving an Xbox 360 game restarts the app.
 */
public final class RestartActivity extends Activity {
    private static final long CHECK_MS = 100, GIVE_UP_MS = 8000;

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        final java.io.File main = new java.io.File("/proc/" + getIntent().getIntExtra("pid", 0));
        final long start = System.currentTimeMillis();
        final Handler handler = new Handler(getMainLooper());
        handler.post(new Runnable() {
            @Override public void run() {
                if (main.exists() && System.currentTimeMillis() - start < GIVE_UP_MS) {
                    handler.postDelayed(this, CHECK_MS);
                    return;
                }
                android.util.Log.i("Tremu", "restart: opening Tremu again");
                startActivity(new Intent(RestartActivity.this, TremuActivity.class)
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK));
                finish();
                android.os.Process.killProcess(android.os.Process.myPid());
            }
        });
    }
}
