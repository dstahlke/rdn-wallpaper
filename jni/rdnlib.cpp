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

    void fixCheckerBoard() {
        for(int chan=0; chan<n; chan++) {
            float *p = getChannel(chan);
            for(int y=0; y<h; y++) {
                int yl = y>  0 ? y-1 : h-1;
                int yr = y<h-1 ? y+1 :   0;
                for(int x=0; x<w; x++) {
                    int xl = x>  0 ? x-1 : w-1;
                    int xr = x<w-1 ? x+1 :   0;
                    p[y*w + x] = (
                        p[y *w + x ] * 4 +
                        p[yl*w + x ] +
                        p[yr*w + x ] +
                        p[y *w + xl] +
                        p[y *w + xr]
                    ) / 8;
                }
            }
        }
    }

    int w, h, n, wh, whn;
    float *A;
};

class Palette {
public:
    virtual void render_line(uint32_t *pix_line, Grid &G, Grid &DG, int y);
};

#define CLIP_BYTE(v) (v < 0 ? 0 : v > 255 ? 255 : v)
#define RGB24(r, g, b) (0xff000000 + (CLIP_BYTE(b)<<16) + (CLIP_BYTE(g)<<8) + CLIP_BYTE(r))

class PaletteGL0 : public Palette {
public:
    void render_line(uint32_t *pix_line, Grid &G, Grid &DG, int y) {
        if(y == 0) {
            avg_rv = accum_rv / cnt;
            avg_rk = accum_rk / cnt;
            accum_rv = 0;
            accum_rk = 0;
            cnt = 0;
        }
        float *gridA = G.getChannel(0) + y * G.w;
        float *gridB = G.getChannel(1) + y * G.w;
        float *gridDA = DG.getChannel(0) + y * G.w;
        float *gridDB = DG.getChannel(1) + y * G.w;
        for(int x = 0; x < G.w; x++) {
            float rv = gridA[x]*gridA[x] + gridB[x]*gridB[x];
            float rk = gridDA[x]*gridDA[x] + gridDB[x]*gridDB[x];
            accum_rv += rv;
            accum_rk += rk;
            rv /= avg_rv;
            rk /= avg_rk;
            int red   = (int)((rv-rk/4) * 150);
            int green = (int)((rv-rk/2) * 100);
            int blue  = green + (int)(gridA[x] * 30);

            pix_line[x] = RGB24(red, green, blue);
        }
        cnt += G.w;
    }

    int cnt;
    float accum_rv, accum_rk;
    float avg_rv, avg_rk;
};

class PaletteGL1 : public Palette {
public:
    void render_line(uint32_t *pix_line, Grid &G, Grid &DG, int y) {
        if(y == 0) {
            avg_rv = accum_rv / cnt;
            avg_rk = accum_rk / cnt;
            accum_rv = 0;
            accum_rk = 0;
            cnt = 0;
        }
        float *gridA = G.getChannel(0) + y * G.w;
        float *gridB = G.getChannel(1) + y * G.w;
        float *gridDA = DG.getChannel(0) + y * G.w;
        float *gridDB = DG.getChannel(1) + y * G.w;
        for(int x = 0; x < G.w; x++) {
            float rv = gridA[x]*gridA[x] + gridB[x]*gridB[x];
            float rk = gridA[x]*gridDA[x] + gridB[x]*gridDB[x];
            accum_rv += rv;
            accum_rk += fabs(rk);
            rv /= avg_rv;
            rk /= avg_rk;
            int red   = (int)(rv * 100);
            int green = (int)(-rk * 30);
            int blue  = green;

            pix_line[x] = RGB24(red, green, blue);
        }
        cnt += G.w;
    }

    int cnt;
    float accum_rv, accum_rk;
    float avg_rv, avg_rk;
};

class PaletteGS : public Palette {
public:
    void render_line(uint32_t *pix_line, Grid &G, Grid &DG, int y) {
        float *gridA = G.getChannel(0) + y * G.w;
        float *gridB = G.getChannel(1) + y * G.w;
        float *gridDA = DG.getChannel(0) + y * G.w;
        float *gridDB = DG.getChannel(1) + y * G.w;
        for(int x = 0; x < G.w; x++) {
            int red   = (int)((1-gridA[x]) * 200);
            int green = (int)(gridDA[x] * 20000);
            int blue  = (int)(gridB [x] * 1000);

            pix_line[x] = RGB24(red, green, blue);
        }
    }
};

Palette *pal_gl0 = new PaletteGL0();
Palette *pal_gl1 = new PaletteGL1();
Palette *pal_gs = new PaletteGS();

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

    virtual void set_params(float *p, int len) {
        LOGE("method set_params must be overridden");
    }

    virtual float get_dt() {
        LOGE("method get_dt must be overridden");
    }

    virtual Palette *get_palette(int id) {
        LOGE("method get_palette must be overridden");
    }
};

