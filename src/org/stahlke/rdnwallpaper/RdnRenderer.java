package org.stahlke.rdnwallpaper;

import android.content.SharedPreferences;
import android.content.Context;
import android.preference.PreferenceManager;
import android.graphics.ColorMatrix;
import android.os.SystemClock;
import android.service.wallpaper.WallpaperService;
import android.util.Log;
import android.view.MotionEvent;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.opengl.GLU;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.util.concurrent.locks.ReentrantLock;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;
import javax.microedition.khronos.opengles.GL11; // FIXME - needed?
import javax.microedition.khronos.opengles.GL11Ext;

import net.rbgrn.android.glwallpaperservice.GLWallpaperService;

class RdnRenderer implements
    GLWallpaperService.Renderer,
    SharedPreferences.OnSharedPreferenceChangeListener
{
    // jni method
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

    private final long mFrameInterval = 50;
    private int mRes = 4;
    private int mRepeatX = 1;
    private int mRepeatY = 2;
    private long mLastDrawTime;
    private int mWidth;
    private int mHeight;
    private boolean mDoRotate;
    private int mGridW;
    private int mGridH;
    private SharedPreferences mPrefs;
    private ReentrantLock mDrawLock = new ReentrantLock();
    private Context context;
    private ByteBuffer pixelBuffer;
    private int bpp = 4; // FIXME
    private int glTextureId = -1;

    private float[] mProfileAccum = new float[4];
    private float[] mProfileTimes = new float[4];
    private int mProfileTicks = 0;

    private OrientationReader mOrientation;

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

    RdnRenderer(RdnWallpaper parent) {
        // FIXME - is `parent` needed?
        PreferenceManager.setDefaultValues(parent,
                R.xml.prefs, false);

        mPrefs = PreferenceManager
                .getDefaultSharedPreferences(parent);
        mPrefs.registerOnSharedPreferenceChangeListener(this);

        setParamsToPrefs();
    }

    public void setContext(Context value) {
        context = value;
        mOrientation = new OrientationReader(context);
        // FIXME
        mOrientation.onResume();
    }

    public void release() {
        // TODO stuff to release
    }

    public void onSurfaceCreated(GL10 gl, EGLConfig config) {
    }

    public void onDrawFrame(GL10 gl10) {
        GL11 gl = (GL11)gl10;

        long t1 = SystemClock.uptimeMillis();

        evolve();

        long t2 = SystemClock.uptimeMillis();

        pixelBuffer.rewind();
        renderFrame(pixelBuffer, mGridW, mGridH/2, 0, 0,
                mOrientation.mVal[0],
                mOrientation.mVal[1],
                mOrientation.mVal[2]);
        pixelBuffer.rewind(); // FIXME - needed?
        renderFrame(pixelBuffer, mGridW, mGridH/2, mGridW*(mGridH/2)*3, 1,
                mOrientation.mVal[0],
                mOrientation.mVal[1],
                mOrientation.mVal[2]);

        long t3 = SystemClock.uptimeMillis();

        // Clear the surface
        gl.glClearColorx(0, 0, 0, 0);
        gl.glClear(GL11.GL_COLOR_BUFFER_BIT);

        // Choose the texture
        gl.glBindTexture(GL11.GL_TEXTURE_2D, glTextureId);

        // Update the texture
        //gl.glTexSubImage2D(GL11.GL_TEXTURE_2D, 0, 0, 0, mGridW, mGridH,
        //                   GL11.GL_RGB, GL11.GL_UNSIGNED_BYTE, pixelBuffer);
        gl.glTexImage2D(GL11.GL_TEXTURE_2D, 0, GL11.GL_RGB, mGridW, mGridH,
                0, GL11.GL_RGB, GL11.GL_UNSIGNED_BYTE, pixelBuffer);

        int[] textureCrop = new int[4];
        textureCrop[0] = 0;
        textureCrop[1] = 0;
        textureCrop[2] = mGridW;
        textureCrop[3] = mGridH;
        // Draw the texture on the surface
        gl.glTexParameteriv(GL10.GL_TEXTURE_2D, GL11Ext.GL_TEXTURE_CROP_RECT_OES, textureCrop, 0);
        gl.glTexParameteri(GL10.GL_TEXTURE_2D, GL10.GL_TEXTURE_WRAP_S, GL10.GL_REPEAT);
        gl.glTexParameteri(GL10.GL_TEXTURE_2D, GL10.GL_TEXTURE_WRAP_T, GL10.GL_REPEAT);

        //for(int x = 0; x < mRepeatX; x++)
        //for(int y = 0; y < mRepeatY; y++) {
        //    if(y % 2 == 0) {
        //        ((GL11Ext) gl).glDrawTexiOES(
        //            x*mGridW*mRes, y*mGridH*mRes, 0, mGridW*mRes-1, mGridH*mRes-1);
        //    }
        //}

        ByteBuffer vbb = ByteBuffer.allocateDirect(12 * 4);
        vbb.order(ByteOrder.nativeOrder());
        FloatBuffer vertexBuffer = vbb.asFloatBuffer();
        vertexBuffer.put(new float[] {
            -1.0f, -1.0f, 0.0f,  // 0. left-bottom-front
             1.0f, -1.0f, 0.0f,  // 1. right-bottom-front
            -1.0f,  1.0f, 0.0f,  // 2. left-top-front
             1.0f,  1.0f, 0.0f   // 3. right-top-front
        });
        vertexBuffer.position(0);

        ByteBuffer tbb = ByteBuffer.allocateDirect(8 * 4);
        tbb.order(ByteOrder.nativeOrder());
        FloatBuffer texBuffer = tbb.asFloatBuffer();
        texBuffer.put(new float[] {
            0.0f, mRepeatY/2.0f,
            mRepeatX, mRepeatY/2.0f,
            0.0f, 0.0f,
            mRepeatX, 0.0f
        });
        texBuffer.position(0);

        gl.glEnableClientState(GL10.GL_VERTEX_ARRAY);
        gl.glVertexPointer(3, GL10.GL_FLOAT, 0, vertexBuffer);
        gl.glEnableClientState(GL10.GL_TEXTURE_COORD_ARRAY);  // Enable texture-coords-array (NEW)
        gl.glTexCoordPointer(2, GL10.GL_FLOAT, 0, texBuffer); // Define texture-coords buffer (NEW)

        gl.glLoadIdentity();
        gl.glDrawArrays(GL10.GL_TRIANGLE_STRIP, 0, 4);

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
                    ", draw="+mProfileTimes[3]);
            }
        }

        try {
            Thread.sleep(10);
        } catch (InterruptedException e) {}
    }

    public void onSurfaceChanged(GL10 gl10, int width, int height) {
        GL11 gl = (GL11)gl10;

        rdnSetSize(width, height);

        pixelBuffer = ByteBuffer.allocateDirect(mGridW*mGridH*bpp);

        gl.glShadeModel(GL11.GL_FLAT);
        gl.glFrontFace(GL11.GL_CCW);

        gl.glEnable(GL11.GL_TEXTURE_2D);

        gl.glMatrixMode(GL11.GL_PROJECTION);
        gl.glLoadIdentity();
        //gl.glOrthof(0.0f, width, 0.0f, height, 0.0f, 1.0f);
        //gl.glOrthof(0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);

        gl.glMatrixMode(GL10.GL_MODELVIEW);
        gl.glLoadIdentity();

        if (glTextureId != -1) {
            gl.glDeleteTextures(1, new int[] { glTextureId }, 0);
        }

        int[] textures = new int[1];
        gl.glGenTextures(1, textures, 0);
        glTextureId = textures[0];

        // we want to modify this texture so bind it
        gl.glBindTexture(GL11.GL_TEXTURE_2D, glTextureId);

        // GL_LINEAR gives us smoothing since the texture is larger than the screen
        gl.glTexParameterf(GL10.GL_TEXTURE_2D,
                           GL10.GL_TEXTURE_MAG_FILTER,
                           GL10.GL_LINEAR);
        gl.glTexParameterf(GL10.GL_TEXTURE_2D,
                           GL10.GL_TEXTURE_MIN_FILTER,
                           GL10.GL_LINEAR);
        // repeat the edge pixels if a surface is larger than the texture
        gl.glTexParameterf(GL10.GL_TEXTURE_2D, GL10.GL_TEXTURE_WRAP_S,
                           GL10.GL_CLAMP_TO_EDGE);
        gl.glTexParameterf(GL10.GL_TEXTURE_2D, GL10.GL_TEXTURE_WRAP_T,
                           GL10.GL_CLAMP_TO_EDGE);

        // and init the GL texture with the pixels
        gl.glTexImage2D(GL11.GL_TEXTURE_2D, 0, GL11.GL_RGB, mGridW, mGridH,
                0, GL11.GL_RGB, GL11.GL_UNSIGNED_BYTE, pixelBuffer);
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
        // FIXME - screen rotate doesn't work anymore
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
        mGridW = 128;
        mGridH = 256; // FIXME
        if(DEBUG) Log.i(TAG, "wh="+mWidth+","+mHeight);
        if(DEBUG) Log.i(TAG, "grid="+mGridW+","+mGridH);
    }

    private class OrientationReader implements SensorEventListener {
        private Context mContext;
        private Sensor mSensor;
        public float[] mVal = new float[3];
        private float mRange;

        public OrientationReader(Context context) {
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
