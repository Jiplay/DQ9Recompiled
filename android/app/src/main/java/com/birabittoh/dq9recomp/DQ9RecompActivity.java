package com.birabittoh.dq9recomp;

import org.libsdl.app.SDLActivity;

/**
 * Thin wrapper around SDL3's SDLActivity that names the native libraries to
 * load: SDL3 itself, then the runner built as a shared library (it holds
 * SDL_main). The self-updater reaches this activity over JNI for the
 * FileProvider and the package installer intent.
 */
public class DQ9RecompActivity extends SDLActivity {

    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL3",
            "nds_runner",
        };
    }
}
