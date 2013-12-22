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

inline void apply_diffuse(float dp, float mag, float &r, float &g, float &b) {
    if(dp < 0.94) return;
    dp = dp*dp;
    dp = dp*dp;
    dp = dp*dp;
    dp = dp*dp;
    dp = dp*dp;
    dp = dp*dp;
    dp *= mag;
    r += dp;
    g += dp;
    b += dp;
}

template <int n>
struct Grid {
    Grid(int _w, int _h) :
        w(_w), h(_h),
        wh(w*h),
        arr(new vecn[wh])
    { }

    ~Grid() {
        delete(arr);
    }

    const int w, h, wh;
    vecn *arr;
};

struct GridsBase {
    GridsBase(int _w, int _h) : w(_w), h(_h), wh(_w*_h) { }

    virtual int get_n() = 0;

    const int w, h, wh;
};

template <int n>
struct GridsN : public GridsBase {
    GridsN(int _w, int _h) :
        GridsBase(_w, _h),
        gridA(w, h),
        gridL(w, h),
        gridDX(w, h),
        gridDY(w, h)
    { }

    int get_n() { return n; }

    void compute_laplacian() {
        vecn *Abuf = gridA.arr;
        vecn *Lbuf = gridL.arr;
        for(int y=0; y<h; y++) {
            int yl = y>  0 ? y-1 : h-1;
            int yr = y<h-1 ? y+1 :   0;
            vecn *L = Lbuf + y*w;
            vecn *A = Abuf + y*w;
            vecn *Aup = Abuf + yl*w;
            vecn *Adn = Abuf + yr*w;
            for(int x=0; x<w; x++) {
                int xl = x>  0 ? x-1 : w-1;
                int xr = x<w-1 ? x+1 :   0;
                // Klein bottle topology
                int x2 = y==0 ? w-1-x : x;
                int x3 = y==h-1 ? w-1-x : x;
                L[x] = -4.0f * A[x] + Aup[x2] + Adn[x3] + A[xl] + A[xr];
            }
        }
    }

    void compute_gradient() {
        vecn *Abuf = gridA.arr;
        vecn *DXbuf = gridDX.arr;
        vecn *DYbuf = gridDY.arr;
        for(int y=0; y<h; y++) {
            int yl = y>  0 ? y-1 : h-1;
            int yr = y<h-1 ? y+1 :   0;
            vecn *A = Abuf + y*w;
            vecn *DX = DXbuf + y*w;
            vecn *DY = DYbuf + y*w;
            vecn *Aup = Abuf + yl*w;
            vecn *Adn = Abuf + yr*w;
            for(int x=0; x<w; x++) {
                int xl = x>  0 ? x-1 : w-1;
                int xr = x<w-1 ? x+1 :   0;
                // Klein bottle topology
                int x2 = y==0 ? w-1-x : x;
                int x3 = y==h-1 ? w-1-x : x;
                DX[x] = A[xr] - A[xl];
                DY[x] = Aup[x2] - Adn[x3];
            }
        }
    }

    Grid<n> gridA;
    Grid<n> gridL;
    Grid<n> gridDX;
    Grid<n> gridDY;
};

GridsBase *grids = NULL;

template <int n>
class Palette {
public:
    virtual void render_line(uint8_t *pix_line,
        vecn *bufA, vecn *bufL, vecn *bufDX, vecn *bufDY,
        int w, int stride, Eigen::Vector3f acc) = 0;

    static inline void to_rgb24(uint8_t *buf, float r, float g, float b) {
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
        //return 0xff000000 | (CLIP_BYTE(bi)<<16) | (CLIP_BYTE(gi)<<8) | CLIP_BYTE(ri);
        buf[0] = CLIP_BYTE(ri);
        buf[1] = CLIP_BYTE(gi);
        buf[2] = CLIP_BYTE(bi);
    }

