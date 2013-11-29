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
//#include <Eigen/SVD>

//#include "prof.h"

#define LOG_TAG "rdn"
#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

#define CLIP_BYTE(v) (v < 0 ? 0 : v > 255 ? 255 : v)

float color_matrix[20];

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

    const int w, h, wh;
    vecn *A;
};

template <int n>
class Palette {
public:
    virtual void render_line(uint32_t *pix_line, Grid<n> &G, Grid<n> &LG, int y, int dir) = 0;

    static inline int to_rgb24(float r, float g, float b) {
        //if(r < 0) r = 0;
        //if(g < 0) g = 0;
        //if(b < 0) b = 0;
        float *cm = color_matrix;
        float rp = r*cm[0] + g*cm[1] + b*cm[2] + cm[4]; cm += 5;
        float gp = r*cm[0] + g*cm[1] + b*cm[2] + cm[4]; cm += 5;
        float bp = r*cm[0] + g*cm[1] + b*cm[2] + cm[4]; cm += 5;
        int ri = int(rp);
        int gi = int(gp);
        int bi = int(bp);
        //accum_r += ri & 7; ri &= 0xf8; if(accum_r > 8) { ri += 8; accum_r -= 8; }
        //accum_g += gi & 7; gi &= 0xf8; if(accum_g > 8) { gi += 8; accum_g -= 8; }
        //accum_b += bi & 7; bi &= 0xf8; if(accum_b > 8) { bi += 8; accum_b -= 8; }
        return 0xff000000 | (CLIP_BYTE(bi)<<16) | (CLIP_BYTE(gi)<<8) | CLIP_BYTE(ri);
    }
};

template <int n>
struct FunctionBase {
    virtual ~FunctionBase() { }

    virtual matnn get_diffusion_matrix() = 0;
    virtual float get_diffusion_norm() = 0;
    virtual float get_dt() = 0;
    virtual void compute_dx_dt(vecn *buf, int w, float dt) = 0;
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
        pal_gl0(new PaletteGL0(*this)),
        pal_gl1(new PaletteGL1(*this)),
        pal_gl2(new PaletteGL2(*this))
    { }

    ~GinzburgLandau() {
        delete(pal_gl0);
        delete(pal_gl1);
        delete(pal_gl2);
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
        ret << D, -D*alpha, D*alpha, D;
        return ret;
    }

    virtual float get_diffusion_norm() {
        // This seems to be appropriate, but I don't know exactly why.
        return D * (1 + fabsf(alpha*alpha));
    }

    virtual float get_dt() {
        return 0.1 / std::max(2.0f, fabsf(beta));
    }

    virtual void compute_dx_dt(vecn *buf, int w, float dt) {
        for(int x=0; x<w; x++) {
            float  U = buf[x][0];
            float  V = buf[x][1];
            float r2 = U*U + V*V;

            buf[x][0] += dt * (U - (U - beta*V)*r2);
            buf[x][1] += dt * (V - (V + beta*U)*r2);
        }
    }

    struct PaletteGL0 : public Palette<n> {
        PaletteGL0(GinzburgLandau &x) : parent(x) { }

        void render_line(uint32_t *pix_line, Grid<n> &G, Grid<n> &LG, int y, int dir) {
            vecn *gridX = G .A + y * G.w;
            vecn *gridL = LG.A + y * G.w;
            float last_rv = gridX[G.w-1].dot(gridX[G.w-1]);
            float rv = gridX[0].dot(gridX[0]);
            for(int x = 0; x < G.w; x++) {
                int next_x = (x < G.w-1) ? x+1 : 0;
                float next_rv = gridX[next_x].dot(gridX[next_x]);
                float U = gridX[x][0];
                float V = gridX[x][1];
                float lU = gridL[x][0];
                float lV = gridL[x][1];
                //float lv = parent.D * gridL[x].dot(gridL[x]);
                float deriv = next_rv - last_rv;
                if(dir) deriv = -deriv;

                float rotA = U*lV - V*lU;
                float rotB = U*lU + V*lV;

                float green =  70.0f - rotA * 1000.0;
                float blue  =  70.0f - rotB * 1000.0;
                float red   = 0; //-70.0f + rv * 200.0f;

                float diffuse = 0.7 + 0.7 * deriv / sqrtf(1.0f + deriv*deriv);
                red   *= diffuse;
                green *= diffuse;
                blue  *= diffuse;

                pix_line[x] = to_rgb24(red, green, blue);
                last_rv = rv;
                rv = next_rv;
            }
        }