struct GinzburgLandau : public FunctionBase {
    GinzburgLandau() :
        D(2.0F),
        alpha(0.0625F),
        beta (1.0F   )
    { }

    virtual void get_background_val(float &U, float &V) {
        U = V = 0;
    }

    virtual void get_seed_val(float &U, float &V, int seed_idx) {
        U = ((seed_idx*5)%7)/7.0F*2.0F-1.0F;
        V = ((seed_idx*9)%13)/13.0F*2.0F-1.0F;
    }

    virtual void set_params(float *p, int len) {
        if(len != 3) {
            LOGE("params is wrong length: %d", len);
        }
        D     = *(p++);
        alpha = *(p++);
        beta  = *(p++);
        LOGE("D     = %f", D    );
        LOGE("alpha = %f", alpha);
        LOGE("beta  = %f", beta );
    }

    virtual float get_dt() {
        // Stability condition determined by experimentation.
        return 0.1f / D / (1 + fabs(alpha));
    }

    virtual void compute_dx_dt(Grid &Gi, Grid &Go, int w, int h) {
        float *Uibuf = Gi.getChannel(0);
        float *Vibuf = Gi.getChannel(1);
        float *Uobuf = Go.getChannel(0);
        float *Vobuf = Go.getChannel(1);
        for(int y=0; y<h; y++) {
            int yl = y>  0 ? y-1 : h-1;
            int yr = y<h-1 ? y+1 :   0;
            for(int x=0; x<w; x++) {
                int xl = x>  0 ? x-1 : w-1;
                int xr = x<w-1 ? x+1 :   0;
                float lU =
                    Uibuf[y *w + x ] * (-4) +
                    Uibuf[yl*w + x ] +
                    Uibuf[yr*w + x ] +
                    Uibuf[y *w + xl] +
                    Uibuf[y *w + xr];
                float lV =
                    Vibuf[y *w + x ] * (-4) +
                    Vibuf[yl*w + x ] +
                    Vibuf[yr*w + x ] +
                    Vibuf[y *w + xl] +
                    Vibuf[y *w + xr];
                float U = Uibuf[y*w+x];
                float V = Vibuf[y*w+x];
                float r2 = U*U + V*V;

                Uobuf[y*w+x] = U - (U - beta*V)*r2 + D*(lU - alpha*lV);
                Vobuf[y*w+x] = V - (V + beta*U)*r2 + D*(lV + alpha*lU);
                // From Ready (https://code.google.com/p/reaction-diffusion)
                //Uobuf[y*w+x] = DU*lU + alpha*U - gamma*V + (-beta*U + delta*V)*r2;
                //Vobuf[y*w+x] = DV*lV + alpha*V + gamma*U + (-beta*V - delta*U)*r2;
            }
        }
    }

    virtual Palette *get_palette(int id) {
        switch(id) {
            case 0: return pal_gl0;
            case 1: return pal_gl1;
            default: return pal_gl0;
        }
    }

    float D, alpha, beta;
};

struct GrayScott : public FunctionBase {
    GrayScott() :
        D(2.0F),
        F(0.01F),
        k(0.049F)
    { }

    virtual void get_background_val(float &A, float &B) {
        A=1; B=0;
    }

    virtual void get_seed_val(float &A, float &B, int seed_idx) {
        //get_background_val(A, B);
        //switch(seed_idx % 2) {
        //    case 0:  A += 0.0F; B += 0.1F; break;
        //    default: A += 0.1F; B += 0.0F; break;
        //}
        A = ((seed_idx*5)%7)/7.0F;
        B = ((seed_idx*9)%13)/13.0F;
    }

    virtual void set_params(float *p, int len) {
        if(len != 3) {
            LOGE("params is wrong length: %d", len);
        }
        D = p[0];
        F = p[1];
        k = p[2];
    }

    virtual float get_dt() {
        // Stability condition determined by experimentation.
        return 0.3f / D;
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

                //Ao[y*w+x] = 1 - ai - mu*ai*bi*bi + D*da;
                //Bo[y*w+x] = mu*ai*bi*bi - phi*bi + D*db;
                Ao[y*w+x] = 2*D*da - ai*bi*bi + F*(1-ai);
                Bo[y*w+x] =   D*db + ai*bi*bi - (F+k)*bi;
            }
        }
    }

    virtual Palette *get_palette(int id) {
        return pal_gs;
    }

    float D, F, k;
};

struct RdnGrids {
    RdnGrids(int _w, int _h) :
        w(_w), h(_h),
        dt(0.01), since_instable(10000),
        gridY  (w, h, 2),
        gridK1 (w, h, 2),
        gridK2 (w, h, 2),
        gridTmp(w, h, 2)
    { }