    template <typename T, typename U>
    static inline float get_diffuse_R2(
        const T &A,
        const T &DX,
        const T &DY,
        const U &acc
    ) {
        Eigen::Vector3f surf;
        surf[0] = DX.dot(A) * 4.0f;
        surf[1] = DY.dot(A) * 4.0f;
        surf[2] = 1;
        surf.normalize();
        return std::max(0.0f, surf.dot(acc));
    }

    template <typename T, typename U>
    static inline float get_diffuse_cross(
        const T &A,
        const T &DX,
        const T &DY,
        const U &acc
    ) {
        Eigen::Vector3f surf;
        surf[0] = (DX[0]*A[1] - DX[1]*A[0]) * 1.0f;
        surf[1] = (DY[0]*A[1] - DY[1]*A[0]) * 1.0f;
        surf[2] = 1;
        surf.normalize();
        return std::max(0.0f, surf.dot(acc));
    }

    template <typename T, typename U>
    static inline float get_diffuse_A(
        const T &A,
        const T &DX,
        const T &DY,
        const U &acc
    ) {
        Eigen::Vector3f surf;
        surf[0] = DX[0] * 4.0f;
        surf[1] = DY[0] * 4.0f;
        surf[2] = 1;
        surf.normalize();
        return std::max(0.0f, surf.dot(acc));
    }
};

struct FunctionBaseBase {
    virtual void set_params(float *p, int len) = 0;

    virtual void reset_grid() = 0;

    virtual void step() = 0;

    virtual void draw(
        int w, int h,
        uint8_t *pixels, int stride, int pal_idx,
        int dir, Eigen::Vector3f acc
    ) = 0;
};

template <int n>
struct FunctionBase : FunctionBaseBase {
    virtual ~FunctionBase() { }

    virtual matnn get_diffusion_matrix() = 0;
    virtual float get_diffusion_norm() = 0;
    virtual float get_dt() = 0;
    virtual void compute_dx_dt(vecn *buf, int w, float dt) = 0;
    virtual vecn get_background_val() = 0;
    virtual vecn get_seed_val(int seed_idx) = 0;
    virtual Palette<n> *get_palette(int id) = 0;

    GridsN<n> *get_grids(int w, int h) {
        bool realloc = !grids || grids->get_n() != n;
        if(!realloc && w) {
            realloc |= (grids->w != w);
            realloc |= (grids->h != h);
        }

        if(realloc) {
            if(!w) return NULL;
            delete(grids);
            GridsN<n> *gn = new GridsN<n>(w, h);
            grids = gn;
            reset_grid(gn);
        }

        return dynamic_cast<GridsN<n> *>(grids);
    }

    void step() {
        GridsN<n> *grids = get_grids(0, 0);
        if(!grids) return;

        int w = grids->w;
        int h = grids->h;
        int wh = grids->wh;

        matnn m = get_diffusion_matrix();
        //Eigen::JacobiSVD<matnn, Eigen::NoQRPreconditioner> svd(m);
        float diffusion_norm = get_diffusion_norm();
        float diffusion_stability = 1.0 / (diffusion_norm * 4.0);
        diffusion_stability *= 0.95;

        float dt = get_dt();

        //LOGI("dt=%g, dn=%g, ds=%g", dt, diffusion_norm, diffusion_stability);

        for(int iter=0; iter<5; iter++) {
            float lap_to_go = dt;
            while(lap_to_go > 0) {
                float lap_dt = lap_to_go;
                if(lap_dt > diffusion_stability) lap_dt = diffusion_stability;
                matnn m2 = m * lap_dt;

                grids->compute_laplacian();
                vecn *Abuf = grids->gridA.arr;
                vecn *Lbuf = grids->gridL.arr;
                for(int i=0; i<wh; i++) {
                    Abuf[i] += m2 * Lbuf[i];
                }

                lap_to_go -= lap_dt;
            }

            for(int y=0; y<h; y++) {
                vecn *bufA = grids->gridA.arr + w*y;
                compute_dx_dt(bufA, w, dt);
            }

            if(!std::isfinite(grids->gridA.arr[0][0])) {
                reset_grid(grids);
            }
        }

        gradient_dirty = 1;
    }

