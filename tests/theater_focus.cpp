#define NOMINMAX
#include "component/demo/theater_focus_gpu.hpp"
#include <cassert>
#include <iostream>
#include <limits>
#include <thread>
#include <chrono>

int main()
{
    using namespace theater_focus;
    assert(input_allowed(0, false)); // unobstructed camera
    assert(input_allowed(0x40, true)); // editor and timeline continue following the cursor
    assert(!input_allowed(0x40, false)); // unrelated native menu
    for (int catchers : {1, 8, 0x10, 0x41, 0x48, 0x50}) assert(!input_allowed(catchers, true));
    assert(std::abs(*view_depth(.99f, 1.f, -1.f) - 100.f) < .001f);
    assert(std::abs(*view_depth(.01f, 0.f, 1.f) - 100.f) < .001f);
    // H1's physical DOF reserves the top 1/64 of reversed depth for viewmodels.
    const float world_near = 4.f, world_focus = 640.f;
    const float hardware_depth = world_near * world_depth_range / world_focus;
    assert(std::abs(*view_depth(hardware_depth, 0, world_near * world_depth_range) - world_focus) < .001f);
    assert(!view_depth(1.f, 1.f, -1.f)); // sky
    assert(!view_depth(0.f, 0.f, 1.f)); // reversed-Z sky
    assert(!view_depth(std::numeric_limits<float>::quiet_NaN(), 1.f, -1.f));
    assert(!view_depth(-.1f, 1.f, -1.f));
    assert(!view_depth(.5f, 1.f, 1.f)); // behind camera
    assert(pixel(1.f, 1920) == 1919 && pixel(-1.f, 1080) == 0);
    float at30 = 100, at240 = 100;
    for (int i = 0; i < 30; ++i) at30 = smooth_focus(at30, 500, 1.f / 30, .2f);
    for (int i = 0; i < 240; ++i) at240 = smooth_focus(at240, 500, 1.f / 240, .2f);
    assert(std::abs(at30 - at240) < .001f);
    assert(smooth_focus(100, 500, .001f, 1) == 500);
    assert(smooth_focus(100, 500, 0, .2f) == 100);
    assert(smooth_focus(100, 500, 1.f/60, .8f) > smooth_focus(100, 500, 1.f/60, .2f));
    assert(std::abs(smooth_focus(100, 500, 1.f/60, .2f) - 180.f) < .001f);
    assert(smooth_focus(500, 100, 1.f/60, .8f) < smooth_focus(500, 100, 1.f/60, .2f));
    assert(speed_from_response(0) == 1);
    assert(std::abs(smooth_focus(100, 500, .05f, speed_from_response(.2f)) -
        (100.f + 400.f * (1.f - std::exp(-.05f/.2f)))) < .001f);

    ID3D11Device* device{}; ID3D11DeviceContext* context{};
    assert(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, nullptr, 0,
        D3D11_SDK_VERSION, &device, nullptr, &context)));
    const float pixels[] = {0.f, .5f, .75f, .875f, .9375f, .96875f};
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 3; desc.Height = 2; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R32_FLOAT; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    D3D11_SUBRESOURCE_DATA data{pixels, 3 * sizeof(float), 0};
    ID3D11Texture2D* texture{};
    assert(SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture)));
    depth_reader reader;
    auto wait_for = [&](float x, float y, float a, float b, float expected)
    {
        reader.reset();
        std::optional<float> z;
        for (int i = 0; i < 200 && !z; ++i)
        {
            z = reader.sample(texture, x, y, a, b);
            // Only the test flushes: the game's normal submissions drive GPU progress.
            context->Flush();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        assert(z && std::abs(*z - expected) < .001f);
    };
    wait_for(1.f, 1.f, 1.f, -1.f, 32.f); // right/bottom edge, real GPU row pitch
    wait_for(0.f, 0.f, 1.f, -1.f, 1.f);
    wait_for(.5f, 0.f, 0.f, 1.f, 2.f); // reversed projection
    reader.reset(); texture->Release(); texture = nullptr;
    desc.Format = DXGI_FORMAT_R32_TYPELESS; // H1's actual $floatz resource
    assert(SUCCEEDED(device->CreateTexture2D(&desc, &data, &texture)));
    wait_for(1.f, 1.f, 1.f, -1.f, 32.f);
    wait_for(.5f, 0.f, 0.f, 1.f, 2.f);
    assert(!reader.sample(nullptr, 0, 0, 1, -1)); // reset/unavailable buffer
    reader.reset(); texture->Release(); context->Release(); device->Release();
    std::cout << "PASS mouse DOF: projection/sky/invalid depth, frame-rate-independent smoothing, WARP pixel readback and reset\n";
}
