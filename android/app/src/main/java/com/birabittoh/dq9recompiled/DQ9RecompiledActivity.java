package com.birabittoh.dq9recompiled;

import android.app.Activity;
import android.os.Bundle;
import android.util.Log;

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
