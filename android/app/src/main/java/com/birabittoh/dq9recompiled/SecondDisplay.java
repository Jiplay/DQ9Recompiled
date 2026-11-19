package com.birabittoh.dq9recompiled;

import android.app.Activity;
import android.app.Presentation;
import android.content.Context;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Display;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import java.lang.reflect.Method;

/**
 * The bottom DS screen on a dual-screen handheld's second built-in panel: a
 * Presentation holding one SurfaceView, whose surface and touches go to the
 * runner (ndsrecomp/runner/src/android_second_display.cpp), which presents
 * the bottom LCD into it with the same GPU device as the main window.
 *
 * Everything follows what DisplayManager reports; every display is logged so
 * a log from a device answers what it has. The window never takes focus, so
 * a gamepad keeps driving the activity. All methods run on the UI thread.
 */
final class SecondDisplay implements DisplayManager.DisplayListener {
    private static final String TAG = "DQ9Recompiled";

    // android.view.Display's hidden types (getType()).
    private static final int TYPE_INTERNAL = 1;
    private static final int TYPE_OVERLAY = 4;

    private final Activity activity;
    private final DisplayManager displays;
    private BottomPresentation presentation;
    private boolean started;
    private boolean resumed = true;

    SecondDisplay(Activity activity) {
        this.activity = activity;
        this.displays = (DisplayManager) activity.getSystemService(Context.DISPLAY_SERVICE);
    }

    void start() {
        if (started || displays == null) return;
        started = true;
        displays.registerDisplayListener(this, new Handler(Looper.getMainLooper()));
        logDisplays("start");
        update();
    }

    void stop() {
        if (!started) return;
        started = false;
        displays.unregisterDisplayListener(this);
        dismiss();
    }

    // The surface goes before SDL pauses the runner, which still has to let
    // go of it; it comes back once the activity is resumed.
    void onPause() {
        resumed = false;
        dismiss();
    }

    void onResume() {
        resumed = true;
        update();
    }

    @Override
    public void onDisplayAdded(int displayId) {
        Log.i(TAG, "second display: display " + displayId + " added");
        logDisplays("added");
        update();
    }

    @Override
    public void onDisplayRemoved(int displayId) {
        Log.i(TAG, "second display: display " + displayId + " removed");
        update();
    }

    @Override
    public void onDisplayChanged(int displayId) {
        update();
    }