        GinzburgLandau &parent;
    };

    struct PaletteGL1 : public Palette<n> {
        PaletteGL1(GinzburgLandau &x) : parent(x) { }

        void render_line(uint32_t *pix_line, Grid<n> &G, Grid<n> &LG, int y, int dir) {
            vecn *gridX = G .A + y * G.w;
            vecn *gridL = LG.A + y * G.w;
            for(int x = 0; x < G.w; x++) {
                float rv = gridX[x].dot(gridX[x]);
                float lv = parent.D * gridL[x].dot(gridL[x]);
                float green = 0;
                float blue  = lv * 500;
                float red   = rv * 100 + blue;

                pix_line[x] = to_rgb24(red, green, blue);
            }
        }

        GinzburgLandau &parent;
    };

    struct PaletteGL2 : public Palette<n> {
        PaletteGL2(GinzburgLandau &x) : parent(x) { }

        void render_line(uint32_t *pix_line, Grid<n> &G, Grid<n> &LG, int y, int dir) {
            vecn *gridX = G .A + y * G.w;
            //vecn *gridL = LG.A + y * G.w;
            float last_rv = gridX[G.w-1].dot(gridX[G.w-1]);
            float rv = gridX[0].dot(gridX[0]);
            for(int x = 0; x < G.w; x++) {
                int next_x = (x < G.w-1) ? x+1 : 0;
                float next_rv = gridX[next_x].dot(gridX[next_x]);
                //float lv = parent.D * gridL[x].dot(gridL[x]);
                float deriv = next_rv - last_rv;
                if(dir) deriv = -deriv;
                float green = 128.0f + 200.0f * deriv;
                float blue  = 0;
                float red   = 0;

                pix_line[x] = to_rgb24(red, green, blue);
                last_rv = rv;
                rv = next_rv;
            }
        }

        GinzburgLandau &parent;
    };

    virtual Palette<n> *get_palette(int id) {
        switch(id) {
            case 0: return pal_gl0;
            case 1: return pal_gl1;
            case 2: return pal_gl2;
            default: return pal_gl0;
        }
    }

    float D, alpha, beta;
    Palette<n> *pal_gl0;
    Palette<n> *pal_gl1;
    Palette<n> *pal_gl2;
};

struct GrayScott : public FunctionBase<2> {
    static const int n = 2;

    GrayScott() :
        D(2.0F),
        F(0.01F),
        k(0.049F),
        pal_gs0(new PaletteGS0(*this)),
        pal_gs1(new PaletteGS1(*this)),
        pal_gs2(new PaletteGS2(*this))
    { }

