#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <utility>

#include <android/log.h>
#include <android/bitmap.h>
#include <jni.h>

//#include <Eigen/Array>

#define LOG_TAG "rdn"
#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

#define CLIP_BYTE(v) (v < 0 ? 0 : v > 255 ? 255 : v)

float color_matrix[20];

template <typename T>
T max(const T x, const T y) {
    return x > y ? x : y;
}

template <int n>
struct vector {
    vector() {
        v[0] = 0;
        v[1] = 0;
    }

    vector(float a, float b) {
        v[0] = a;
        v[1] = b;
    }

    vector<n> &operator+=(const vector<n> &rhs) {
        for(int i=0; i<n; i++) v[i] += rhs.v[i];
        return *this;
    }

    vector<n> operator+(const vector<n> &rhs) const {
        vector<n> ret = *this;
        ret += rhs;
        return ret;
    }

    vector<n> &operator*=(const float x) {
        for(int i=0; i<n; i++) v[i] *= x;
        return *this;
    }

    vector<n> operator*(const float x) const {
        vector<n> ret = *this;
        ret *= x;
        return ret;
    }

    float v[n];
};

struct Grid {
    Grid(int _w, int _h) :
        w(_w), h(_h),
        wh(w*h),
        A(new vector<2>[wh])
    { }