    void reset_grid() {
        GridsN<n> *grids = get_grids(0, 0);
        if(!grids) return;
        reset_grid(grids);
    }

    void reset_grid(GridsN<n> *grids) {
        int w = grids->w;
        int h = grids->h;
        int wh = grids->wh;

        vecn bgval = get_background_val();
        for(int i = 0; i < wh; i++) {
            grids->gridA.arr[i] = bgval;
        }

        for(int seed_idx = 0; seed_idx < 20; seed_idx++) {
            int sr = 20;
            vecn seedval = get_seed_val(seed_idx);
            int x0 = rand() % (w - sr);
            int y0 = rand() % (h - sr);
            for(int y = y0; y < y0+sr; y++) {
                vecn *buf = grids->gridA.arr + y * w;
                for(int x = x0; x < x0+sr; x++) {
                    buf[x] = seedval;
                }
            }
        }

        gradient_dirty = 1;
    }

    void draw(
        int w, int h,
        uint8_t *pixels, int stride, int pal_idx,
        int dir, Eigen::Vector3f acc
    ) {
        GridsN<n> *grids = get_grids(w, h);
        if(!grids) return;

        if(gradient_dirty) {
            grids->compute_gradient();
            gradient_dirty = 0;
        }

        Palette<n> *pal = get_palette(pal_idx);

        for(int y = 0; y < h; y++) {
            uint8_t *pix_line = pixels + y * stride;
            vecn *bufA  = grids->gridA .arr + y * w;
            vecn *bufL  = grids->gridL .arr + y * w;
            vecn *bufDX = grids->gridDX.arr + y * w;
            vecn *bufDY = grids->gridDY.arr + y * w;
            int stride = dir ? -3 : 3;
            if(dir) pix_line += 3*(w-1);
            pal->render_line(pix_line, bufA, bufL, bufDX, bufDY, w, stride, acc);
        }

#if 1
        static int print_interval = 0;
        if((print_interval++) % 20 == 0) {
            vecn *bufA  = grids->gridA .arr;
            vecn *bufL  = grids->gridL .arr;
            vecn minA = bufA[0];
            vecn maxA = bufA[0];
            vecn minL = bufL[0];
            vecn maxL = bufL[0];
            for(int x=0; x<w*h; x++) {
                for(int c=0; c<n; c++) {
                    minA[c] = std::min(minA[c], bufA[x][c]);
                    maxA[c] = std::max(maxA[c], bufA[x][c]);
                    minL[c] = std::min(minL[c], bufL[x][c]);
                    maxL[c] = std::max(maxL[c], bufL[x][c]);
                }
            }
            for(int c=0; c<n; c++) {
                LOGI("A[%d] range [%f,%f]", c, minA[c], maxA[c]);
            }
            for(int c=0; c<n; c++) {
                LOGI("L[%d] range [%f,%f]", c, minL[c], maxL[c]);
            }
        }
#endif
    }