    ~GrayScott() {
        delete(pal_gs0);
        delete(pal_gs1);
        delete(pal_gs2);
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

    virtual float get_diffusion_norm() {
        return 2*D;
    }

    virtual float get_dt() {
        return 1.5;
    }

    virtual void compute_dx_dt(vecn *buf, int w, float dt) {
        for(int x=0; x<w; x++) {
            float a = buf[x][0];
            float b = buf[x][1];

            buf[x][0] += dt * (-a*b*b + F*(1-a));
            buf[x][1] += dt * ( a*b*b - (F+k)*b);
        }
    }

    struct PaletteGS0 : public Palette<n> {
        PaletteGS0(GrayScott &x) : parent(x) { }

        void render_line(uint32_t *pix_line, Grid<n> &G, Grid<n> &LG, int y, int dir) {
            vecn *gridX = G .A + y * G.w;
            vecn *gridL = LG.A + y * G.w;
            for(int x = 0; x < G.w; x++) {
                float A = gridX[x][0];
                float B = gridX[x][1];
                float LA = parent.D * gridL[x][0];
                //float LB = parent.D * gridL[x][1];

                float red   = (1-A) * 200;
                float green = LA * 40000;
                float blue  = B * 500;

                pix_line[x] = to_rgb24(red, green, blue);
            }
        }

        GrayScott &parent;
    };

    struct PaletteGS1 : public Palette<n> {
        PaletteGS1(GrayScott &x) : parent(x) { }

        void render_line(uint32_t *pix_line, Grid<n> &G, Grid<n> &LG, int y, int dir) {
            //vecn *gridX = G .A + y * G.w;
            //vecn *gridD = DG.A + y * G.w;
            vecn *gridL = LG.A + y * G.w;
            for(int x = 0; x < G.w; x++) {
                //float A = gridX[x][0];
                //float B = gridX[x][1];
                float LA = parent.D * gridL[x][0];
                float LB = parent.D * gridL[x][1];

                float red   = LB * 60000;
                float green = 0;
                float blue  = LA * 60000;

                pix_line[x] = to_rgb24(red, green, blue);
            }
        }

        GrayScott &parent;
    };

    struct PaletteGS2 : public Palette<n> {
        PaletteGS2(GrayScott &x) : parent(x) { }

        void render_line(uint32_t *pix_line, Grid<n> &G, Grid<n> &LG, int y, int dir) {
            vecn *gridX = G .A + y * G.w;
            //vecn *gridL = LG.A + y * G.w;
            float last_rv = gridX[G.w-1].dot(gridX[G.w-1]);
            float rv = gridX[0].dot(gridX[0]);
            for(int x = 0; x < G.w; x++) {
                int next_x = (x < G.w-1) ? x+1 : 0;
                float next_rv = gridX[next_x].dot(gridX[next_x]);
                //float lv = parent.D * gridL[x].dot(gridL[x]);
                float deriv = next_rv - last_rv;
                if(dir) deriv = -deriv;
                float green = 128.0f + 200.0f * deriv;
                float blue  = 0;
                float red   = 0;

                pix_line[x] = to_rgb24(red, green, blue);
                last_rv = rv;
                rv = next_rv;
            }
        }

        GrayScott &parent;
    };

    virtual Palette<n> *get_palette(int id) {
        switch(id) {
            case 0: return pal_gs0;
            case 1: return pal_gs1;
            case 2: return pal_gs2;
            default: return pal_gs0;
        }
    }

    float D, F, k;
    Palette<n> *pal_gs0;
    Palette<n> *pal_gs1;
    Palette<n> *pal_gs2;
};

struct WackerScholl : public FunctionBase<2> {
    static const int n = 2;

    WackerScholl() :
        D(2.0F),
        alpha(0.02f),
        tau(0.05f),
        j0(1.21f),
        d(8.0f),
        pal_gs0(new PaletteWS0()),
        pal_gs1(new PaletteWS1(*this)),
        pal_gs2(new PaletteWS2(*this))
    { }

    ~WackerScholl() {
        delete(pal_gs0);
        delete(pal_gs1);
        delete(pal_gs2);
    }

    virtual vecn get_background_val() {
        vecn ret;
        ret[0] = j0/((j0*j0+1)*tau);
        ret[1] = j0/((j0*j0+1)*tau) + j0;
        return ret;
    }

    virtual vecn get_seed_val(int seed_idx) {
        //get_background_val(A, B);
        //switch(seed_idx % 2) {
        //    case 0:  A += 0.0F; B += 0.1F; break;
        //    default: A += 0.1F; B += 0.0F; break;
        //}
        vecn ret = get_background_val();
        ret[0] += ((seed_idx*5)%7)/7.0F;
        ret[1] += ((seed_idx*9)%13)/13.0F;
        return ret;
    }

