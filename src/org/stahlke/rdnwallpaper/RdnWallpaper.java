package org.stahlke.rdnwallpaper;

import android.content.SharedPreferences;
import android.preference.PreferenceManager;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Rect;
import android.os.Handler;
import android.os.SystemClock;
import android.service.wallpaper.WallpaperService;
import android.util.Log;
import android.view.MotionEvent;
import android.view.SurfaceHolder;

import java.util.Random;

public class RdnWallpaper extends WallpaperService {
    public static final String TAG = "rdn";

    // jni method
    public static native void renderFrame(Bitmap bitmap);
    public static native void setParams(int fn_idx, float[] params);
    public static native void resetGrid();

    static {
        System.loadLibrary("rdnlib");
    }

    private final Handler mHandler = new Handler();

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
        Log.i(TAG, "onCreate");
        super.onCreate();
    }

    @Override
    public void onDestroy() {
        Log.i(TAG, "onDestroy");
        super.onDestroy();
    }

    @Override
    public Engine onCreateEngine() {
        Log.i(TAG, "onCreateEngine");
        return new MyEngine();
    }

    class MyEngine extends Engine implements
        SharedPreferences.OnSharedPreferenceChangeListener
    {
        private final long mFrameInterval = 50;
        private int mRes = 4;
        private int mRepeatX = 1;
        private int mRepeatY = 2;
        private final Paint mPaint = new Paint();
        private long mLastDrawTime;
        private int mWidth;
        private int mHeight;
        private int mGridW;
        private int mGridH;
        private Bitmap mBitmap;
        private SharedPreferences mPrefs;

        private Random mRng = new Random();

        private final Runnable mDrawCallback = new Runnable() {
            public void run() {
                drawFrame();
            }
        };
        private boolean mVisible;

        MyEngine() {
            // Create a Paint to draw the lines for our cube
            final Paint paint = mPaint;
            paint.setColor(0xffffffff);
            paint.setAntiAlias(true);
            paint.setStrokeWidth(1);
            paint.setStrokeCap(Paint.Cap.ROUND);
            paint.setStyle(Paint.Style.STROKE);
            paint.setFilterBitmap(true);
            paint.setTextSize(20);

            PreferenceManager.setDefaultValues(RdnWallpaper.this,
                    R.xml.prefs, false);

            mPrefs = PreferenceManager
                    .getDefaultSharedPreferences(RdnWallpaper.this);
            mPrefs.registerOnSharedPreferenceChangeListener(this);

            setParamsToPrefs();
        }

        private int getFnIdx() {
            return Integer.parseInt(mPrefs.getString("function", "0"));
        }

        public void onSharedPreferenceChanged(SharedPreferences prefs, String key) {
            Log.i(TAG, "alpha="+prefs.getFloat("param0", 0));
            Log.i(TAG, "function="+getFnIdx());
            setParamsToPrefs();
            // Make animation start again when prefs are edited.
            onVisibilityChanged(true);
        }

        private float[] getParamsArray() {
            int fn_idx = getFnIdx();

            // FIXME
            float[] params = new float[] {
                mPrefs.getFloat("param_"+fn_idx+"_0", 0),
                mPrefs.getFloat("param_"+fn_idx+"_1", 0),
                mPrefs.getFloat("param_"+fn_idx+"_2", 0)
            };
            return params;
        }

        private void setParamsToPrefs() {
            int fn_idx = getFnIdx();
            float[] p_arr = getParamsArray();
            setParams(fn_idx, p_arr);

            String s = "setParamsToPrefs="+fn_idx;
            for(int i=0; i<p_arr.length; i++) {
                s += ","+p_arr[i];
            }
            Log.i(TAG, s);

            int newRes =
                Integer.parseInt(mPrefs.getString("resolution", "0"));

            int newRepeatX =
                Integer.parseInt(mPrefs.getString("repeatX", "0"));

            int newRepeatY =
                Integer.parseInt(mPrefs.getString("repeatY", "0"));

            Log.i(TAG, "res,rep="+newRes+","+newRepeatX+","+newRepeatY);
            if(newRes     == 0) newRes     = getDefaultRes    (mWidth, mHeight);
            if(newRepeatX == 0) newRepeatX = getDefaultRepeatX(mWidth, mHeight);
            if(newRepeatY == 0) newRepeatY = getDefaultRepeatY(mWidth, mHeight);
            Log.i(TAG, "     -> "+newRes+","+newRepeatX+","+newRepeatY);

            if(
                newRes != mRes ||
                newRepeatX != mRepeatX ||
                newRepeatY != mRepeatY
            ) {
                mRes = newRes;
                mRepeatX = newRepeatX;
                mRepeatY = newRepeatY;
                reshapeGrid();
            }
        }

        @Override
        public void onCreate(SurfaceHolder surfaceHolder) {
            Log.i(TAG, "MyEngine.onCreate");
            super.onCreate(surfaceHolder);
            //setTouchEventsEnabled(true);
        }

        @Override
        public void onDestroy() {
            Log.i(TAG, "MyEngine.onDestroy");
            super.onDestroy();
            mHandler.removeCallbacks(mDrawCallback);
        }

        @Override
        public void onVisibilityChanged(boolean visible) {
            if (mVisible != visible) {
                Log.i(TAG, "MyEngine.onVisibilityChanged: "+visible);
                mVisible = visible;
                if (visible) {
                    drawFrame();
                } else {
                    mHandler.removeCallbacks(mDrawCallback);
                }
            }
        }

        @Override
        public void onSurfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            Log.i(TAG, "MyEngine.onSurfaceChanged");
            super.onSurfaceChanged(holder, format, width, height);
            mWidth = width;
            mHeight = height;

            // might need to update resolution/tiling
            setParamsToPrefs();

            reshapeGrid();
        }

        private void reshapeGrid() {
            mGridW = Math.max(4,  mWidth / mRes / mRepeatX);
            mGridH = Math.max(4, mHeight / mRes / mRepeatY);
            Log.i(TAG, "wh="+mWidth+","+mHeight);
            Log.i(TAG, "grid="+mGridW+","+mGridH);

            mBitmap = Bitmap.createBitmap(mGridW, mGridH, Bitmap.Config.ARGB_8888);

            // This will call renderFrame, which will automatically reallocate
            // the JNI buffers.
            drawFrame();
        }

        @Override
        public void onSurfaceCreated(SurfaceHolder holder) {
            Log.i(TAG, "MyEngine.onSurfaceCreated");
            super.onSurfaceCreated(holder);
        }

        @Override
        public void onSurfaceDestroyed(SurfaceHolder holder) {
            Log.i(TAG, "MyEngine.onSurfaceDestroyed");
            super.onSurfaceDestroyed(holder);
            mVisible = false;
            mHandler.removeCallbacks(mDrawCallback);
        }

        void drawFrame() {
            final SurfaceHolder holder = getSurfaceHolder();

            long delay = mFrameInterval;

            Canvas c = null;
            try {
                c = holder.lockCanvas();
                if (c != null) {
                    long t1 = SystemClock.elapsedRealtime();
                    renderFrame(mBitmap);

                    long t2 = SystemClock.elapsedRealtime();
                    c.save();
                    c.scale(mRes, mRes);
                    mPaint.setFilterBitmap(true);
                    for(int x = 0; x < mRepeatX; x++)
                    for(int y = 0; y < mRepeatY; y++) {
                        c.drawBitmap(mBitmap, x * mGridW, y * mGridH, mPaint);
                    }
                    c.restore();

                    long tf = SystemClock.elapsedRealtime();
                    long gap = tf - mLastDrawTime;
                    mLastDrawTime = tf;
                    // FIXME
                    c.drawText("gap="+gap, 10, 100, mPaint);
                    c.drawText("calc="+(t2-t1), 10, 120, mPaint);
                    c.drawText("draw="+(tf-t2), 10, 140, mPaint);

                    delay -= tf-t1;
                    if(delay < 0) delay = 0;
                }
            } finally {
                if (c != null) holder.unlockCanvasAndPost(c);
            }

            // Reschedule the next redraw
            mHandler.removeCallbacks(mDrawCallback);
            if (mVisible) {
                mHandler.postDelayed(mDrawCallback, delay);
            }
        }
    }
}
