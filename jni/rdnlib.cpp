#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <utility>

#include <android/log.h>
#include <android/bitmap.h>
#include <jni.h>

#include <Eigen/Core>
#include <Eigen/SVD>

#define LOG_TAG "rdn"
#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

#define CLIP_BYTE(v) (v < 0 ? 0 : v > 255 ? 255 : v)

float color_matrix[20];

template <typename T>
T max(const T x, const T y) {
    return x > y ? x : y;
}

#define vecn Eigen::Matrix<float, n, 1>
#define matnn Eigen::Matrix<float, n, n>

template <int n>
struct Grid {
    Grid(int _w, int _h) :
        w(_w), h(_h),
        wh(w*h),
        A(new vecn[wh])
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
    vecn *A;
};

template <int n>
class Palette {
public:
    virtual void render_line(uint32_t *pix_line, Grid<n> &G, Grid<n> &DG, Grid<n> &LG, int y) = 0;

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

template <int n>
struct FunctionBase {
    virtual ~FunctionBase() { }

    virtual matnn get_diffusion_matrix() = 0;

    virtual void compute_dx_dt(Grid<n> &Gi, Grid<n> &Go, Grid<n> &Gl, int w, int y) = 0;

    virtual vecn get_background_val() = 0;

    virtual vecn get_seed_val(int seed_idx) = 0;

    virtual void set_params(float *p, int len) = 0;

    virtual Palette<n> *get_palette(int id) = 0;
};

struct GinzburgLandau : public FunctionBase<2> {
    static const int n = 2;

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

    virtual vecn get_background_val() {
        vecn ret;
        ret << 1, 0;
        return ret;
    }