    virtual void set_params(float *p, int len) {
        if(len != 5) {
            LOGE("params is wrong length: %d", len);
        }
        D = p[0];
        alpha = p[1];
        tau = p[2];
        j0 = p[3];
        d = p[4];
    }

    virtual matnn get_diffusion_matrix() {
        matnn ret;
        ret << D, 0, 0, D*d;
        return ret;
    }

    virtual float get_diffusion_norm() {
        return std::max(D, D*d);
    }

    virtual float get_dt() {
        return 1.0;
    }

    virtual void compute_dx_dt(vecn *buf, int w, float dt) {
        for(int x=0; x<w; x++) {
            float a = buf[x][0];
            float b = buf[x][1];

            buf[x][0] += dt * ((b-a)/((b-a)*(b-a)+1) - tau*a);
            buf[x][1] += dt * (alpha*(j0-(b-a)));
        }
    }

    struct PaletteWS0 : public Palette<n> {
        void render_line(uint32_t *pix_line, Grid<n> &G, Grid<n> &LG, int y, int dir) {
            vecn *gridX = G .A + y * G.w;
            vecn *gridL = LG.A + y * G.w;
            for(int x = 0; x < G.w; x++) {
                float A = gridX[x][0];
                float B = gridX[x][1];
                float LA = gridL[x][0];

                float red   = (1-A) * 200;
                float green = LA * 20000;
                float blue  = B * 1000;

                pix_line[x] = to_rgb24(red, green, blue);
            }
        }
    };

    struct PaletteWS1 : public Palette<n> {
        PaletteWS1(WackerScholl &x) : parent(x) { }

        void render_line(uint32_t *pix_line, Grid<n> &G, Grid<n> &LG, int y, int dir) {
            //vecn *gridX = G .A + y * G.w;
            //vecn *gridD = DG.A + y * G.w;
            vecn *gridL = LG.A + y * G.w;
            for(int x = 0; x < G.w; x++) {
                //float A = gridX[x][0];
                //float B = gridX[x][1];
                float LA = gridL[x][0];
                float LB = gridL[x][1];

                float rv = parent.D * LB;
                float gv = parent.D * LA;
                //float w = sqrtf(LA*LA + LB*LB);
                float red   = rv * 60000;
                float green = 0; //parent.D * LB * 20000;
                float blue  = gv * 60000;

                pix_line[x] = to_rgb24(red, green, blue);
            }
        }

        WackerScholl &parent;
    };

    struct PaletteWS2 : public Palette<n> {
        PaletteWS2(WackerScholl &x) : parent(x) { }

        void render_line(uint32_t *pix_line, Grid<n> &G, Grid<n> &LG, int y, int dir) {
            vecn *gridX = G .A + y * G.w;
            vecn *gridL = LG.A + y * G.w;
            float last_rv = gridX[G.w-1].dot(gridX[G.w-1]);
            float rv = gridX[0].dot(gridX[0]);
            for(int x = 0; x < G.w; x++) {
                int next_x = (x < G.w-1) ? x+1 : 0;
                float next_rv = gridX[next_x].dot(gridX[next_x]);
                //float lv = parent.D * gridL[x].dot(gridL[x]);
                float deriv = next_rv - last_rv;
                if(dir) deriv = -deriv;
                float green = 128.0f + 200.0f * deriv;
                float blue  = 0;
                float red   = 0;

                pix_line[x] = to_rgb24(red, green, blue);
                last_rv = rv;
                rv = next_rv;
            }
        }

        WackerScholl &parent;
    };

    virtual Palette<n> *get_palette(int id) {
        switch(id) {
            case 0: return pal_gs0;
            case 1: return pal_gs1;
            case 2: return pal_gs2;
            default: return pal_gs0;
        }
    }

    float D, alpha, tau, j0, d;
    Palette<n> *pal_gs0;
    Palette<n> *pal_gs1;
    Palette<n> *pal_gs2;
};

template <int n>
struct RdnGrids {
    RdnGrids(int _w, int _h) :
        w(_w), h(_h),
        gridY(w, h),
        gridL(w, h)
    { }

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
    }