    bool gradient_dirty;
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
        D2 = D*D;
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
        // This seems to be appropriate, but I don't know exactly why.
        //return 0.5 / std::max(5.0f, fabsf(beta*beta));
        return 0.1;
    }

    virtual void compute_dx_dt(vecn *buf, int w, float dt) {
        for(int x=0; x<w; x++) {
            float  U = buf[x][0];
            float  V = buf[x][1];
            float r2 = U*U + V*V;

            //buf[x][0] += dt * (U - (U - beta*V)*r2);
            //buf[x][1] += dt * (V - (V + beta*U)*r2);
            U += dt * U*(1.0f-r2);
            V += dt * V*(1.0f-r2);
            float t = dt*beta*r2;
            buf[x][0] = U*(1.0f-t*t/2.0f) - V*t;
            buf[x][1] = V*(1.0f-t*t/2.0f) + U*t;
        }
    }

    struct PaletteGL0 : public Palette<n> {
        PaletteGL0(GinzburgLandau &x) : parent(x) { }

        void render_line(uint8_t *pix_line,
            vecn *bufA, vecn *bufL, vecn *bufDX, vecn *bufDY,
            int w, int stride, Eigen::Vector3f acc
        ) {
            for(int x = 0; x < w; x++) {
                float diffuse = get_diffuse_R2(bufA[x], bufDX[x], bufDY[x], acc);
                float lv = parent.D2 * bufL[x].dot(bufL[x]);
                float rv = bufA[x].dot(bufA[x]);

                float green = 0;
                float red   = (0.5f-rv) * 500.0f * diffuse;
                if(red < 0) red = 0;
                float blue  = lv * 2000.0f * diffuse - red;
                if(blue < 0) blue = 0;

                apply_diffuse(diffuse, 100.0f, red, green, blue);

                to_rgb24(pix_line, red, green, blue); pix_line += stride;
            }
        }

        GinzburgLandau &parent;
    };

    struct PaletteGL1 : public Palette<n> {
        PaletteGL1(GinzburgLandau &x) : parent(x) { }

        void render_line(uint8_t *pix_line,
            vecn *bufA, vecn *bufL, vecn *bufDX, vecn *bufDY,
            int w, int stride, Eigen::Vector3f acc
        ) {
            for(int x = 0; x < w; x++) {
                float U = bufA[x][0];
                float V = bufA[x][1];
                float lU = bufL[x][0] * parent.D;
                float lV = bufL[x][1] * parent.D;
                float diffuse = get_diffuse_cross(bufA[x], bufDX[x], bufDY[x], acc);

                float rotA = U*lV - V*lU;
                float rotB = U*lU + V*lV;

                float green =  70.0f - rotA * 500.0;
                float blue  =  70.0f - rotB * 500.0;
                float red   = 0; //-70.0f + rv * 200.0f;

                red   *= diffuse;
                green *= diffuse;
                blue  *= diffuse;

                apply_diffuse(diffuse, 200.0f, red, green, blue);

                to_rgb24(pix_line, red, green, blue); pix_line += stride;
            }
        }

        GinzburgLandau &parent;
    };

    struct PaletteGL2 : public Palette<n> {
        PaletteGL2(GinzburgLandau &x) : parent(x) { }

        void render_line(uint8_t *pix_line,
            vecn *bufA, vecn *bufL, vecn *bufDX, vecn *bufDY,
            int w, int stride, Eigen::Vector3f acc
        ) {
            for(int x = 0; x < w; x++) {
                float diffuse = get_diffuse_R2(bufA[x], bufDX[x], bufDY[x], acc);

                float green = 25.0f + 150.0f*diffuse;
                float blue  = 0;
                float red   = 0;

                apply_diffuse(diffuse, 150.0f, red, green, blue);

                to_rgb24(pix_line, red, green, blue); pix_line += stride;
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

    float D, D2, alpha, beta;
    Palette<n> *pal_gl0;
    Palette<n> *pal_gl1;
    Palette<n> *pal_gl2;
};

#if 0
struct GinzburgLandauQ : public FunctionBase<4> {
    static const int n = 4;

    GinzburgLandauQ() :
        D(2.0F),
        alpha(0.0625F),
        beta (1.0F   ),
        pal_gl0(new PaletteGL0(*this)),
        pal_gl1(new PaletteGL1(*this))
    { }

    ~GinzburgLandauQ() {
        delete(pal_gl0);
        delete(pal_gl1);
    }

    virtual vecn get_background_val() {
        vecn ret;
        ret << 1, 0, 0, 0;
        return ret;
    }

    virtual vecn get_seed_val(int seed_idx) {
        vecn ret;
        ret <<
            ((seed_idx*5)%7)/7.0F*2.0F-1.0F,
            ((seed_idx*9)%13)/13.0F*2.0F-1.0F,
            ((seed_idx*11)%17)/17.0F*2.0F-1.0F,
            ((seed_idx*9)%5)/5.0F*2.0F-1.0F;
        return get_background_val() + ret;
    }

    static inline matnn quat_to_mat(float aw, float ax, float ay, float az) {
        matnn ret;
        ret <<
            aw, -ax, -ay, -az,
            ax,  aw,  az, -ay,
            ay, -az,  aw,  ax,
            az,  ay, -ax,  aw;
        return ret;
    }

    virtual void set_params(float *p, int len) {
        if(len != 3) {
            LOGE("params is wrong length: %d", len);
        }
        D     = *(p++);
        alpha = *(p++);
        beta  = *(p++);
        float theta = (float)(M_PI / 180.0) * 15.0f;

        dmat = D * quat_to_mat(1.0f, alpha, 0.0f, 0.0f);
        fmat = quat_to_mat(0.0f, sinf(theta), cos(theta), 0.0f);
    }

    virtual matnn get_diffusion_matrix() {
        return dmat;
    }

    virtual float get_diffusion_norm() {
        // This seems to be appropriate, but I don't know exactly why.
        return D * (1 + fabsf(alpha*alpha));
    }

    virtual float get_dt() {
        // This seems to be appropriate, but I don't know exactly why.
        //return 0.3 / std::max(5.0f, fabsf(beta*beta));
        return 0.05;
    }

    virtual void compute_dx_dt(vecn *buf, int w, float dt) {
        for(int x=0; x<w; x++) {
            float r2 = buf[x].squaredNorm();

            //fmat = quat_to_mat(0.0f, 0.0f, beta, 0.0f);
            //buf[x] += dt * (buf[x] - r2 * (fmat * buf[x]));
            buf[x] += dt * buf[x] * (1.0f - r2);
            float t = dt*beta*r2;
            buf[x] = buf[x]*(1.0f-t*t/2.0f) - fmat*buf[x]*t;
        }
    }

    struct PaletteGL0 : public Palette<n> {
        PaletteGL0(GinzburgLandauQ &x) : parent(x) { }

        void render_line(uint8_t *pix_line,
            vecn *bufA, vecn *bufL, vecn *bufDX, vecn *bufDY,
            int w, int stride, Eigen::Vector3f acc
        ) {
            for(int x = 0; x < w; x++) {
                float diffuse = get_diffuse_R2(bufA[x], bufDX[x], bufDY[x], acc);

                float green = 25.0f + 150.0f*diffuse;
                float blue  = 0;
                float red   = 0;

                apply_diffuse(diffuse, 50.0f, red, green, blue);

                to_rgb24(pix_line, red, green, blue); pix_line += stride;
            }
        }

        GinzburgLandauQ &parent;
    };

    struct PaletteGL1 : public Palette<n> {
        PaletteGL1(GinzburgLandauQ &x) : parent(x) { }

        void render_line(uint8_t *pix_line,
            vecn *bufA, vecn *bufL, vecn *bufDX, vecn *bufDY,
            int w, int stride, Eigen::Vector3f acc
        ) {
            for(int x = 0; x < w; x++) {
                float diffuse = get_diffuse_R2(bufA[x], bufDX[x], bufDY[x], acc);
                float rv = bufA[x].dot(bufA[x]);
                float lv = parent.D2 * bufL[x].dot(bufL[x]);

                float green = 0;
                float blue  = lv * 500 * diffuse;
                float red   = rv * 100 * diffuse + blue;

                apply_diffuse(diffuse, 50.0f, red, green, blue);

                to_rgb24(pix_line, red, green, blue); pix_line += stride;
            }
        }

        GinzburgLandauQ &parent;
    };

    virtual Palette<n> *get_palette(int id) {
        switch(id) {
            case 0: return pal_gl0;
            case 1: return pal_gl1;
            default: return pal_gl0;
        }
    }

    float D, alpha, beta;
    matnn fmat, dmat;
    Palette<n> *pal_gl0;
    Palette<n> *pal_gl1;
};
#endif

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

            buf[x][0] += dt * (-a*b*b + F*(1.0f-a));
            buf[x][1] += dt * ( a*b*b - (F+k)*b);
        }
    }

    struct PaletteGS0 : public Palette<n> {
        PaletteGS0(GrayScott &x) : parent(x) { }

        void render_line(uint8_t *pix_line,
            vecn *bufA, vecn *bufL, vecn *bufDX, vecn *bufDY,
            int w, int stride, Eigen::Vector3f acc
        ) {
            for(int x = 0; x < w; x++) {
                float A = bufA[x][0];
                float B = bufA[x][1];
                //float LA = parent.D * bufL[x][0];
                //float LB = parent.D * bufL[x][1];

                float red   = (1.0f-A) * 150.0f;
                float green = 0;
                float blue  = (A*B*B - parent.F*(1.0f-A)) * 30000.0f;

                float diffuse = get_diffuse_A(bufA[x], bufDX[x], bufDY[x], acc);
                red   *= diffuse;
                green *= diffuse;
                blue  *= diffuse;

                apply_diffuse(diffuse, 50.0f, red, green, blue);

                to_rgb24(pix_line, red, green, blue); pix_line += stride;
            }
        }

        GrayScott &parent;
    };

    struct PaletteGS1 : public Palette<n> {
        PaletteGS1(GrayScott &x) : parent(x) { }

        void render_line(uint8_t *pix_line,
            vecn *bufA, vecn *bufL, vecn *bufDX, vecn *bufDY,
            int w, int stride, Eigen::Vector3f acc
        ) {
            for(int x = 0; x < w; x++) {
                //float A = bufA[x][0];
                //float B = bufA[x][1];
                float LA = parent.D * bufL[x][0];
                float LB = parent.D * bufL[x][1];

                float red   = 64.0f + LB * 100000.0f;
                float green = 0;
                float blue  = 64.0f + LA * 60000.0f;

                float diffuse = get_diffuse_A(bufA[x], bufDX[x], bufDY[x], acc);
                red   *= diffuse;
                green *= diffuse;
                blue  *= diffuse;

                apply_diffuse(diffuse, 50.0f, red, green, blue);

                to_rgb24(pix_line, red, green, blue); pix_line += stride;
            }
        }

        GrayScott &parent;
    };

    struct PaletteGS2 : public Palette<n> {
        PaletteGS2(GrayScott &x) : parent(x) { }

        void render_line(uint8_t *pix_line,
            vecn *bufA, vecn *bufL, vecn *bufDX, vecn *bufDY,
            int w, int stride, Eigen::Vector3f acc
        ) {
            for(int x = 0; x < w; x++) {
                float diffuse = get_diffuse_A(bufA[x], bufDX[x], bufDY[x], acc);

                float green = 25.0f + 150.0f*diffuse;
                float blue  = 0;
                float red   = 0;

                apply_diffuse(diffuse, 50.0f, red, green, blue);

                to_rgb24(pix_line, red, green, blue); pix_line += stride;
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

#if 0
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
        void render_line(uint8_t *pix_line,
            vecn *bufA, vecn *bufL, vecn *bufDX, vecn *bufDY,
            int w, int stride, Eigen::Vector3f acc
        ) {
            for(int x = 0; x < w; x++) {
                float A = bufA[x][0];
                float B = bufA[x][1];
                float LA = bufL[x][0];

                float red   = (1-A) * 200;
                float green = LA * 20000;
                float blue  = B * 1000;

                to_rgb24(pix_line, red, green, blue); pix_line += stride;
            }
        }
    };

    struct PaletteWS1 : public Palette<n> {
        PaletteWS1(WackerScholl &x) : parent(x) { }

        void render_line(uint8_t *pix_line,
            vecn *bufA, vecn *bufL, vecn *bufDX, vecn *bufDY,
            int w, int stride, Eigen::Vector3f acc
        ) {
            for(int x = 0; x < w; x++) {
                //float A = bufA[x][0];
                //float B = bufA[x][1];
                float LA = bufL[x][0];
                float LB = bufL[x][1];

                float rv = parent.D * LB;
                float gv = parent.D * LA;
                //float w = sqrtf(LA*LA + LB*LB);
                float red   = rv * 60000;
                float green = 0; //parent.D * LB * 20000;
                float blue  = gv * 60000;

                to_rgb24(pix_line, red, green, blue); pix_line += stride;
            }
        }

        WackerScholl &parent;
    };

    struct PaletteWS2 : public Palette<n> {
        PaletteWS2(WackerScholl &x) : parent(x) { }

        void render_line(uint8_t *pix_line,
            vecn *bufA, vecn *bufL, vecn *bufDX, vecn *bufDY,
            int w, int stride, Eigen::Vector3f acc
        ) {
            for(int x = 0; x < w; x++) {
                float diffuse = get_diffuse_A(bufA[x], bufDX[x], bufDY[x], acc);

                float green = 255.0f*diffuse;
                float blue  = 0;
                float red   = 0;

                to_rgb24(pix_line, red, green, blue); pix_line += stride;
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
#endif

FunctionBaseBase *fn_list[] = {
    new GinzburgLandau(),
    new GrayScott()
    //new GinzburgLandauQ()
    //new WackerScholl()
};
FunctionBaseBase *fn = fn_list[0];
int pal_idx = 0;
Eigen::Vector3f last_acc;

//int profile_ticks = -1;

extern "C" {
    JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnRenderer_evolve(
        JNIEnv *env, jobject obj);
    JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnRenderer_renderFrame(
        JNIEnv *env, jobject obj, jobject bitmap, jint w, jint h, jint offset, jint dir,
        jfloat acc_x, jfloat acc_y, jfloat acc_z);
    JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnRenderer_setParams(
        JNIEnv *env, jobject obj, jint fn_idx, jfloatArray params, jint pal);
    JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnRenderer_setColorMatrix(
        JNIEnv *env, jobject obj, jfloatArray new_cm_arr);
    JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnRenderer_resetGrid(
        JNIEnv *env, jobject obj);
};

JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnRenderer_renderFrame(
    JNIEnv *env, jobject obj, jobject bitmap, jint w, jint h, jint offset, jint dir,
    jfloat acc_x, jfloat acc_y, jfloat acc_z
) {
    uint8_t *pixels = (uint8_t *)(env->GetDirectBufferAddress(bitmap));
    pixels += offset;

    // FIXME
    if(!grids) {
        last_acc << 0, 1, 0;
    }

    Eigen::Vector3f acc;
    acc << acc_x, acc_y, acc_z;
    acc.normalize();

    if(fabsf(acc[2]) > 0.9) {
        acc = last_acc;
    } else {
        last_acc = acc;
    }

    acc[2] = 0;
    acc.normalize();
    acc[2] = 2;
    acc.normalize();

    if(dir) acc[0] *= -1;

    //LOGI("acc=%f,%f,%f", acc[0], acc[1], acc[2]);

    fn->draw(w, h, pixels, w*3, pal_idx, dir, acc);
}

JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnRenderer_evolve(
    JNIEnv *env, jobject obj
) {
    fn->step();

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

JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnRenderer_setParams(
    JNIEnv *env, jobject obj, jint fn_idx, jfloatArray params_in, jint _pal_idx
) {
    fn = fn_list[fn_idx];
    pal_idx = _pal_idx;
    jfloat *params = env->GetFloatArrayElements(params_in, NULL);
    jsize len = env->GetArrayLength(params_in);
    fn->set_params((float *)params, len);
    env->ReleaseFloatArrayElements(params_in, params, JNI_ABORT);
}

JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnRenderer_setColorMatrix(
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

JNIEXPORT void JNICALL Java_org_stahlke_rdnwallpaper_RdnRenderer_resetGrid(
    JNIEnv *env, jobject obj
) {
    fn->reset_grid();
}