    ~Grid() {
        delete(A);
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

    const int w, h, wh;
    vector<2> *A;
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

    virtual float get_diffusion_matrix(float m[]) = 0;

    virtual void compute_dx_dt(Grid &Gi, Grid &Go, Grid &Gl, int w, int y) = 0;

    virtual vector<2> get_background_val() = 0;

    virtual vector<2> get_seed_val(int seed_idx) = 0;

    virtual void set_params(float *p, int len) = 0;

    virtual Palette *get_palette(int id) = 0;
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

    virtual vector<2> get_background_val() {
        return vector<2>(1, 0);
    }

    virtual vector<2> get_seed_val(int seed_idx) {
        float U = ((seed_idx*5)%7)/7.0F*2.0F-1.0F;
        float V = ((seed_idx*9)%13)/13.0F*2.0F-1.0F;
        return vector<2>(U, V);
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
        vector<2> *ibuf = Gi.A + y*w;
        vector<2> *obuf = Go.A + y*w;

        for(int x=0; x<w; x++) {
            float  U = ibuf[x].v[0];
            float  V = ibuf[x].v[1];
            float r2 = U*U + V*V;

            obuf[x].v[0] = U - (U - beta*V)*r2; // + D*(lU - alpha*lV);
            obuf[x].v[1] = V - (V + beta*U)*r2; // + D*(lV + alpha*lU);
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
            vector<2> *gridX = G .A + y * G.w;
            vector<2> *gridD = DG.A + y * G.w;
            for(int x = 0; x < G.w; x++) {
                float A = gridX[x].v[0];
                float B = gridX[x].v[1];
                float DA = gridD[x].v[0];
                float DB = gridD[x].v[1];
                float rv = A*A + B*B;
                float rk = DA*DA + DB*DB;
                accum_rv += rv;
                accum_rk += rk;
                rv /= avg_rv;
                rk /= avg_rk;
                int red   = (rv-rk/4) * 150;
                int green = (rv-rk/2) * 100;
                int blue  = green + A * 30;

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
            vector<2> *gridX = G .A + y * G.w;
            vector<2> *gridD = DG.A + y * G.w;
            vector<2> *gridL = LG.A + y * G.w;
            for(int x = 0; x < G.w; x++) {
                float A = gridX[x].v[0];
                float B = gridX[x].v[1];
                float DA = gridD[x].v[0];
                float DB = gridD[x].v[1];
                float LA = gridL[x].v[0];
                float LB = gridL[x].v[1];
                float rv = A*A + B*B;
                float lv = DA*LA + DB*LB;
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

    virtual vector<2> get_background_val() {
        return vector<2>(1, 0);
    }

    virtual vector<2> get_seed_val(int seed_idx) {
        //get_background_val(A, B);
        //switch(seed_idx % 2) {
        //    case 0:  A += 0.0F; B += 0.1F; break;
        //    default: A += 0.1F; B += 0.0F; break;
        //}
        float A = ((seed_idx*5)%7)/7.0F;
        float B = ((seed_idx*9)%13)/13.0F;
        return vector<2>(A, B);
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
        vector<2> *ibuf = Gi.A + y*w;
        vector<2> *obuf = Go.A + y*w;

        for(int x=0; x<w; x++) {
            float ai = ibuf[x].v[0];
            float bi = ibuf[x].v[1];

            //Ao[x] = 1 - ai - mu*ai*bi*bi + D*da;
            //Bo[x] = mu*ai*bi*bi - phi*bi + D*db;
            obuf[x].v[0] = /* 2*D*da */ - ai*bi*bi + F*(1-ai);
            obuf[x].v[1] = /*   D*db + */ ai*bi*bi - (F+k)*bi;
        }
    }

    struct PaletteGS0 : public Palette {
        void render_line(uint32_t *pix_line, Grid &G, Grid &DG, Grid &LG, int y) {
            vector<2> *gridX = G .A + y * G.w;
            vector<2> *gridD = DG.A + y * G.w;
            //vector<2> *gridL = LG.A + y * G.w;
            for(int x = 0; x < G.w; x++) {
                float A = gridX[x].v[0];
                float B = gridX[x].v[1];
                float DA = gridD[x].v[0];
                //float DB = gridD[x].v[1];
                //float LA = gridL[x].v[0];
                //float LB = gridL[x].v[1];

                int red   = (1-A) * 200;
                int green = DA * 20000;
                int blue  = B * 1000;

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
            //vector<2> *gridX = G .A + y * G.w;
            //vector<2> *gridD = DG.A + y * G.w;
            vector<2> *gridL = LG.A + y * G.w;
            for(int x = 0; x < G.w; x++) {
                //float A = gridX[x].v[0];
                //float B = gridX[x].v[1];
                //float DA = gridD[x].v[0];
                //float DB = gridD[x].v[1];
                float LA = gridL[x].v[0];
                float LB = gridL[x].v[1];

                float rv = parent.D * LB;
                float gv = parent.D * LA;
                if(rv > 0) accum_rv += rv;
                //float w = sqrtf(LA*LA + LB*LB);
                int red   = rv * 60000;
                int green = 0; //parent.D * LB * 20000;
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
    static const int n = 2; // FIXME
    RdnGrids(int _w, int _h) :
        w(_w), h(_h),
        gridY(w, h),
        gridK(w, h),
        gridL(w, h)
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
        vector<n> bgval = fn->get_background_val();
        for(int i = 0; i < gridY.wh; i++) {
            gridY.A[i] = bgval;
        }

        for(int seed_idx = 0; seed_idx < 20; seed_idx++) {
            int sr = 20;
            vector<n> seedval = fn->get_seed_val(seed_idx);
            int x0 = rand() % (w - sr);
            int y0 = rand() % (h - sr);
            for(int y = y0; y < y0+sr; y++) {
                vector<n> *buf = gridY.A + y * w;
                for(int x = x0; x < x0+sr; x++) {
                    buf[x] = seedval;
                }
            }
        }

        reset_dt();
        reset_cur_instable();
    }

    void compute_laplacian() {
        vector<n> *Ybuf = gridY.A;
        vector<n> *Lbuf = gridL.A;
        for(int y=0; y<h; y++) {
            int yl = y>  0 ? y-1 : h-1;
            int yr = y<h-1 ? y+1 :   0;
            for(int x=0; x<w; x++) {
                int xl = x>  0 ? x-1 : w-1;
                int xr = x<w-1 ? x+1 :   0;
                // Klein bottle topology
                int x2 = y==0 ? w-1-x : x;
                int x3 = y==h-1 ? w-1-x : x;
                Lbuf[y*w+x] =
                    Ybuf[y *w + x ] * (-4.0f) +
                    Ybuf[yl*w + x2] +
                    Ybuf[yr*w + x3] +
                    Ybuf[y *w + xl] +
                    Ybuf[y *w + xr];
            }
        }
    }

    void step(FunctionBase *fn) {
        float m[n*n];
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
                vector<n> *Ybuf = gridY.A;
                vector<n> *Lbuf = gridL.A;
                for(int i=0; i<gridY.wh; i++) {
                    for(int j=0; j<n; j++)
                    for(int k=0; k<n; k++) {
                    float v = m[j*n + k] * lap_dt;
                        Ybuf[i].v[j] += Lbuf[i].v[k] * v;
                    }
                }

                lap_to_go -= lap_dt;
            }

            for(int y=0; y<h; y++) {
                fn->compute_dx_dt(gridY, gridK, gridL, w, y);
            }

            float limit_d = 0.5;

            for(int i=0; i<gridY.wh; i++) {
                vector<n> d = gridK.A[i] * cur_dt;
                float ad = max(fabsf(d.v[0]), fabsf(d.v[1])); // FIXME
                if(ad > limit_d) {
                    cur_dt *= limit_d / ad;
                    LOGI("ad=%g dt=%g", ad, cur_dt);
                    for(int j=0; j<n; j++) {
                        d.v[j] = (d.v[j]>0) ? limit_d : -limit_d;
                    }
                    since_instable = 0;
                }

                gridY.A[i] += d;
            }

            if(!isfinite(gridY.A[0].v[0])) {
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

    if (!rdn || rdn->w != (int)info.width || rdn->h != (int)info.height) {
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
