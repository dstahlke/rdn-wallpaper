#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <utility>

#include <android/log.h>
#include <android/bitmap.h>
#include <jni.h>

#define LOG_TAG "rdn"
#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

#define CLIP_BYTE(v) (v < 0 ? 0 : v > 255 ? 255 : v)

float color_matrix[20];

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

//    int detectCheckerBoard() {
//        float limit = 0.01;
//        float la = -limit;
//        float lb =  limit;
//        int instable = 0;
//        for(int j=0; j<n; j++) {
//            float *p = getChannel(j);
//            int l = wh-3-3*w;
//            int w2 = w*2;
//            int w3 = w*3;
//            for(int i=0; i<l; i++) {
//                if(
//            p[i   ] < la && p[i+1   ] > lb && p[i+2   ] < la && // p[i+3   ] > lb &&
//            p[i+w ] > lb && p[i+1+w ] < la && p[i+2+w ] > lb && // p[i+3+w ] < la &&
//            p[i+w2] < la && p[i+1+w2] > lb && p[i+2+w2] < la // && p[i+3+w2] > lb &&
//            //p[i+w3] > lb && p[i+1+w3] < la && p[i+2+w3] > lb && p[i+3+w3] < la
//                ) {
//                    instable++;
//                }
//            }
//        }
//        return instable;
//    }

    const int w, h, n, wh, whn;
    float *A;
};

class Palette {
public:
    virtual void render_line(uint32_t *pix_line, Grid &G, Grid &DG, Grid &LG, int y);

    static inline int to_rgb24(float r, float g, float b) {
        if(r < 0) r = 0;
        if(g < 0) g = 0;
        if(b < 0) b = 0;
        float *cm = color_matrix;
        float rp = r*cm[0] + g*cm[1] + b*cm[2] + cm[4]; cm += 5;
        float gp = r*cm[0] + g*cm[1] + b*cm[2] + cm[4]; cm += 5;
        float bp = r*cm[0] + g*cm[1] + b*cm[2] + cm[4]; cm += 5;
        int ri = int(rp);
        int gi = int(gp);
        int bi = int(bp);
        return 0xff000000 + (CLIP_BYTE(bi)<<16) + (CLIP_BYTE(gi)<<8) + CLIP_BYTE(ri);
    }
};

struct FunctionBase {
    virtual ~FunctionBase() { }

    virtual float get_diffusion_matrix(float m[]) {
        LOGE("method get_diffusion_matrix must be overridden");
    }

    virtual void compute_dx_dt(Grid &Gi, Grid &Go, Grid &Gl, int w, int y) {
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

    virtual Palette *get_palette(int id) {
        LOGE("method get_palette must be overridden");
    }
};

struct GinzburgLandau : public FunctionBase {
    GinzburgLandau() :
        D(2.0F),
        alpha(0.0625F),
        beta (1.0F   ),
        pal_gl0(new PaletteGL0()),
        pal_gl1(new PaletteGL1(*this))
    { }

    ~GinzburgLandau() {
        delete(pal_gl0);
        delete(pal_gl1);
    }

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

    virtual float get_diffusion_matrix(float m[]) {
        m[0] = D;       m[1] = -D*alpha;
        m[2] = D*alpha; m[3] = D;
        return D+(1+fabsf(alpha)); // FIXME
    }

    virtual void compute_dx_dt(Grid &Gi, Grid &Go, Grid &Gl, int w, int y) {
        float *Uibuf = Gi.getChannel(0) + y*w;
        float *Vibuf = Gi.getChannel(1) + y*w;
        float *Uobuf = Go.getChannel(0) + y*w;
        float *Vobuf = Go.getChannel(1) + y*w;

        for(int x=0; x<w; x++) {
            float  U = Uibuf[x];
            float  V = Vibuf[x];
            float r2 = U*U + V*V;

            Uobuf[x] = U - (U - beta*V)*r2; // + D*(lU - alpha*lV);
            Vobuf[x] = V - (V + beta*U)*r2; // + D*(lV + alpha*lU);
            // From Ready (https://code.google.com/p/reaction-diffusion)
            //Uobuf[x] = DU*lU + alpha*U - gamma*V + (-beta*U + delta*V)*r2;
            //Vobuf[x] = DV*lV + alpha*V + gamma*U + (-beta*V - delta*U)*r2;
        }
    }

    struct PaletteGL0 : public Palette {
        void render_line(uint32_t *pix_line, Grid &G, Grid &DG, Grid &LG, int y) {
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
                int red   = (rv-rk/4) * 150;
                int green = (rv-rk/2) * 100;
                int blue  = green + gridA[x] * 30;

                pix_line[x] = to_rgb24(red, green, blue);
            }
            cnt += G.w;
        }

        int cnt;
        float accum_rv, accum_rk;
        float avg_rv, avg_rk;
    };

    struct PaletteGL1 : public Palette {
        PaletteGL1(GinzburgLandau &x) : parent(x) { }

