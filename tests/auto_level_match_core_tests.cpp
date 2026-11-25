#include "../Source/MasterSpace/AutoLevelMatch.h"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

using namespace mas::alm;
static int checks = 0;
static void check(bool ok, const char* msg)
{
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << msg << '\n'; std::exit(1); }
}
static bool near(double a,double b,double tol=1e-5) { return std::abs(a-b)<tol; }

int main()
{
    const Calibration unity {1.0,{1.0,1.0,1.0,0,0,0}};
    const Calibration coherent {1.0,{1.0,1.0,1.0,1,1,1}};
    const double h=std::sqrt(0.5);
    check(estimate(unity,h,h,0).valid && near(estimate(unity,h,h,0).db,0), "uncorrelated equal power");
    check(near(estimate(coherent,h,h,0).db,-3.0102999566), "direct/early covariance");
    check(near(estimate(coherent,0,h,h).db,-3.0102999566), "early/late covariance");
    check(near(estimate(coherent,h,0,h).db,-3.0102999566), "direct/late covariance");
    const Calibration negative {1.0,{1.0,1.0,0,-1,0,0}};
    check(!estimate(negative,h,h,0).valid, "cancellation fallback");
    check(near(estimate(unity,0.5,0,0).db,6.020599913), "known gain");
    check(estimate(unity,0.1,0,0).limited && estimate(unity,0.1,0,0).db==12, "boost limit");
    check(estimate(unity,3,0,0).limited && estimate(unity,3,0,0).db==-6, "cut limit");
    check(!estimate(unity,0,0,0).valid, "silence fallback");
    check(!estimate(Calibration{},1,0,0).valid, "missing calibration");
    check(!estimate(Calibration{1,{1,1,1,2,0,0}},1,1,0).valid, "bad covariance");
    check(!estimate(Calibration{1,{1,1,1,.9,.9,-.9}},1,1,1).valid, "non-PSD covariance");
    check(!estimate(Calibration{1,{1,1,1,0,0,std::numeric_limits<double>::quiet_NaN()}},1,1,1).valid, "NaN calibration");
    check(!estimate(unity,-1,0,0).valid, "negative weight");
    check(!estimate(unity,std::numeric_limits<double>::infinity(),0,0).valid, "infinite weight");
    check(near(estimate(Calibration{1e-20,{1e-20,1e-20,1e-20,0,0,0}},0.5,0,0).db,6.020599913), "scale invariant");
    for(double fs : {44100.0,48000.0,96000.0})
    {
        StereoGain a,b;
        a.prepare(fs); b.prepare(fs);
        const int n=static_cast<int>(fs*0.1);
        std::vector<float> l(n,0.25f),r(n,-0.125f),m=l,s=r;
        const auto e=estimate(unity,0.5,0,0);
        a.setTarget(e,true,true); a.process(l.data(),r.data(),n);
        for(int i=0;i<n;i+=64)
        {
            b.setTarget(e,true,true);
            b.process(m.data()+i,s.data()+i,std::min(64,n-i));
        }
        check(std::memcmp(l.data(),m.data(),n*sizeof(float))==0 &&
              std::memcmp(r.data(),s.data(),n*sizeof(float))==0,"block partition exact");
        // dB is stored as float; converting 6.0205999 dB back to linear gain need not yield exactly 2.
        const float expected = std::pow(10.0f,e.db/20.0f);
        check(a.gain()==expected && b.gain()==expected && near(expected,2.0,1e-6),"settled gain");
        for(int i=0;i<n;++i)
        {
            if (r[i]!=-0.5f*l[i]) { check(false,"stereo ratio"); }
            if(i>0 && (l[i]<l[i-1] || l[i]-l[i-1]>0.001f)) check(false,"smooth monotonic ramp");
        }
        check(true,"stereo ratio and monotonic ramp");
        a.setTarget(e,false,true); a.process(l.data(),r.data(),n);
        check(a.gain()==1.0f,"OFF settles to unity");
        std::array<float,4> bits{0.0f,-0.0f,0.1234567f,-0.987654f}, orig=bits;
        a.process(bits.data(),nullptr,4);
        check(std::memcmp(bits.data(),orig.data(),sizeof(bits))==0,"OFF bit identity");
        a.setTarget(e,true,true); a.process(l.data(),r.data(),n);
        a.setTarget(e,true,false); a.process(bits.data(),nullptr,4);
        check(a.gain()==1.0f && std::memcmp(bits.data(),orig.data(),sizeof(bits))==0,"AUX exact unity");
        a.setTarget(e,true,true); a.process(l.data(),r.data(),n);
        a.setTarget({},true,true); a.process(l.data(),r.data(),n);
        check(a.gain()==1.0f,"invalid calibration returns unity");
        a.setTarget(e,true,true); a.process(l.data(),nullptr,n);
        float mono=0.25f; a.process(&mono,&mono,1);
        check(mono==0.25f*expected && near(mono,0.5,1e-6),"aliased mono not multiplied twice");
        float zero=0; a.process(&zero,nullptr,1);
        check(zero==0,"no generated signal");
    }
    std::cout << checks << " checks, 0 failed\n";
}
