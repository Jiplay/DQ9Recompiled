package com.birabittoh.dq9recompiled;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;

import androidx.browser.customtabs.CustomTabsIntent;

import org.libsdl.app.SDLActivity;

/**
 * Thin wrapper around SDL3's SDLActivity that names the native libraries to
 * load: SDL3 itself, then the runner built as a shared library (it holds
 * SDL_main). The self-updater reaches this activity over JNI for the
 * FileProvider and the package installer intent.
 */
public class DQ9RecompiledActivity extends SDLActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        initDiscord(this);
        super.onCreate(savedInstanceState);
    }

    /**
     * Opens an http(s) page in a Custom Tab (the browser's trimmed-down view
     * over the game, with "open in browser" in its menu). The native Discord
     * sign-in (ndsrecomp/runner/src/net/open_url_android.cpp) calls this over
     * JNI from its worker thread. Returns false when it could not, so the
     * caller can fall back to a plain browser.
     */
    public boolean openCustomTab(final String url) {
        final Uri uri = Uri.parse(url);
        final String scheme = uri.getScheme();
        if (!"https".equals(scheme) && !"http".equals(scheme)) return false;
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    new CustomTabsIntent.Builder().build()
                        .launchUrl(DQ9RecompiledActivity.this, uri);
                } catch (Throwable t) {
                    Log.w("DQ9Recompiled", "Custom Tab failed, using the browser", t);
                    try {
                        startActivity(new Intent(Intent.ACTION_VIEW, uri));
                    } catch (Throwable t2) {
                        Log.w("DQ9Recompiled", "no browser to open the URL", t2);
                    }
                }
            }
        });
        return true;
    }

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL3",
            "nds_runner",
        };
    }

    /**
     * Discord's Social SDK (Rich Presence, ndsrecomp/runner/src/
     * discord_rpc_social_sdk.cpp) reaches the Discord app through this
     * activity, and has to have it before the runner creates its client.
     * The SDK is only in builds given its .aar (app/build.gradle.kts), hence
     * the reflection.
     */
    private static void initDiscord(Activity activity) {
        try {
            Class.forName("com.discord.socialsdk.DiscordSocialSdkInit")
                .getMethod("setEngineActivity", Activity.class)
                .invoke(null, activity);
        } catch (ClassNotFoundException e) {
            // A build without the SDK: no Discord activity.
        } catch (Throwable t) {
            Log.w("DQ9Recompiled", "Discord Social SDK init failed", t);
        }
    }
}