    void reset_grid(FunctionBase *fn) {
        float A, B;
        fn->get_background_val(A, B);
        for(int y = 0; y < h; y++) {
            float *gridYA = gridY.getChannel(0) + y * w;
            float *gridYB = gridY.getChannel(1) + y * w;
            for(int x = 0; x < w; x++) {
                gridYA[x] = A;
                gridYB[x] = B;
            }
        }

        for(int seed_idx = 0; seed_idx < 20; seed_idx++) {
            int sr = 20;
            float A, B;
            fn->get_seed_val(A, B, seed_idx);
            int x0 = rand() % (w - sr);
            int y0 = rand() % (h - sr);
            for(int y = y0; y < y0+sr; y++) {
                float *gridYA = gridY.getChannel(0) + y * w;
                float *gridYB = gridY.getChannel(1) + y * w;
                for(int x = x0; x < x0+sr; x++) {
                    gridYA[x] = A;
                    gridYB[x] = B;
                }
            }
        }

        dt = 0.01;
        since_instable = 10000;
    }

    int step_inner(FunctionBase *fn, float step_dt) {
        fn->compute_dx_dt(gridY, gridK1, w, h);

        //int second_order = 0;
        //if(second_order) {
        //    for(int i=0; i<gridY.whn; i++) {
        //        gridTmp.A[i] = gridY.A[i] + gridK1.A[i] * step_dt/2;
        //    }
        //    fn->compute_dx_dt(gridTmp, gridK2, w, h);
        //    for(int i=0; i<gridY.whn; i++) {
        //        gridY.A[i] += gridK2.A[i] * step_dt;
        //    }
        //} else {

        float limit = 0.001 / step_dt;
        int instable = 0;
        for(int j=0; j<gridY.n; j++) {
            float *p = gridK1.getChannel(j);
            int l = gridY.wh-2-2*gridY.w;
            int w2 = w+w;
            for(int i=0; i<l; i++) {
                if(
                    p[i   ] < limit && p[i+1   ] > limit && p[i+2   ] < limit &&
                    p[i+w ] > limit && p[i+1+w ] < limit && p[i+2+w ] > limit &&
                    p[i+w2] < limit && p[i+1+w2] > limit && p[i+2+w2] < limit
                ) {
                    instable++;
                    //LOGI("pixel=%g", p[i]);
                    break;
                }
            }
        }
        for(int i=0; i<gridY.whn; i++) {
            gridY.A[i] += gridK1.A[i] * step_dt;
        }

        return instable;
    }

    void step(FunctionBase *fn) {
        for(int iter=0; iter<5; iter++) {
            int instable = step_inner(fn, dt);

            if(instable) {
                LOGI("dt=%g si=%d", dt, since_instable);
                dt *= 0.99;
                LOGI("-> dt=%g", dt);
                gridY.fixCheckerBoard();
                // FIXME: could freeze UI:
                while(step_inner(fn, dt / 5.0)) {
                    dt *= 0.99;
                    LOGI("-> dt=%g", dt);
                }
                since_instable = 0;
            } else {
                since_instable++;
                if(since_instable > 1000) {
                    dt *= 1.01;
                } else {
                    dt *= 1.0001;
                }
            }

            // FIXME: tie to reset_grid or something?
            if(dt < 1e-6) dt = 1e-6;
            if(dt > 1e+1) dt = 1e+1;

            if(!isfinite(gridY.getChannel(0)[0])) {
                reset_grid(fn);
            }
        }
        //LOGI("pixel=%g,%g,%g,%g",
        //    gridY.getChannel(0)[0], gridY.getChannel(1)[0],
        //    gridK1.getChannel(0)[0], gridK1.getChannel(1)[0]);
    }

    void draw(void *pixels, int stride, FunctionBase *fn, Palette *pal) {
        for(int y = 0; y < h; y++) {
            uint32_t *pix_line = (uint32_t *)((char *)pixels + y * stride);
            pal->render_line(pix_line, gridY, gridK1, y);
        }
    }

    int w, h;
    float dt;
    int since_instable;
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
Palette *pal = fn->get_palette(0);

extern "C" {
    JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_renderFrame(
        JNIEnv *env, jobject obj, jobject bitmap);
    JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_setParams(
        JNIEnv *env, jobject obj, jint fn_idx, jfloatArray params, jint pal);
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
    rdn->draw(pixels, info.stride, fn, pal);
}

JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_setParams(
    JNIEnv *env, jobject obj, jint fn_idx, jfloatArray params_in, jint pal_idx
) {
    fn = fn_list[fn_idx];
    pal = fn->get_palette(pal_idx);
    jfloat *params = env->GetFloatArrayElements(params_in, NULL);
    jsize len = env->GetArrayLength(params_in);
    fn->set_params((float *)params, len);
    env->ReleaseFloatArrayElements(params_in, params, JNI_ABORT);
    //rdn->reset_grid();
    // FIXME
    if(rdn) rdn->since_instable = 10000;
}

JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_resetGrid(
    JNIEnv *env, jobject obj
) {
    rdn->reset_grid(fn);
}
