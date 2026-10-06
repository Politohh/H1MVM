#include "component/demo/theater_scene_math.hpp"
#include <cassert>
#include <iostream>
int main()
{
    using namespace theater_scene::math;
    for (const vector rotation : {vector{0,0,0}, vector{30,70,-20}, vector{-89,170,60}})
    {
        const auto basis = angles(rotation.data());
        const vector displacement{12,-35,77};
        const auto recovered = world(local(displacement,basis),basis);
        for (int i=0;i<3;++i) assert(std::abs(recovered[i]-displacement[i])<.0001f);
        const auto forward = direction(rotation[0],rotation[1]);
        for (int i=0;i<3;++i) assert(std::abs(forward[i]-basis[0][i])<.00001f);
        for (int i=0;i<3;++i)
        {
            const auto reconstructed = world(local(basis[i],basis),basis);
            for(int j=0;j<3;++j) assert(std::abs(reconstructed[j]-basis[i][j])<.00001f);
        }
    }
    assert(color(1,0,0)==0xffff0000u && color(0,0,1)==0xff0000ffu);
    const auto sunrise = day_angles(6), noon = day_angles(12), sunset = day_angles(18);
    assert(std::abs(sunrise[0])<.001f && std::abs(sunset[0])<.001f);
    assert(std::abs(noon[0]+45)<.001f && std::abs(noon[1])<.001f);
    assert(std::abs(sunrise[1]+90)<.001f && std::abs(sunset[1]-90)<.001f);
    const auto midnight = direction(day_angles(0)[0],day_angles(0)[1]);
    const auto midnight24 = direction(day_angles(24)[0],day_angles(24)[1]);
    for(int i=0;i<3;++i) assert(std::abs(midnight[i]-midnight24[i])<.00001f);
    assert(midnight[2]<0);
    for(float hour=0;hour<=24;hour+=.05f)
    {
        const auto sun=day_angles(hour);
        const auto dir=direction(sun[0],sun[1]);
        const auto up=direction(sun[0]-90,sun[1]);
        float norm{}, dot{};
        for(int i=0;i<3;++i) { norm+=dir[i]*dir[i]; dot+=dir[i]*up[i]; }
        assert(std::abs(norm-1)<.00001f && std::abs(dot)<.00001f);
        const auto next=day_angles(hour+.001f);
        const auto ray=direction(next[0],next[1]);
        for(int i=0;i<3;++i) assert(std::abs(ray[i]-dir[i])<.001f);
    }
    fog original; original.bytes.fill(0xab);
    auto modified=original;
    modified.set<float>(80,250.f); modified.set<std::uint32_t>(148,color(.25f,.5f,1));
    assert(modified.get<float>(80)==250.f);
    assert(modified.get<std::uint32_t>(148)==0xff4080ffu);
    for(std::size_t i=0;i<156;++i) if (!(i>=80 && i<84) && !(i>=148 && i<152))
        assert(modified.bytes[i]==original.bytes[i]);
    std::cout << "PASS bone local/world preservation, camera basis, sun direction and native BGRA/fog storage\n";
}
