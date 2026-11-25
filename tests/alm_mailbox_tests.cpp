#include "../Source/MasterSpace/ALMCalibrationMailbox.h"
#include <cassert>
#include <limits>
int main()
{
    using namespace mas::alm;
    CalibrationMailbox box;
    assert(!box.read().estimate.valid);
    auto a=box.begin(),b=box.begin();
    assert(!box.publish(a,{3,true,false}));
    assert(!box.read().estimate.valid && box.read().ticket==b);
    assert(!box.publish(b,{std::numeric_limits<float>::quiet_NaN(),true,false}));
    assert(box.publish(b,{3.4567f,true,false}));
    assert(box.read().ticket==b && box.read().estimate.valid);
    assert(std::abs(box.read().estimate.db-3.457f)<1e-5f);
    assert(!box.publish(b,{1,true,false}));
    auto c=box.begin();
    assert(box.publish(c,{50,true,false}) && box.read().estimate.limited);
    assert(std::abs(box.read().estimate.db-12)<1e-5f);
}
