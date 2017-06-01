#include "common.inc"
#include "helper_cuda.h"

template<bool WithMoment>
void __global__ Iterate(double zr, double zi, double xscale, double yscale, double* results)
{
    const double escape_radius_squared = ESCAPE_RADIUS_SQUARED;
    const int maxiter = MAXITER;

    unsigned slotno = blockIdx.x * blockDim.x + threadIdx.x;
    unsigned x = slotno % Xres, y = slotno / Xres;
    if(y >= Yres) return;

    double cr = zr += xscale*int(x-Xres/2), sr = cr;
    double ci = zi += yscale*int(y-Yres/2), si = ci;
    double dist = 0;
    int iter    = maxiter;
    bool act    = true;

    if(zr*(1+zr*(8*zr*zr+(16*zi*zi-3)))+zi*zi*(8*zi*zi-3) < 3./32 || ((zr+1)*(zr+1)+zi*zi)<1./16) { act=false; iter=0; }

    while(act)
    for(unsigned n=0; n < 10; ++n)
    {
        double r2 = cr * cr;
        double i2 = ci * ci;
        dist = act ? r2 + i2 : dist;
        act = iter && dist < escape_radius_squared ? act : false;
        iter -= act;
        double ri = cr * ci;
        ci = zi + (ri * 2);
        cr = zr + (r2 - i2);

        if(WithMoment && !n)
        {
            bool moment = iter & (iter-1);
            iter = (cr == sr && ci == si) ? 0 : iter;
            sr = moment ? sr : cr;
            si = moment ? si : ci;
        }
    }
    results[slotno] = iter ? std::log2( maxiter-iter + 1 - std::log2(std::log2(dist) / 2)) * (4/std::log2(std::exp(1.))) : 0;
}

constexpr unsigned npixels = Xres * Yres, nthreads = 128, nblocks = (npixels + nthreads - 1) / nthreads;

int main()
{
    static double results[npixels], *p = NULL;
    checkCudaErrors(cudaMalloc((void**)&p, sizeof(results))); assert(p != NULL);

    bool NeedMoment = true;

    MAINLOOP_START();
    while(MAINLOOP_GET_CONDITION())
    {
        double zr, zi, xscale, yscale; MAINLOOP_SET_COORDINATES();

        if(NeedMoment)
            Iterate<true><<<nblocks, nthreads, 0>>>( zr, zi, xscale, yscale, p);
        else
            Iterate<false><<<nblocks, nthreads, 0>>>( zr, zi, xscale, yscale, p);

        checkCudaErrors(cudaMemcpy(results, p, sizeof(results), cudaMemcpyDeviceToHost));

        unsigned n_inside = std::count_if(results, results+npixels, std::bind1st(std::equal_to<double>(), 0.));
        NeedMoment = n_inside >= (Xres*Yres)/1024;

        std::vector<unsigned> pixels (Xres * Yres);

        #pragma omp parallel for
        for(unsigned y=0; y<Yres; ++y)
            for(unsigned x=0; x<Xres; ++x)
                pixels[y*Xres + x] = Color(x,y, results[y*Xres+x]);

        /*static std::FILE* fp = std::fopen("/mnt/arch/mandelbrot-4K.bin", "wb");
        std::fwrite(&pixels[0], Xres*Yres, sizeof(unsigned), fp);
        std::fflush(fp);*/

        MAINLOOP_PUT_RESULT(pixels);
    }
    MAINLOOP_FINISH();
}