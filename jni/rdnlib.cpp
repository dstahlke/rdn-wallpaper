#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include <android/log.h>
#include <android/bitmap.h>
#include <jni.h>

#define LOG_TAG "rdn"
#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

struct Grid {
    Grid(int _w, int _h, int _n) :
        w(_w), h(_h), n(_n),
        wh(w*h), whn(wh*n),
        A(new float[wh*n])
    { }

    ~Grid() {
        delete(A);
    }

    float *getChannel(int i) {
        return A + wh*i;
    }

    int w, h, n, wh, whn;
    float *A;
};

struct FunctionBase {
    virtual ~FunctionBase() { }

    virtual void compute_dx_dt(Grid &Gi, Grid &Go, int w, int h) {
        LOGE("method compute_dx_dt must be overridden");
    }

    virtual void get_background_val(float &A, float &B) {
        LOGE("method get_background_val must be overridden");
    }

    virtual void get_seed_val(float &A, float &B, int seed_idx) {
        LOGE("method get_seed_val must be overridden");
    }

    virtual void set_params(float *p) {
        LOGE("method set_params must be overridden");
    }

    virtual float get_dt() {
        LOGE("method set_params must be overridden");
    }
};

struct GinzburgLandau : public FunctionBase {
    GinzburgLandau() :
        D(2.0F),
        alpha( 0.9F),
        beta (-2.5F)
    { }

    virtual void get_background_val(float &A, float &B) {
        A = B = 0;
    }

    virtual void get_seed_val(float &A, float &B, int seed_idx) {
        switch(seed_idx % 2) {
            case 0:  A = 0; B = 1; break;
            default: A = 1; B = 0; break;
        }
    }

    virtual void set_params(float *p) {
        D = p[0];
        alpha = p[1];
        beta = p[2];
    }

    virtual float get_dt() {
        // Stability condition determined by experimentation.
        return 0.1f / D / (1 + fabs(alpha));
    }

    virtual void compute_dx_dt(Grid &Gi, Grid &Go, int w, int h) {
        float *Ai = Gi.getChannel(0);
        float *Bi = Gi.getChannel(1);
        float *Ao = Go.getChannel(0);
        float *Bo = Go.getChannel(1);
        for(int y=0; y<h; y++) {
            int yl = y>  0 ? y-1 : h-1;
            int yr = y<h-1 ? y+1 :   0;
            for(int x=0; x<w; x++) {
                int xl = x>  0 ? x-1 : w-1;
                int xr = x<w-1 ? x+1 :   0;
                float da =
                    Ai[y *w + x ] * (-4) +
                    Ai[yl*w + x ] +
                    Ai[yr*w + x ] +
                    Ai[y *w + xl] +
                    Ai[y *w + xr];
                float db =
                    Bi[y *w + x ] * (-4) +
                    Bi[yl*w + x ] +
                    Bi[yr*w + x ] +
                    Bi[y *w + xl] +
                    Bi[y *w + xr];
                float ai = Ai[y*w+x];
                float bi = Bi[y*w+x];
                float r2 = ai*ai + bi*bi;

                Ao[y*w+x] = (1-r2)*ai + beta*r2*bi + D*(da - alpha*db);
                Bo[y*w+x] = (1-r2)*bi - beta*r2*ai + D*(db + alpha*da);
            }
        }
    }

    float D, alpha, beta;
};

struct GrayScott : public FunctionBase {
    GrayScott() :
        D(2.0F),
        phi( 2.8F),
        mu (33.7F)
    { }

    virtual void get_background_val(float &A, float &B) {
        float disc = sqrtf(1 - 4 * phi * phi / mu);
        A = (1 - disc)/2;
        B = (1 + disc)/2/phi;
    }

    virtual void get_seed_val(float &A, float &B, int seed_idx) {
        get_background_val(A, B);
        switch(seed_idx % 2) {
            case 0:  A += 0.0F; B += 0.2F; break;
            default: A += 0.2F; B += 0.0F; break;
        }
    }

    virtual void set_params(float *p) {
        D = p[0];
        phi = p[1];
        mu = p[2];
    }

    virtual float get_dt() {
        // Stability condition determined by experimentation.
        return 0.1f / D;
    }

    virtual void compute_dx_dt(Grid &Gi, Grid &Go, int w, int h) {
        float *Ai = Gi.getChannel(0);
        float *Bi = Gi.getChannel(1);
        float *Ao = Go.getChannel(0);
        float *Bo = Go.getChannel(1);
        for(int y=0; y<h; y++) {
            int yl = y>  0 ? y-1 : h-1;
            int yr = y<h-1 ? y+1 :   0;
            for(int x=0; x<w; x++) {
                int xl = x>  0 ? x-1 : w-1;
                int xr = x<w-1 ? x+1 :   0;
                float da =
                    Ai[y *w + x ] * (-4) +
                    Ai[yl*w + x ] +
                    Ai[yr*w + x ] +
                    Ai[y *w + xl] +
                    Ai[y *w + xr];
                float db =
                    Bi[y *w + x ] * (-4) +
                    Bi[yl*w + x ] +
                    Bi[yr*w + x ] +
                    Bi[y *w + xl] +
                    Bi[y *w + xr];
                float ai = Ai[y*w+x];
                float bi = Bi[y*w+x];

                Ao[y*w+x] = 1 - ai - mu*ai*bi*bi + D*da;
                Bo[y*w+x] = mu*ai*bi*bi - phi*bi + D*db;
            }
        }
    }

    float D, phi, mu;
};

