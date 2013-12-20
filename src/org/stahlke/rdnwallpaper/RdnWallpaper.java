package org.stahlke.rdnwallpaper;

import android.service.wallpaper.WallpaperService;
import android.util.Log;

import net.rbgrn.android.glwallpaperservice.GLWallpaperService;

public class RdnWallpaper extends GLWallpaperService {
    public static final String TAG = "rdn";
    public static final boolean DEBUG = BuildConfig.DEBUG;

    public static RecentWaker mRecentWaker = new RecentWaker();

    static public int getDefaultRes(int w, int h) {
        return 3;
    }

    static public int getDefaultRepeatX(int w, int h) {
        return w / 500 + 1;
    }

    static public int getDefaultRepeatY(int w, int h) {
        return h / 500 + 1;
    }

    @Override
    public Engine onCreateEngine() {
        if(DEBUG) Log.i(TAG, "onCreateEngine");
        return new MyEngine();
    }

    class MyEngine extends GLEngine {
        private RdnRenderer renderer;

        MyEngine() {
            renderer = new RdnRenderer(RdnWallpaper.this);
            setRenderer(renderer);
            setRenderMode(RENDERMODE_CONTINUOUSLY);
            mRecentWaker.add(this);
        }

        @Override
        public void onDestroy() {
            if(DEBUG) Log.i(TAG, "MyEngine.onDestroy");

            mRecentWaker.remove(this);

            if(renderer != null) {
                renderer.release();
            }
            renderer = null;

            super.onDestroy();
        }

        @Override
        public void onVisibilityChanged(boolean visible) {
            if(DEBUG) Log.i(TAG, "RdnWallpaper.onVisibilityChanged("+visible+") for "+this);
            renderer.onVisibilityChanged(visible);
            super.onVisibilityChanged(visible);
        }

        public void wakeup() {
            if(DEBUG) Log.i(TAG, "RdnWallpaper.wakeup for "+this);
            onVisibilityChanged(true);
        }
    }
}
