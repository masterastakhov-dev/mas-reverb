#define main originalTestMain
#include "space_plugin_tests.cpp"
#undef main
#include "../Source/MasterSpace/ALMCalibration.h"
int main()
{
    using namespace lvl;
    using namespace mas::alm;
    juce::ScopedJuceInitialiser_GUI init;
    auto root=juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ms-alm-survey");
    setenv("MASTER_SPACE_PRESET_ROOT",root.getFullPathName().toRawUTF8(),1);
    auto probe=std::make_unique<MasterSpaceAudioProcessor>();
    auto p=probe->defaultParamMap();
    p[ParamIDs::mode]=0; p[ParamIDs::routing]=0; p[ParamIDs::depth]=35;
    p[ParamIDs::output]=0; p[ParamIDs::duckOn]=0; p[ParamIDs::rgOn]=0;
    p[ParamIDs::hcOn]=0; p[ParamIDs::cpOn]=0; p[ParamIDs::eqOn]=0;
    for(double fs : {48000.,96000.})
    {
        auto in=material(0,fs,12.);
        auto render=[&](const ParamMap& v)
        {
            auto proc=std::make_unique<MasterSpaceAudioProcessor>();
            proc->setRateAndBufferSizeDetails(fs,256);
            proc->applyParamMap(v);
            proc->prepareToPlay(fs,256);
            auto out=in; run(*proc,out,256); return out;
        };
        auto d=p,e=p,l=p;
        d[ParamIDs::mix]=0; d[ParamIDs::erOn]=0; d[ParamIDs::lateOn]=0;
        e[ParamIDs::mix]=100; e[ParamIDs::lateOn]=0;
        l[ParamIDs::mix]=100; l[ParamIDs::erOn]=0;
        auto direct=render(d),early=render(e),late=render(l),actual=render(p);
        double w=p[ParamIDs::mix]*0.01*1.5707963267948966;
        double wd=std::cos(w),ww=std::sin(w),ey=0,ex=0,err=0;
        KW ki(fs),kd(fs),ke(fs),kl(fs),ka(fs);
        CalibrationAccumulator accum;
        for(int i=0;i<in.getNumSamples();++i) for(int c=0;c<2;++c)
        {
            double x=ki.run(c,in.getSample(c,i)),a=kd.run(c,direct.getSample(c,i));
            double b=ke.run(c,early.getSample(c,i)),t=kl.run(c,late.getSample(c,i));
            double y=ka.run(c,actual.getSample(c,i));
            if(i<int(4*fs)) continue;
            accum.add(x,a,b,t);
            double delta=y-(wd*a+ww*(b+t));
            ey+=y*y; ex+=x*x; err+=delta*delta;
        }
        auto m=estimate(accum.finish(),wd,ww,ww);
        if(!m.valid || !std::isfinite(ey) || !std::isfinite(ex) ||
           !std::isfinite(err) || ey<=0 || ex<=0) return 1;
        double actualDb=10*std::log10(ex/ey);
        double residual=10*std::log10(std::max(err/ey,1e-30));
        std::printf("ALM %.0f model=%+.3f actual=%+.3f error=%.3f residual=%.2f dB\n",
            fs,m.db,actualDb,std::abs(double(m.db)-actualDb),residual);
    }
    return 0;
}
