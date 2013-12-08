package org.stahlke.rdnwallpaper;

import android.service.wallpaper.WallpaperService;
import android.util.Log;

import net.rbgrn.android.glwallpaperservice.GLWallpaperService;

public class RdnWallpaper extends GLWallpaperService {
    public static final String TAG = "rdn";
    public static final boolean DEBUG = BuildConfig.DEBUG;

    // FIXME
    public static DrawThreadHolder mDrawThreadHolder = new DrawThreadHolder();

    static public int getDefaultRes(int w, int h) {
        return 4;
    }

    static public int getDefaultRepeatX(int w, int h) {
        return w / 500 + 1;
    }

    static public int getDefaultRepeatY(int w, int h) {
        return h / 500 + 1;
    }

    @Override
    public void onCreate() {
        if(DEBUG) Log.i(TAG, "onCreate");
        super.onCreate();
    }

    @Override
    public void onDestroy() {
        if(DEBUG) Log.i(TAG, "onDestroy");
        super.onDestroy();
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
            renderer.setContext(getBaseContext());
            setRenderer(renderer);
            setRenderMode(RENDERMODE_CONTINUOUSLY);
        }

        //@Override
        //public void onCreate(SurfaceHolder surfaceHolder) {
        //    if(DEBUG) Log.i(TAG, "MyEngine.onCreate");
        //    super.onCreate(surfaceHolder);
        //}

        @Override
        public void onDestroy() {
            if(DEBUG) Log.i(TAG, "MyEngine.onDestroy");

            if(renderer != null) {
                renderer.release();
            }
            renderer = null;

            super.onDestroy();
        }

        @Override
        public void onVisibilityChanged(boolean visible) {
            if(DEBUG) Log.i(TAG, "MyEngine.onVisibilityChanged: "+visible);
//            FIXME
//            if(visible) {
//                mOrientation.onResume();
//            } else {
//                mOrientation.onPause();
//            }
        }

        // FIXME
        public void wakeup() {
            if(DEBUG) Log.i(TAG, "MyEngine.wakeup");
            onVisibilityChanged(true);
        }
    }
}
