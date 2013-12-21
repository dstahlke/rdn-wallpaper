package org.stahlke.rdnwallpaper;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.ColorMatrix;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.opengl.GLU;
import android.os.SystemClock;
import android.preference.PreferenceManager;
import android.service.wallpaper.WallpaperService;
import android.util.Log;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.WindowManager;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.util.concurrent.locks.ReentrantLock;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;
import javax.microedition.khronos.opengles.GL11;
import javax.microedition.khronos.opengles.GL11Ext;

import net.rbgrn.android.glwallpaperservice.GLWallpaperService;

class RdnRenderer implements
    GLWallpaperService.Renderer,
    SharedPreferences.OnSharedPreferenceChangeListener
{
    // jni methods
    public static native void evolve();
    public static native void renderFrame(ByteBuffer bitmap, int w, int h, int offset,
            int dir, float acc_x, float acc_y, float acc_z);
    public static native void setParams(int fn_idx, float[] params, int pal_idx);
    public static native void setColorMatrix(float[] cm);
    public static native void resetGrid();

    static {
        System.loadLibrary("rdnlib");
    }

    private static final String TAG = RdnWallpaper.TAG;
    private static final boolean DEBUG = RdnWallpaper.DEBUG;
    private static final int bpp = 3;

    private Context mContext;
    private int mRes = 4;
    private int mRepeatX = 1;
    private int mRepeatY = 2;
    private long mLastDrawTime;
    private int mWidth;
    private int mHeight;
    private int mGridW;
    private int mGridH;
    private int mTexW;
    private int mTexH;
    private SharedPreferences mPrefs;
    private ReentrantLock mDrawLock = new ReentrantLock();
    private ByteBuffer mPixelBuffer;
    private int mTextureId = -1;

    private float[] mProfileAccum = new float[4];
    private float[] mProfileTimes = new float[4];
    private int mProfileTicks = 0;

    private AccelerometerReader mAccelerometer;

    // http://stackoverflow.com/a/15119089/1048959
    private static void adjustHue(ColorMatrix cm, float value) {
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

    private static int nextPow2(int x) {
        x -= 1;
        int y = 1;
        while(x > 0) {
            x >>= 1;
            y <<= 1;
        }
        return y;
    }

    RdnRenderer(Context context) {
        mContext = context;

        mAccelerometer = new AccelerometerReader(context);

        PreferenceManager.setDefaultValues(context,
                R.xml.prefs, false);

        mPrefs = PreferenceManager
                .getDefaultSharedPreferences(context);
        mPrefs.registerOnSharedPreferenceChangeListener(this);

        setParamsToPrefs();

        onVisibilityChanged(false);
    }

    public void release() {
        onVisibilityChanged(false);
        mPrefs.unregisterOnSharedPreferenceChangeListener(this);
        mAccelerometer.onPause();
    }

    public void onVisibilityChanged(boolean visible) {
        if(visible) {
            mAccelerometer.onResume();
        } else {
            mAccelerometer.onPause();
        }
    }

    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
    }

    public void onDrawFrame(GL10 gl10) {
        mDrawLock.lock(); try {
            onDrawFrame_inner(gl10);
        } finally { mDrawLock.unlock(); }
    }

    public void onDrawFrame_inner(GL10 gl10) {
        GL11 gl = (GL11)gl10;

        long t1 = SystemClock.uptimeMillis();

        evolve();

        long t2 = SystemClock.uptimeMillis();

        renderFrame(mPixelBuffer, mGridW, mGridH/2, 0, 0,
                mAccelerometer.mVal[0],
                mAccelerometer.mVal[1],
                mAccelerometer.mVal[2]);

        if(mRepeatY > 1) {
            renderFrame(mPixelBuffer, mGridW, mGridH/2, mGridW*(mGridH/2)*bpp, 1,
                    mAccelerometer.mVal[0],
                    mAccelerometer.mVal[1],
                    mAccelerometer.mVal[2]);
        }

        long t3 = SystemClock.uptimeMillis();

        gl.glClearColorx(0, 0, 0, 0);
        gl.glClear(GL11.GL_COLOR_BUFFER_BIT);

        gl.glBindTexture(GL11.GL_TEXTURE_2D, mTextureId);
        gl.glTexSubImage2D(GL11.GL_TEXTURE_2D, 0, 0, 0, mGridW, mGridH,
                           GL11.GL_RGB, GL11.GL_UNSIGNED_BYTE, mPixelBuffer);

        for(int x=0; x<mRepeatX; x++)
        for(int y=0; y<(mRepeatY+1)/2; y++) {
            float x0 = -1.0f + 2.0f*x/mRepeatX;
            float x1 = -1.0f + 2.0f*(x+1)/mRepeatX;
            float y0 = -1.0f + 2.0f*y/(mRepeatY/2.0f);
            float y1 = -1.0f + 2.0f*(y+1)/(mRepeatY/2.0f);

            ByteBuffer vbb = ByteBuffer.allocateDirect(12 * 4);
            vbb.order(ByteOrder.nativeOrder());
            FloatBuffer vertexBuffer = vbb.asFloatBuffer();
            vertexBuffer.put(new float[] {
                x0, y0, 0.0f,
                x1, y0, 0.0f,
                x0, y1, 0.0f,
                x1, y1, 0.0f
            });
            vertexBuffer.position(0);

            float tx = (float)mGridW / mTexW;
            float ty = (float)mGridH / mTexH;
            ByteBuffer tbb = ByteBuffer.allocateDirect(8 * 4);
            tbb.order(ByteOrder.nativeOrder());
            FloatBuffer texBuffer = tbb.asFloatBuffer();
            texBuffer.put(new float[] {
                0.0f, ty,
                tx, ty,
                0.0f, 0.0f,
                tx, 0.0f
            });
            texBuffer.position(0);

            gl.glEnableClientState(GL10.GL_VERTEX_ARRAY);
            gl.glVertexPointer(3, GL10.GL_FLOAT, 0, vertexBuffer);
            gl.glEnableClientState(GL10.GL_TEXTURE_COORD_ARRAY);
            gl.glTexCoordPointer(2, GL10.GL_FLOAT, 0, texBuffer);

            gl.glDrawArrays(GL10.GL_TRIANGLE_STRIP, 0, 4);
        }

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

            if(DEBUG) {
                Log.i(TAG,
                    "gap=" +mProfileTimes[0]+
                    ", calc="+mProfileTimes[1]+
                    ", rend="+mProfileTimes[2]+
                    ", draw="+mProfileTimes[3]+
                    ", size="+mGridW+","+mGridH+
                    ", tex="+mTexW+","+mTexH);
            }
        }

        Thread.yield();
    }

    public void onSurfaceChanged(GL10 gl10, int width, int height) {
        mDrawLock.lock(); try {
            onSurfaceChanged_inner(gl10, width, height);
        } finally { mDrawLock.unlock(); }
    }

    private void onSurfaceChanged_inner(GL10 gl10, int width, int height) {
        GL11 gl = (GL11)gl10;

        rdnSetSize(width, height);
        mPixelBuffer = ByteBuffer.allocateDirect(mGridW*mGridH*bpp);

        gl.glShadeModel(GL11.GL_FLAT);
        gl.glFrontFace(GL11.GL_CCW);

        gl.glEnable(GL11.GL_TEXTURE_2D);

        gl.glViewport(0, 0, width, height);

        gl.glMatrixMode(GL11.GL_PROJECTION);
        gl.glLoadIdentity();

        gl.glMatrixMode(GL10.GL_MODELVIEW);
        gl.glLoadIdentity();

        int rot_key = ((WindowManager)mContext.getSystemService(Context.WINDOW_SERVICE)).
            getDefaultDisplay().getRotation();
        int rot_ang =
            (rot_key == Surface.ROTATION_90) ? 90 :
            (rot_key == Surface.ROTATION_180) ? 180 :
            (rot_key == Surface.ROTATION_270) ? 270 : 0;
        gl.glRotatef(rot_ang, 0.0f, 0.0f, 1.0f);

        if (mTextureId != -1) {
            gl.glDeleteTextures(1, new int[] { mTextureId }, 0);
        }

        int[] textures = new int[1];
        gl.glGenTextures(1, textures, 0);
        mTextureId = textures[0];

        gl.glBindTexture(GL11.GL_TEXTURE_2D, mTextureId);

        gl.glTexParameterf(GL10.GL_TEXTURE_2D,
                           GL10.GL_TEXTURE_MAG_FILTER,
                           GL10.GL_LINEAR);
        gl.glTexParameterf(GL10.GL_TEXTURE_2D,
                           GL10.GL_TEXTURE_MIN_FILTER,
                           GL10.GL_LINEAR);
        gl.glTexParameterf(GL10.GL_TEXTURE_2D, GL10.GL_TEXTURE_WRAP_S,
                           GL10.GL_CLAMP_TO_EDGE);
        gl.glTexParameterf(GL10.GL_TEXTURE_2D, GL10.GL_TEXTURE_WRAP_T,
                           GL10.GL_CLAMP_TO_EDGE);
        //gl.glTexParameteri(GL10.GL_TEXTURE_2D, GL10.GL_TEXTURE_WRAP_S, GL10.GL_REPEAT);
        //gl.glTexParameteri(GL10.GL_TEXTURE_2D, GL10.GL_TEXTURE_WRAP_T, GL10.GL_REPEAT);

        // It seems that pow2 sizes are not needed unless GL_REPEAT is used, but it
        // never hurts to be careful.
        mTexW = nextPow2(mGridW);
        mTexH = nextPow2(mGridH);

        gl.glTexImage2D(GL11.GL_TEXTURE_2D, 0, GL11.GL_RGB, mTexW, mTexH,
                0, GL11.GL_RGB, GL11.GL_UNSIGNED_BYTE, null);
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

        float newHue = RdnPrefs.getHueVal(mContext);

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
        if(newRes     == 0) newRes     = RdnWallpaper.getDefaultRes    (mWidth, mHeight);
        if(newRepeatX == 0) newRepeatX = RdnWallpaper.getDefaultRepeatX(mWidth, mHeight);
        if(newRepeatY == 0) newRepeatY = RdnWallpaper.getDefaultRepeatY(mWidth, mHeight);
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

    private void rdnSetSize(int width, int height) {
        if(height > width) {
            mWidth = width;
            mHeight = height;
        } else {
            mWidth = height;
            mHeight = width;
        }

        // might need to update resolution/tiling
        setParamsToPrefs();

        reshapeGrid();
    }

    private void reshapeGrid() {
        mGridW = Math.max(4,  mWidth / mRes / mRepeatX);
        mGridH = Math.max(4, mHeight / mRes / mRepeatY) * 2;
        mGridW -= mGridW % 4;
        if(DEBUG) Log.i(TAG, "wh="+mWidth+","+mHeight);
        if(DEBUG) Log.i(TAG, "grid="+mGridW+","+mGridH);
    }

    private class AccelerometerReader implements SensorEventListener {
        private Context mContext;
        private Sensor mSensor;
        public float[] mVal = new float[3];
        private float mRange;

        public AccelerometerReader(Context context) {
            mContext = context;
            SensorManager sm = (SensorManager)mContext.getSystemService(mContext.SENSOR_SERVICE);
            mSensor = sm.getDefaultSensor(Sensor.TYPE_ACCELEROMETER);
            mRange = mSensor.getMaximumRange();
        }

        public void onResume() {
            SensorManager sm = (SensorManager)mContext.getSystemService(mContext.SENSOR_SERVICE);
            sm.registerListener(this, mSensor, SensorManager.SENSOR_DELAY_GAME);
        }

        public void onPause() {
            SensorManager sm = (SensorManager)mContext.getSystemService(mContext.SENSOR_SERVICE);
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