    virtual vecn get_seed_val(int seed_idx) {
        float U = ((seed_idx*5)%7)/7.0F*2.0F-1.0F;
        float V = ((seed_idx*9)%13)/13.0F*2.0F-1.0F;
        vecn ret;
        ret << U, V;
        return ret;
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

    virtual matnn get_diffusion_matrix() {
        matnn ret;
        ret <<
            D,       -D*alpha,
            D*alpha, D;
        return ret;
    }

    virtual void compute_dx_dt(Grid<n> &Gi, Grid<n> &Go, Grid<n> &Gl, int w, int y) {
        vecn *ibuf = Gi.A + y*w;
        vecn *obuf = Go.A + y*w;

        for(int x=0; x<w; x++) {
            float  U = ibuf[x][0];
            float  V = ibuf[x][1];
            float r2 = U*U + V*V;

            obuf[x][0] = U - (U - beta*V)*r2; // + D*(lU - alpha*lV);
            obuf[x][1] = V - (V + beta*U)*r2; // + D*(lV + alpha*lU);
            // From Ready (https://code.google.com/p/reaction-diffusion)
            //Uobuf[x] = DU*lU + alpha*U - gamma*V + (-beta*U + delta*V)*r2;
            //Vobuf[x] = DV*lV + alpha*V + gamma*U + (-beta*V - delta*U)*r2;
        }
    }

    struct PaletteGL0 : public Palette<n> {
        void render_line(uint32_t *pix_line, Grid<n> &G, Grid<n> &DG, Grid<n> &LG, int y) {
            if(y == 0) {
                avg_rv = accum_rv / cnt;
                avg_rk = accum_rk / cnt;
                accum_rv = 0;
                accum_rk = 0;
                cnt = 0;
            }
            vecn *gridX = G .A + y * G.w;
            vecn *gridD = DG.A + y * G.w;
            for(int x = 0; x < G.w; x++) {
                float A = gridX[x][0];
                float B = gridX[x][1];
                float DA = gridD[x][0];
                float DB = gridD[x][1];
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

    struct PaletteGL1 : public Palette<n> {
        PaletteGL1(GinzburgLandau &x) : parent(x) { }

        void render_line(uint32_t *pix_line, Grid<n> &G, Grid<n> &DG, Grid<n> &LG, int y) {
            if(y == 0) {
                avg_rv = accum_rv / cnt;
                accum_rv = 0;
                cnt = 0;
            }
            vecn *gridX = G .A + y * G.w;
            vecn *gridD = DG.A + y * G.w;
            vecn *gridL = LG.A + y * G.w;
            for(int x = 0; x < G.w; x++) {
                float A = gridX[x][0];
                float B = gridX[x][1];
                float DA = gridD[x][0];
                float DB = gridD[x][1];
                float LA = gridL[x][0];
                float LB = gridL[x][1];
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

    virtual Palette<n> *get_palette(int id) {
        switch(id) {
            case 0: return pal_gl0;
            case 1: return pal_gl1;
            default: return pal_gl0;
        }
    }

    float D, alpha, beta;
    Palette<n> *pal_gl0;
    Palette<n> *pal_gl1;
};

struct GrayScott : public FunctionBase<2> {
    static const int n = 2;

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

    virtual vecn get_background_val() {
        vecn ret;
        ret << 1, 0;
        return ret;
    }

    virtual vecn get_seed_val(int seed_idx) {
        //get_background_val(A, B);
        //switch(seed_idx % 2) {
        //    case 0:  A += 0.0F; B += 0.1F; break;
        //    default: A += 0.1F; B += 0.0F; break;
        //}
        float A = ((seed_idx*5)%7)/7.0F;
        float B = ((seed_idx*9)%13)/13.0F;
        vecn ret;
        ret << A, B;
        return ret;
    }

    virtual void set_params(float *p, int len) {
        if(len != 3) {
            LOGE("params is wrong length: %d", len);
        }
        D = p[0];
        F = p[1];
        k = p[2];
    }

    virtual matnn get_diffusion_matrix() {
        matnn ret;
        ret << 2*D, 0, 0, D;
        return ret;
    }

    virtual void compute_dx_dt(Grid<n> &Gi, Grid<n> &Go, Grid<n> &Gl, int w, int y) {
        vecn *ibuf = Gi.A + y*w;
        vecn *obuf = Go.A + y*w;

        for(int x=0; x<w; x++) {
            float ai = ibuf[x][0];
            float bi = ibuf[x][1];

            //Ao[x] = 1 - ai - mu*ai*bi*bi + D*da;
            //Bo[x] = mu*ai*bi*bi - phi*bi + D*db;
            obuf[x][0] = /* 2*D*da */ - ai*bi*bi + F*(1-ai);
            obuf[x][1] = /*   D*db + */ ai*bi*bi - (F+k)*bi;
        }
    }

    struct PaletteGS0 : public Palette<n> {
        void render_line(uint32_t *pix_line, Grid<n> &G, Grid<n> &DG, Grid<n> &LG, int y) {
            vecn *gridX = G .A + y * G.w;
            vecn *gridD = DG.A + y * G.w;
            //vecn *gridL = LG.A + y * G.w;
            for(int x = 0; x < G.w; x++) {
                float A = gridX[x][0];
                float B = gridX[x][1];
                float DA = gridD[x][0];
                //float DB = gridD[x][1];
                //float LA = gridL[x][0];
                //float LB = gridL[x][1];

                int red   = (1-A) * 200;
                int green = DA * 20000;
                int blue  = B * 1000;

                pix_line[x] = to_rgb24(red, green, blue);
            }
        }
    };

    struct PaletteGS1 : public Palette<n> {
        PaletteGS1(GrayScott &x) : parent(x) { }

        void render_line(uint32_t *pix_line, Grid<n> &G, Grid<n> &DG, Grid<n> &LG, int y) {
            if(y == 0) {
                avg_rv = accum_rv / cnt;
                accum_rv = 0;
                cnt = 0;
            }
            //vecn *gridX = G .A + y * G.w;
            //vecn *gridD = DG.A + y * G.w;
            vecn *gridL = LG.A + y * G.w;
            for(int x = 0; x < G.w; x++) {
                //float A = gridX[x][0];
                //float B = gridX[x][1];
                //float DA = gridD[x][0];
                //float DB = gridD[x][1];
                float LA = gridL[x][0];
                float LB = gridL[x][1];

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

    virtual Palette<n> *get_palette(int id) {
        switch(id) {
            case 0: return pal_gs0;
            case 1: return pal_gs1;
            default: return pal_gs0;
        }
    }

    float D, F, k;
    Palette<n> *pal_gs0;
    Palette<n> *pal_gs1;
};

template <int n>
struct RdnGrids {
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

    void reset_grid(FunctionBase<n> *fn) {
        vecn bgval = fn->get_background_val();
        for(int i = 0; i < gridY.wh; i++) {
            gridY.A[i] = bgval;
        }

        for(int seed_idx = 0; seed_idx < 20; seed_idx++) {
            int sr = 20;
            vecn seedval = fn->get_seed_val(seed_idx);
            int x0 = rand() % (w - sr);
            int y0 = rand() % (h - sr);
            for(int y = y0; y < y0+sr; y++) {
                vecn *buf = gridY.A + y * w;
                for(int x = x0; x < x0+sr; x++) {
                    buf[x] = seedval;
                }
            }
        }

        reset_dt();
        reset_cur_instable();
    }

    void compute_laplacian() {
        vecn *Ybuf = gridY.A;
        vecn *Lbuf = gridL.A;
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

    void step(FunctionBase<n> *fn) {
        matnn m = fn->get_diffusion_matrix();
        Eigen::JacobiSVD<matnn, Eigen::NoQRPreconditioner> svd(m);
        float diffusion_norm = svd.singularValues().array().abs().maxCoeff();
        float diffusion_stability = 1.0 / (diffusion_norm * 4.0);
        diffusion_stability *= 0.95;

        //LOGI("dt=%g, dn=%g, ds=%g", cur_dt, diffusion_norm, diffusion_stability);

        float max_dt = 2; // FIXME

        for(int iter=0; iter<5; iter++) {
            since_instable++;
            //if(since_instable > 1000) {
            //    cur_dt *= 1.05;
            //} else {
            //    cur_dt *= 1.0001;
            //}
            cur_dt *= 1.01;
            if(cur_dt > max_dt) cur_dt = max_dt;

            float lap_to_go = cur_dt;
            while(lap_to_go > 0) {
                float lap_dt = lap_to_go;
                if(lap_dt > diffusion_stability) lap_dt = diffusion_stability;
                matnn m2 = m * lap_dt;

                compute_laplacian();
                vecn *Ybuf = gridY.A;
                vecn *Lbuf = gridL.A;
                for(int i=0; i<gridY.wh; i++) {
                    Ybuf[i] += m2 * Lbuf[i];
                }

                lap_to_go -= lap_dt;
            }

            float limit_d = 0.5;

            for(int y=0; y<h; y++) {
                vecn *bufY = gridY.A + w*y;
                vecn *bufK = gridK.A + w*y;

                fn->compute_dx_dt(gridY, gridK, gridL, w, y);

                for(int x=0; x<w; x++) {
                    vecn d = bufK[x] * cur_dt;
                    float ad = d.cwiseAbs().maxCoeff();
                    if(ad > limit_d) {
                        cur_dt *= limit_d / ad;
                        LOGI("ad=%g dt=%g", ad, cur_dt);
                        for(int j=0; j<n; j++) {
                            d[j] = (d[j]>0) ? limit_d : -limit_d;
                        }
                        since_instable = 0;
                    }
                    bufY[x] += d;
                }
            }

            if(!std::isfinite(gridY.A[0][0])) {
                reset_grid(fn);
            }
        }
        //LOGI("pixel=%g,%g,%g,%g",
        //    gridY.getChannel(0)[0], gridY.getChannel(1)[0],
        //    gridK.getChannel(0)[0], gridK.getChannel(1)[0]);
    }

    void draw(void *pixels, int stride, FunctionBase<n> *fn, Palette<n> *pal) {
        for(int y = 0; y < h; y++) {
            uint32_t *pix_line = (uint32_t *)((char *)pixels + y * stride);
            pal->render_line(pix_line, gridY, gridK, gridL, y);
        }
    }

    int w, h;
    float cur_dt;
    int since_instable;
    Grid<n> gridY;
    Grid<n> gridK;
    Grid<n> gridL;
};

RdnGrids<2> *rdn = NULL;
FunctionBase<2> *fn_gl = new GinzburgLandau();
FunctionBase<2> *fn_gs = new GrayScott();
FunctionBase<2> *fn_list[] = { fn_gl, fn_gs };
FunctionBase<2> *fn = fn_gl;
Palette<2> *pal = fn->get_palette(0);

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
        rdn = new RdnGrids<2>(info.width, info.height);
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
