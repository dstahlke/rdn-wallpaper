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
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import java.util.concurrent.locks.ReentrantLock;

public class RdnWallpaper extends WallpaperService {
    public static final String TAG = "rdn";
    public static final boolean DEBUG = BuildConfig.DEBUG;

    // jni method
    public static native void evolve();
    public static native void renderFrame(Bitmap bitmap, int dir,
            float acc_x, float acc_y, float acc_z);
    public static native void setParams(int fn_idx, float[] params, int pal_idx);
    public static native void setColorMatrix(float[] cm);
    public static native void resetGrid();

    static {
        System.loadLibrary("rdnlib");
    }

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
        private ReentrantLock mDrawLock = new ReentrantLock();

        private float[] mProfileAccum = new float[4];
        private float[] mProfileTimes = new float[4];
        private int mProfileTicks = 0;

        private DrawThread mDrawThread = null;

        private OrientationReader mOrientation = new OrientationReader();

        class DrawThread extends Thread {
            boolean mRunning = false;
            boolean mAlive = true;
            private Object mRunningLock = new Object();

            DrawThread() {
                super("RDN Draw Thread");
            }

            public void run() {
                if(DEBUG) Log.i(TAG, "mDrawThread started");
                while(true) {
                    synchronized(mRunningLock) {
                        while(!mRunning && mAlive) {
                            try {
                                mRunningLock.wait();
                            } catch(InterruptedException e) { }
                        }
                        if(!mAlive) break;
                    }

                    drawFrame();

                    yield();
                }
                if(DEBUG) Log.i(TAG, "mDrawThread exit");
            }

            public void setRunning(boolean x) {
                synchronized(mRunningLock) {
                    mRunning = x;
                    mRunningLock.notify();
                }
            }

            public void requestShutdown() {
                synchronized(mRunningLock) {
                    mAlive = false;
                    mRunningLock.notify();
                }
            }
        }

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
            if(DEBUG) Log.i(TAG, "alpha="+prefs.getFloat("param0", 0));
            if(DEBUG) Log.i(TAG, "function="+getFnIdx());
            setParamsToPrefs();
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
            if(DEBUG) Log.i(TAG, s);

            int pal = mPrefs.getInt("palette"+fn_idx, 0);
            if(DEBUG) Log.i(TAG, "palette="+pal);

            float newHue = mPrefs.getFloat("hue", 0);

            ColorMatrix cm = new ColorMatrix();
            adjustHue(cm, newHue / 180f * (float)Math.PI);

            mDrawLock.lock(); try {
                setParams(fn_idx, p_arr, pal);
                setColorMatrix(cm.getArray());
            } finally { mDrawLock.unlock(); }

            int newRes =
                Integer.parseInt(mPrefs.getString("resolution", "0"));

            int newRepeatX =
                Integer.parseInt(mPrefs.getString("repeatX", "0"));

            int newRepeatY =
                Integer.parseInt(mPrefs.getString("repeatY", "0"));