    void compute_laplacian() {
        vecn *Ybuf = gridY.A;
        vecn *Lbuf = gridL.A;
        for(int y=0; y<h; y++) {
            int yl = y>  0 ? y-1 : h-1;
            int yr = y<h-1 ? y+1 :   0;
            vecn *L = Lbuf + y*w;
            vecn *Y = Ybuf + y*w;
            vecn *Yup = Ybuf + yl*w;
            vecn *Ydn = Ybuf + yr*w;
            for(int x=0; x<w; x++) {
                int xl = x>  0 ? x-1 : w-1;
                int xr = x<w-1 ? x+1 :   0;
                // Klein bottle topology
                int x2 = y==0 ? w-1-x : x;
                int x3 = y==h-1 ? w-1-x : x;
                L[x] = -4.0f * Y[x] + Yup[x2] + Ydn[x3] + Y[xl] + Y[xr];
            }
        }
    }

    void step(FunctionBase<n> *fn) {
        matnn m = fn->get_diffusion_matrix();
        //Eigen::JacobiSVD<matnn, Eigen::NoQRPreconditioner> svd(m);
        float diffusion_norm = fn->get_diffusion_norm();
        float diffusion_stability = 1.0 / (diffusion_norm * 4.0);
        diffusion_stability *= 0.95;

        float dt = fn->get_dt();

        //LOGI("dt=%g, dn=%g, ds=%g", dt, diffusion_norm, diffusion_stability);

        for(int iter=0; iter<5; iter++) {
            float lap_to_go = dt;
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

            for(int y=0; y<h; y++) {
                vecn *bufY = gridY.A + w*y;
                fn->compute_dx_dt(bufY, w, dt);
            }

            if(!std::isfinite(gridY.A[0][0])) {
                reset_grid(fn);
            }
        }
    }

    void draw(void *pixels, int stride, FunctionBase<n> *fn, Palette<n> *pal, int dir) {
        for(int y = 0; y < h; y++) {
            uint32_t *pix_line = (uint32_t *)((char *)pixels + y * stride);
            pal->render_line(pix_line, gridY, gridL, y, dir);
        }
    }

    int w, h;
    Grid<n> gridY;
    Grid<n> gridL;
};

RdnGrids<2> *rdn = NULL;
FunctionBase<2> *fn_gl = new GinzburgLandau();
FunctionBase<2> *fn_gs = new GrayScott();
FunctionBase<2> *fn_ws = new WackerScholl();
FunctionBase<2> *fn_list[] = { fn_gl, fn_gs, fn_ws };
FunctionBase<2> *fn = fn_gl;
Palette<2> *pal = fn->get_palette(0);

//int profile_ticks = -1;

extern "C" {
    JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_evolve(
        JNIEnv *env, jobject obj);
    JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_renderFrame(
        JNIEnv *env, jobject obj, jobject bitmap, jint dir);
    JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_setParams(
        JNIEnv *env, jobject obj, jint fn_idx, jfloatArray params, jint pal);
    JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_setColorMatrix(
        JNIEnv *env, jobject obj, jfloatArray new_cm_arr);
    JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_resetGrid(
        JNIEnv *env, jobject obj);
};

JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_renderFrame(
    JNIEnv *env, jobject obj, jobject bitmap, jint dir
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

    rdn->draw(pixels, info.stride, fn, pal, dir);
}

JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnWallpaper_evolve(
    JNIEnv *env, jobject obj
) {
    if(rdn) rdn->step(fn);

//    if(profile_ticks == 50) {
//        monstartup("librdnlib.so");
//    } else if(profile_ticks == 200) {
//        LOGI("writing profile");
//        setenv("CPUPROFILE", "/data/data/org.stahlke.rdnwallpaper/files/gmon.out", 1);
//        moncleanup();
//    } else {
//        if(profile_ticks <= 200) LOGI("ticks=%d", profile_ticks);
//    }
//    profile_ticks++;
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