struct RdnGrids {
    RdnGrids(int _w, int _h) :
        w(_w), h(_h),
        gridY  (w, h, 2),
        gridK1 (w, h, 2),
        gridK2 (w, h, 2),
        gridTmp(w, h, 2)
    { }

    void reset_grid(FunctionBase *fn) {
        for(int y = 0; y < h; y++) {
            float *gridYA = gridY.getChannel(0) + y * w;
            float *gridYB = gridY.getChannel(1) + y * w;
            for(int x = 0; x < w; x++) {
                fn->get_background_val(gridYA[x], gridYB[x]);
            }
        }

        for(int seed_idx = 0; seed_idx < 10; seed_idx++) {
            int sr = 10;
            int x0 = rand() % (w - sr);
            int y0 = rand() % (h - sr);
            for(int y = y0; y < y0+sr; y++) {
                float *gridYA = gridY.getChannel(0) + y * w;
                float *gridYB = gridY.getChannel(1) + y * w;
                for(int x = x0; x < x0+sr; x++) {
                    fn->get_seed_val(gridYA[x], gridYB[x], seed_idx);
                }
            }
        }
    }

    void step(FunctionBase *fn) {
        for(int iter=0; iter<7; iter++) {
            fn->compute_dx_dt(gridY, gridK1, w, h);

            //float max = 0;
            //for(int i=0; i<gridY.whn; i++) {
            //    float v = fabs(gridK1.A[i]);
            //    if(v > max) max = v;
            //}
            ////LOGI("max=%g", max);
            //if(max < 1) max = 1;
            //float dt = 0.1 / max;

            float dt = fn->get_dt();
            //LOGI("dt=%g", dt);

            int second_order = 0;
            if(second_order) {
                for(int i=0; i<gridY.whn; i++) {
                    gridTmp.A[i] = gridY.A[i] + gridK1.A[i] * dt/2;
                }
                fn->compute_dx_dt(gridTmp, gridK2, w, h);
                for(int i=0; i<gridY.whn; i++) {
                    gridY.A[i] += gridK2.A[i] * dt;
                }
            } else {
                for(int i=0; i<gridY.whn; i++) {
                    gridY.A[i] += gridK1.A[i] * dt;
                }
            }
        }
        //LOGI("pixel=%g,%g,%g,%g",
        //    gridY.getChannel(0)[0], gridY.getChannel(1)[0],
        //    gridK1.getChannel(0)[0], gridK1.getChannel(1)[0]);
    }

    void draw(void *pixels, int stride, FunctionBase *fn) {
        for(int y = 0; y < h; y++) {
            uint32_t *line = (uint32_t *)((char *)pixels + y * stride);
            float *gridYA = gridY.getChannel(0) + y * w;
            float *gridYB = gridY.getChannel(1) + y * w;
            float *gridKA = gridK1.getChannel(0) + y * w;
            float *gridKB = gridK1.getChannel(1) + y * w;
            for(int x = 0; x < w; x++) {
                float rv = gridYA[x]*gridYA[x] + gridYB[x]*gridYB[x];
                float rk = gridKA[x]*gridKA[x] + gridKB[x]*gridKB[x];
                int red   = (int)((rv-rk/4) * 200);
                int green = (int)((rv-rk/2) * 200);
                int blue  = green + (int)(gridYA[x] * 30);

                if(red < 0) red = 0;
                if(red > 255) red = 255;
                if(green < 0) green = 0;
                if(green > 255) green = 255;
                if(blue < 0) blue = 0;
                if(blue > 255) blue = 255;
                line[x] = 0xff000000 + (blue<<16) + (green<<8) + red;
            }
        }
    }

    int w, h;
    Grid gridY;
    Grid gridK1;
    Grid gridK2;
    Grid gridTmp;
};

RdnGrids *rdn = NULL;
FunctionBase *fn_gl = new GinzburgLandau();
FunctionBase *fn_gs = new GrayScott();
FunctionBase *fn_list[] = { fn_gl, fn_gs };
FunctionBase *fn = fn_gl;

extern "C" {
    JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_renderFrame(
        JNIEnv *env, jobject obj, jobject bitmap);
    JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_setParams(
        JNIEnv *env, jobject obj, jint fn_idx, jfloatArray params);
    JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_resetGrid(
        JNIEnv *env, jobject obj);
};

JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_renderFrame(
    JNIEnv *env, jobject obj, jobject bitmap
) {
    int ret;
    AndroidBitmapInfo info;
    if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0) {
        LOGE("AndroidBitmap_getInfo() failed ! error=%d", ret);
        return;
    }

    if (info.format != ANDROID_BITMAP_FORMAT_RGBA_8888) {
        LOGE("Bitmap format is not RGBA_8888 !");
        return;
    }

    void *pixels;
    if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0) {
        LOGE("AndroidBitmap_lockPixels() failed ! error=%d", ret);
    }

    if (!rdn || rdn->w != info.width || rdn->h != info.height) {
        delete(rdn);
        rdn = new RdnGrids(info.width, info.height);
        rdn->reset_grid(fn);
    }

    rdn->step(fn);
    rdn->draw(pixels, info.stride, fn);
}

JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_setParams(
    JNIEnv *env, jobject obj, jint fn_idx, jfloatArray params_in
) {
    fn = fn_list[fn_idx];
    jfloat *params = env->GetFloatArrayElements(params_in, NULL);
    fn->set_params((float *)params);
    env->ReleaseFloatArrayElements(params_in, params, JNI_ABORT);
    //rdn->reset_grid();
}

JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_resetGrid(
    JNIEnv *env, jobject obj
) {
    rdn->reset_grid(fn);
}