            if(DEBUG) Log.i(TAG, "res,rep="+newRes+","+newRepeatX+","+newRepeatY);
            if(newRes     == 0) newRes     = getDefaultRes    (mWidth, mHeight);
            if(newRepeatX == 0) newRepeatX = getDefaultRepeatX(mWidth, mHeight);
            if(newRepeatY == 0) newRepeatY = getDefaultRepeatY(mWidth, mHeight);
            if(DEBUG) Log.i(TAG, "     -> "+newRes+","+newRepeatX+","+newRepeatY);

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
            if(DEBUG) Log.i(TAG, "MyEngine.onCreate");
            super.onCreate(surfaceHolder);
            //setTouchEventsEnabled(true);
        }

        @Override
        public void onDestroy() {
            if(DEBUG) Log.i(TAG, "MyEngine.onDestroy");
            super.onDestroy();
        }

        @Override
        public void onVisibilityChanged(boolean visible) {
            if(DEBUG) Log.i(TAG, "MyEngine.onVisibilityChanged: "+visible);
            if(mDrawThread != null) {
                mDrawThread.setRunning(visible);
            }
            if(visible) {
                mOrientation.onResume();
            } else {
                mOrientation.onPause();
            }
        }

        @Override
        public void onSurfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            if(DEBUG) Log.i(TAG, "MyEngine.onSurfaceChanged");
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
            if(DEBUG) Log.i(TAG, "wh="+mWidth+","+mHeight);
            if(DEBUG) Log.i(TAG, "grid="+mGridW+","+mGridH);

            mDrawLock.lock(); try {
                mBitmap = Bitmap.createBitmap(mGridW, mGridH, Bitmap.Config.ARGB_8888);
            } finally { mDrawLock.unlock(); }
        }

        @Override
        public void onSurfaceCreated(SurfaceHolder holder) {
            if(DEBUG) Log.i(TAG, "MyEngine.onSurfaceCreated");
            super.onSurfaceCreated(holder);

            mDrawThread = new DrawThread();
            mDrawThreadHolder.add(this);
            mDrawThread.setPriority(Thread.MIN_PRIORITY);
            mDrawThread.start();
        }

        @Override
        public void onSurfaceDestroyed(SurfaceHolder holder) {
            if(DEBUG) Log.i(TAG, "MyEngine.onSurfaceDestroyed");
            super.onSurfaceDestroyed(holder);
            mDrawThread.requestShutdown();
            mDrawThreadHolder.remove(this);
        }

        public void wakeup() {
            if(DEBUG) Log.i(TAG, "MyEngine.wakeup");
            onVisibilityChanged(true);
        }

        void drawFrame() {
            // FIXME - Is a lock needed?  Can holder.lockCanvas deadlock if surface is
            // destroyed?
            final SurfaceHolder holder = getSurfaceHolder();

            Canvas c = null;
            try {
                c = holder.lockCanvas();
                holder.setFormat(PixelFormat.RGBA_8888);
                if(c != null) {
                    mDrawLock.lock(); try {
                        doDraw(c);
                    } finally { mDrawLock.unlock(); }
                }
            } finally {
                if(c != null) holder.unlockCanvasAndPost(c);
            }
        }

        private void doDraw(Canvas c) {
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

            renderFrame(mBitmap, 0,
                    mOrientation.mVal[0],
                    mOrientation.mVal[1],
                    mOrientation.mVal[2]);
            for(int x = 0; x < mRepeatX; x++)
            for(int y = 0; y < mRepeatY; y++) {
                if(y % 2 == 0) {
                    c.drawBitmap(mBitmap, x * mGridW, y * mGridH, mPaint);
                }
            }

            if(mRepeatY > 1) {
                renderFrame(mBitmap, 1,
                        mOrientation.mVal[0],
                        mOrientation.mVal[1],
                        mOrientation.mVal[2]);
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

            if(DEBUG) {
                c.drawText("time="+t1, 10, 80, mPaint);
                c.drawText("gap=" +mProfileTimes[0], 10, 100, mPaint);
                c.drawText("calc="+mProfileTimes[1], 10, 120, mPaint);
                c.drawText("rend="+mProfileTimes[2], 10, 140, mPaint);
                c.drawText("draw="+mProfileTimes[3], 10, 160, mPaint);
            }

            c.restore();
        }
    }

    private class OrientationReader implements SensorEventListener {
        private Sensor mSensor;
        public float[] mVal = new float[3];
        private float mRange;

        public OrientationReader() {
            SensorManager sm = (SensorManager)getSystemService(SENSOR_SERVICE);
            mSensor = sm.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
            mRange = mSensor.getMaximumRange();
        }

        public void onResume() {
            SensorManager sm = (SensorManager)getSystemService(SENSOR_SERVICE);
            sm.registerListener(this, mSensor, SensorManager.SENSOR_DELAY_GAME);
        }

        public void onPause() {
            SensorManager sm = (SensorManager)getSystemService(SENSOR_SERVICE);
            sm.unregisterListener(this);
        }

        public void onSensorChanged(SensorEvent event) {
            if(event.sensor == mSensor) {
                for(int i=0; i<3; i++) {
                    mVal[i] = event.values[i] / mRange;
                }
            }
        }

        public void onAccuracyChanged(Sensor sensor, int accuracy) { }
    }
}
