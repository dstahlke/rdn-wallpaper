package org.stahlke.rdnwallpaper;

import android.content.SharedPreferences;
import android.preference.PreferenceManager;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.ColorMatrix;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Matrix;
import android.os.Handler;
import android.os.SystemClock;
import android.service.wallpaper.WallpaperService;
import android.util.Log;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.graphics.PixelFormat;

public class RdnWallpaper extends WallpaperService {
    public static final String TAG = "rdn";

    // jni method
    public static native void evolve();
    public static native void renderFrame(Bitmap bitmap, int dir);
    public static native void setParams(int fn_idx, float[] params, int pal_idx);
    public static native void setColorMatrix(float[] cm);
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

    // http://stackoverflow.com/a/15119089/1048959
    public static void adjustHue(ColorMatrix cm, float value) {
        float cosVal = (float) Math.cos(value);
        float sinVal = (float) Math.sin(value);
        float lumR = 0.213f;
        float lumG = 0.715f;
        float lumB = 0.072f;
        float[] mat = new float[] {
            lumR + cosVal * (1 - lumR) + sinVal * (-lumR), lumG + cosVal * (-lumG) +
                sinVal * (-lumG), lumB + cosVal * (-lumB) + sinVal * (1 - lumB), 0, 0, 
            lumR + cosVal * (-lumR) + sinVal * (0.143f), lumG + cosVal * (1 - lumG) +
                sinVal * (0.140f), lumB + cosVal * (-lumB) + sinVal * (-0.283f), 0, 0,
            lumR + cosVal * (-lumR) + sinVal * (-(1 - lumR)), lumG + cosVal * (-lumG) +
                sinVal * (lumG), lumB + cosVal * (1 - lumB) + sinVal * (lumB), 0, 0, 
                 0f, 0f, 0f, 1f, 0f, 
                 0f, 0f, 0f, 0f, 1f };
        cm.postConcat(new ColorMatrix(mat));
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
        private boolean mDoRotate;
        private int mGridW;
        private int mGridH;
        private Bitmap mBitmap;
        private SharedPreferences mPrefs;

        private float[] mProfileAccum = new float[4];
        private float[] mProfileTimes = new float[4];
        private int mProfileTicks = 0;

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
            int len=0;
            for(len=0; ; len++) {
                if(!mPrefs.contains("param_"+fn_idx+"_"+len)) break;
            }
            float[] ret = new float[len];
            for(int i=0; i<len; i++) {
                ret[i] = mPrefs.getFloat("param_"+fn_idx+"_"+i, 0);
            }
            return ret;
        }

        private void setParamsToPrefs() {
            int fn_idx = getFnIdx();
            float[] p_arr = getParamsArray();

            String s = "setParamsToPrefs="+fn_idx;
            for(int i=0; i<p_arr.length; i++) {
                s += ","+p_arr[i];
            }
            Log.i(TAG, s);

            int pal = mPrefs.getInt("palette"+fn_idx, 0);
            Log.i(TAG, "palette="+pal);

            setParams(fn_idx, p_arr, pal);

            float newHue = mPrefs.getFloat("hue", 0);

            ColorMatrix cm = new ColorMatrix();
            adjustHue(cm, newHue / 180f * (float)Math.PI);
            setColorMatrix(cm.getArray());

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
            if(height > width) {
                mWidth = width;
                mHeight = height;
                mDoRotate = false;
            } else {
                mWidth = height;
                mHeight = width;
                mDoRotate = true;
            }

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
                    holder.setFormat(PixelFormat.RGBA_8888);
                    c.save();
                    if(mDoRotate) {
                        c.rotate(-90);
                        c.translate(-mWidth+1,0);
                    }

                    long t1 = SystemClock.uptimeMillis();

                    evolve();

                    long t2 = SystemClock.uptimeMillis();

                    long t3 = SystemClock.uptimeMillis();
                    c.save();
                    c.scale(mRes, mRes);
                    mPaint.setFilterBitmap(true);

                    renderFrame(mBitmap, 0);
                    for(int x = 0; x < mRepeatX; x++)
                    for(int y = 0; y < mRepeatY; y++) {
                        if(y % 2 == 0) {
                            c.drawBitmap(mBitmap, x * mGridW, y * mGridH, mPaint);
                        }
                    }

                    if(mRepeatY > 1) {
                        renderFrame(mBitmap, 1);
                    }
                    c.save();
                    Matrix m = new Matrix();
                    m.preScale(-1, 1);
                    m.postTranslate(mGridW*mRepeatX, 0);
                    c.concat(m);
                    for(int x = 0; x < mRepeatX; x++)
                    for(int y = 0; y < mRepeatY; y++) {
                        if(y % 2 == 1) {
                            c.drawBitmap(mBitmap, x * mGridW, y * mGridH, mPaint);
                        }
                    }
                    c.restore();

                    c.restore();

                    long tf = SystemClock.uptimeMillis();
                    long gap = tf - mLastDrawTime;
                    mLastDrawTime = tf;

                    mProfileAccum[0] += gap;
                    mProfileAccum[1] += t2-t1;
                    mProfileAccum[2] += t3-t2;
                    mProfileAccum[3] += tf-t2;
                    mProfileTicks++;

                    if(mProfileTicks == 10) {
                        for(int i=0; i<mProfileAccum.length; i++) {
                            mProfileTimes[i] = mProfileAccum[i] / mProfileTicks;
                            mProfileAccum[i] = 0;
                        }
                        mProfileTicks = 0;
                    }

                    // FIXME
                    c.drawText("time="+t1, 10, 80, mPaint);
                    c.drawText("gap=" +mProfileTimes[0], 10, 100, mPaint);
                    c.drawText("calc="+mProfileTimes[1], 10, 120, mPaint);
                    c.drawText("rend="+mProfileTimes[2], 10, 140, mPaint);
                    c.drawText("draw="+mProfileTimes[3], 10, 160, mPaint);

                    delay -= tf-t1;
                    if(delay < 0) delay = 0;

                    c.restore();
                }
            } finally {
                if (c != null) holder.unlockCanvasAndPost(c);
            }

            // Reschedule the next redraw
            mHandler.removeCallbacks(mDrawCallback);
            if (mVisible) {
                //mHandler.postDelayed(mDrawCallback, delay);
                mHandler.post(mDrawCallback);
            }
        }
    }
}