        void render_line(uint32_t *pix_line, Grid &G, Grid &DG, Grid &LG, int y) {
            if(y == 0) {
                avg_rv = accum_rv / cnt;
                accum_rv = 0;
                cnt = 0;
            }
            float *gridA = G.getChannel(0) + y * G.w;
            float *gridB = G.getChannel(1) + y * G.w;
            float *gridDA = DG.getChannel(0) + y * G.w;
            float *gridDB = DG.getChannel(1) + y * G.w;
            float *gridLA = LG.getChannel(0) + y * G.w;
            float *gridLB = LG.getChannel(1) + y * G.w;
            for(int x = 0; x < G.w; x++) {
                float rv = gridA[x]*gridA[x] + gridB[x]*gridB[x];
                float lv = gridDA[x]*gridLA[x] + gridDB[x]*gridLB[x];
                accum_rv += rv;
                rv /= avg_rv;
                lv /= avg_rv;
                int green = 0;
                int blue  = parent.D * lv * 500;
                int red   = rv * 100 + blue;

                pix_line[x] = to_rgb24(red, green, blue);
            }
            cnt += G.w;
        }

        int cnt;
        float accum_rv;
        float avg_rv;

        GinzburgLandau &parent;
    };

    virtual Palette *get_palette(int id) {
        switch(id) {
            case 0: return pal_gl0;
            case 1: return pal_gl1;
            default: return pal_gl0;
        }
    }

    float D, alpha, beta;
    Palette *pal_gl0;
    Palette *pal_gl1;
};

struct GrayScott : public FunctionBase {
    GrayScott() :
        D(2.0F),
        F(0.01F),
        k(0.049F),
        pal_gs0(new PaletteGS0()),
        pal_gs1(new PaletteGS1(*this))
    { }

    ~GrayScott() {
        delete(pal_gs1);
    }

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

    virtual float get_diffusion_matrix(float m[]) {
        m[0] = 2*D; m[1] = 0;
        m[2] = 0;   m[3] = D;
        float norm = 2*D;
        return norm;
    }

    virtual void compute_dx_dt(Grid &Gi, Grid &Go, Grid &Gl, int w, int y) {
        float *Ai = Gi.getChannel(0) + y*w;
        float *Bi = Gi.getChannel(1) + y*w;
        float *Ao = Go.getChannel(0) + y*w;
        float *Bo = Go.getChannel(1) + y*w;
        float *Al = Gl.getChannel(0) + y*w;
        float *Bl = Gl.getChannel(1) + y*w;

        for(int x=0; x<w; x++) {
            float da = Al[x];
            float db = Bl[x];
            float ai = Ai[x];
            float bi = Bi[x];

            //Ao[x] = 1 - ai - mu*ai*bi*bi + D*da;
            //Bo[x] = mu*ai*bi*bi - phi*bi + D*db;
            Ao[x] = /* 2*D*da */ - ai*bi*bi + F*(1-ai);
            Bo[x] = /*   D*db + */ ai*bi*bi - (F+k)*bi;
        }
    }

    struct PaletteGS0 : public Palette {
        void render_line(uint32_t *pix_line, Grid &G, Grid &DG, Grid &LG, int y) {
            float *gridA = G.getChannel(0) + y * G.w;
            float *gridB = G.getChannel(1) + y * G.w;
            float *gridDA = DG.getChannel(0) + y * G.w;
            float *gridDB = DG.getChannel(1) + y * G.w;
            for(int x = 0; x < G.w; x++) {
                int red   = (1-gridA[x]) * 200;
                int green = gridDA[x] * 20000;
                int blue  = gridB [x] * 1000;

                pix_line[x] = to_rgb24(red, green, blue);
            }
        }
    };

    struct PaletteGS1 : public Palette {
        PaletteGS1(GrayScott &x) : parent(x) { }

        void render_line(uint32_t *pix_line, Grid &G, Grid &DG, Grid &LG, int y) {
            if(y == 0) {
                avg_rv = accum_rv / cnt;
                accum_rv = 0;
                cnt = 0;
            }
            float *gridA = G.getChannel(0) + y * G.w;
            float *gridB = G.getChannel(1) + y * G.w;
            float *gridDA = DG.getChannel(0) + y * G.w;
            float *gridDB = DG.getChannel(1) + y * G.w;
            float *gridLA = LG.getChannel(0) + y * G.w;
            float *gridLB = LG.getChannel(1) + y * G.w;
            for(int x = 0; x < G.w; x++) {
                float rv = parent.D * gridLB[x];
                float gv = parent.D * gridLA[x];
                if(rv > 0) accum_rv += rv;
                //float w = sqrtf(gridLA[x]*gridLA[x] + gridLB[x]*gridLB[x]);
                int red   = rv * 60000;
                int green = 0; //parent.D * gridLB[x] * 20000;
                int blue  = gv * 60000;

                pix_line[x] = to_rgb24(red, green, blue);
            }
            cnt += G.w;
        }

        int cnt;
        float accum_rv;
        float avg_rv;

        GrayScott &parent;
    };

    virtual Palette *get_palette(int id) {
        switch(id) {
            case 0: return pal_gs0;
            case 1: return pal_gs1;
            default: return pal_gs0;
        }
    }