    private int ownDisplayId() {
        Display own = null;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) own = activity.getDisplay();
        if (own == null) own = activity.getWindowManager().getDefaultDisplay();
        return own != null ? own.getDisplayId() : Display.DEFAULT_DISPLAY;
    }

    // Display.getType() is hidden; -1 when it cannot be read.
    private static int displayType(Display display) {
        try {
            Method method = Display.class.getMethod("getType");
            return (Integer) method.invoke(display);
        } catch (Throwable t) {
            return -1;
        }
    }

    // A panel of the device itself (or a developer-options simulated one):
    // never a cast, a screen recording or an HDMI monitor.
    private static boolean builtIn(Display display) {
        final int type = displayType(display);
        return type == TYPE_INTERNAL || type == TYPE_OVERLAY || type == -1;
    }

    private Display pick() {
        final int own = ownDisplayId();
        Display[] candidates =
            displays.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
        for (Display display : candidates) {
            if (display.getDisplayId() != own && builtIn(display)
                    && display.getState() != Display.STATE_OFF)
                return display;
        }
        // Not flagged for presentations: the window manager may refuse the
        // Presentation there, which show() reports.
        for (Display display : displays.getDisplays()) {
            if (display.getDisplayId() != own && builtIn(display)
                    && (display.getFlags() & Display.FLAG_PRIVATE) == 0
                    && display.getState() != Display.STATE_OFF)
                return display;
        }
        return null;
    }

    private void update() {
        if (!started || !resumed) {
            dismiss();
            return;
        }
        final Display target = pick();
        if (presentation != null) {
            if (target != null
                    && presentation.getDisplay().getDisplayId() == target.getDisplayId())
                return;
            dismiss();
        }
        if (target == null) return;
        Log.i(TAG, "second display: showing the bottom screen on display "
            + target.getDisplayId() + " (" + target.getName() + "), the game is on "
            + ownDisplayId());
        BottomPresentation shown = new BottomPresentation(activity, target);
        try {
            shown.show();
            presentation = shown;
        } catch (WindowManager.InvalidDisplayException e) {
            Log.w(TAG, "second display: display " + target.getDisplayId()
                + " refused the presentation", e);
        } catch (RuntimeException e) {
            Log.w(TAG, "second display: presentation failed", e);
        }
    }

    private void dismiss() {
        if (presentation == null) return;
        Log.i(TAG, "second display: taking the bottom screen down");
        try {
            presentation.dismiss();
        } catch (RuntimeException e) {
            Log.w(TAG, "second display: dismiss failed", e);
        }
        presentation = null;
    }

    private void logDisplays(String why) {
        final int own = ownDisplayId();
        final Display[] all = displays.getDisplays();
        Log.i(TAG, "second display (" + why + "): " + all.length
            + " display(s), the game is on " + own);
        for (Display display : all) {
            final Display.Mode mode = display.getMode();
            final DisplayMetrics metrics = new DisplayMetrics();
            display.getRealMetrics(metrics);
            StringBuilder modes = new StringBuilder();
            for (Display.Mode m : display.getSupportedModes()) {
                if (modes.length() > 0) modes.append(", ");
                modes.append(m.getPhysicalWidth()).append('x').append(m.getPhysicalHeight())
                    .append('@').append(Math.round(m.getRefreshRate()));
            }
            Log.i(TAG, "  display " + display.getDisplayId() + " \"" + display.getName()
                + "\" type=" + displayType(display)
                + " mode=" + mode.getPhysicalWidth() + "x" + mode.getPhysicalHeight()
                + "@" + display.getRefreshRate()
                + " real=" + metrics.widthPixels + "x" + metrics.heightPixels
                + " rotation=" + display.getRotation()
                + " flags=0x" + Integer.toHexString(display.getFlags())
                + " state=" + display.getState()
                + " dpi=" + metrics.xdpi + "x" + metrics.ydpi
                + " modes=[" + modes + "]");
        }
        for (Display display :
                displays.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION))
            Log.i(TAG, "  display " + display.getDisplayId() + " is presentation-capable");
    }

    /** The window on the second panel: one surface, touch only. */
    private static final class BottomPresentation extends Presentation
            implements SurfaceHolder.Callback, View.OnTouchListener {
        private final Activity owner;

        BottomPresentation(Activity owner, Display display) {
            super(owner, display);
            this.owner = owner;
        }

        @Override
        protected void onCreate(Bundle savedInstanceState) {
            super.onCreate(savedInstanceState);
            final Window window = getWindow();
            // Touching a focusable window on another display moves focus
            // there, and with it the gamepad's key events.
            window.addFlags(WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            SurfaceView view = new SurfaceView(getContext());
            view.getHolder().addCallback(this);
            view.setOnTouchListener(this);
            setContentView(view);
            hideSystemBars(window, view);
        }

        @SuppressWarnings("deprecation")
        private static void hideSystemBars(Window window, View view) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                window.setDecorFitsSystemWindows(false);
                WindowInsetsController controller = window.getInsetsController();
                if (controller != null) {
                    controller.hide(WindowInsets.Type.systemBars());
                    controller.setSystemBarsBehavior(
                        WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                }
            } else {
                view.setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
            }
        }

        @Override
        public void surfaceCreated(SurfaceHolder holder) {
            // Handed over with its size in surfaceChanged, which follows.
        }

        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            Log.i(TAG, "second display: surface " + width + "x" + height);
            DQ9RecompiledActivity.nativeSecondDisplaySurface(holder.getSurface(), width, height);
        }

        @Override
        public void surfaceDestroyed(SurfaceHolder holder) {
            // Returns once the runner no longer draws into it.
            DQ9RecompiledActivity.nativeSecondDisplaySurface(null, 0, 0);
        }

        // The first finger is the stylus; a second one ends its touch.
        @Override
        public boolean onTouch(View view, MotionEvent event) {
            final int action;
            switch (event.getActionMasked()) {
                case MotionEvent.ACTION_DOWN: action = 0; break;
                case MotionEvent.ACTION_UP: action = 1; break;
                case MotionEvent.ACTION_MOVE: action = 2; break;
                case MotionEvent.ACTION_CANCEL:
                case MotionEvent.ACTION_POINTER_DOWN: action = 3; break;
                default: return true;
            }
            DQ9RecompiledActivity.nativeSecondDisplayTouch(
                action, event.getX(0), event.getY(0), view.getWidth(), view.getHeight());
            return true;
        }

        // Not focusable, so these should never come here; if they do, they
        // are the game's.
        @Override
        public boolean dispatchKeyEvent(KeyEvent event) {
            return owner.dispatchKeyEvent(event);
        }

        @Override
        public boolean dispatchGenericMotionEvent(MotionEvent event) {
            return owner.dispatchGenericMotionEvent(event);
        }
    }
}