    float D, F, k;
    Palette *pal_gs0;
    Palette *pal_gs1;
};

struct RdnGrids {
    RdnGrids(int _w, int _h) :
        w(_w), h(_h),
        gridY(w, h, 2),
        gridK(w, h, 2),
        gridL(w, h, 2)
    {
        reset_dt();
        reset_cur_instable();
    }

    void reset_dt() {
        cur_dt = 0.01;
    }

    // Allow dt to rapidly rise again
    void reset_cur_instable() {
        since_instable = 10000;
    }

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

        reset_dt();
        reset_cur_instable();
    }

    void compute_laplacian() {
        for(int y=0; y<h; y++) {
            for(int chan=0; chan<gridY.n; chan++) {
                float *Ybuf = gridY.getChannel(chan);
                float *Lbuf = gridL.getChannel(chan);
                int yl = y>  0 ? y-1 : h-1;
                int yr = y<h-1 ? y+1 :   0;
                for(int x=0; x<w; x++) {
                    int xl = x>  0 ? x-1 : w-1;
                    int xr = x<w-1 ? x+1 :   0;
                    // Klein bottle topology
                    int x2 = y==0 ? w-1-x : x;
                    int x3 = y==h-1 ? w-1-x : x;
                    Lbuf[y*w+x] =
                        Ybuf[y *w + x ] * (-4) +
                        Ybuf[yl*w + x2] +
                        Ybuf[yr*w + x3] +
                        Ybuf[y *w + xl] +
                        Ybuf[y *w + xr];
                }
            }
        }
    }

    void step(FunctionBase *fn) {
        const int n = gridY.n;
        float m[25]; // FIXME - n**2
        float diffusion_norm = fn->get_diffusion_matrix(m);
        float diffusion_stability = 1.0 / (diffusion_norm * 4.0);
        diffusion_stability *= 0.95;

        LOGI("dt=%g, ds=%g", cur_dt, diffusion_stability);

        float max_dt = 1;

        for(int iter=0; iter<5; iter++) {
            since_instable++;
            if(since_instable > 1000) {
                cur_dt *= 1.05;
            } else {
                cur_dt *= 1.0001;
            }
            if(cur_dt > max_dt) cur_dt = max_dt;

            // FIXME - try to keep things in cache

            float lap_to_go = cur_dt;
            while(lap_to_go > 0) {
                float lap_dt = lap_to_go;
                if(lap_dt > diffusion_stability) lap_dt = diffusion_stability;

                compute_laplacian();
                for(int chanY=0; chanY<n; chanY++)
                for(int chanL=0; chanL<n; chanL++) {
                    float *Ybuf = gridY.getChannel(chanY);
                    float *Lbuf = gridL.getChannel(chanL);
                    float v = m[chanY*n + chanL] * lap_dt;
                    for(int i=0; i<gridY.wh; i++) {
                        Ybuf[i] += Lbuf[i] * v;
                    }
                }

                lap_to_go -= lap_dt;
            }

            for(int y=0; y<h; y++) {
                fn->compute_dx_dt(gridY, gridK, gridL, w, y);
            }

            float limit_d = 0.5;

            for(int i=0; i<gridY.whn; i++) {
                float d = gridK.A[i] * cur_dt;
                float ad = fabsf(d);
                if(ad > limit_d) {
                    cur_dt *= limit_d / ad;
                    LOGI("ad=%g dt=%g", ad, cur_dt);
                    d = (d>0) ? limit_d : -limit_d;
                    since_instable = 0;
                }

                gridY.A[i] += d;
            }

            if(!isfinite(gridY.getChannel(0)[0])) {
                reset_grid(fn);
            }
        }
        //LOGI("pixel=%g,%g,%g,%g",
        //    gridY.getChannel(0)[0], gridY.getChannel(1)[0],
        //    gridK.getChannel(0)[0], gridK.getChannel(1)[0]);
    }

    void draw(void *pixels, int stride, FunctionBase *fn, Palette *pal) {
        for(int y = 0; y < h; y++) {
            uint32_t *pix_line = (uint32_t *)((char *)pixels + y * stride);
            pal->render_line(pix_line, gridY, gridK, gridL, y);
        }
    }

    int w, h;
    float cur_dt;
    int since_instable;
    Grid gridY;
    Grid gridK;
    Grid gridL;
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
    JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_setColorMatrix(
        JNIEnv *env, jobject obj, jfloatArray new_cm_arr);
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
    if(rdn) rdn->reset_cur_instable();
}

JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_setColorMatrix(
    JNIEnv *env, jobject obj, jfloatArray new_cm_arr
) {
    jfloat *params = env->GetFloatArrayElements(new_cm_arr, NULL);
    jsize len = env->GetArrayLength(new_cm_arr);
    if(len != 20) LOGE("wrong color matrix len: %d", len);
    for(int i=0; i<20; i++) {
        color_matrix[i] = params[i];
    }
    env->ReleaseFloatArrayElements(new_cm_arr, params, JNI_ABORT);
}

JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_resetGrid(
    JNIEnv *env, jobject obj
) {
    rdn->reset_grid(fn);
}
