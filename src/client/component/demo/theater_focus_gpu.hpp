#pragma once
#include <d3d11.h>
#include <array>
#include <optional>
#include <cstdint>
#include "theater_focus_math.hpp"

namespace theater_focus
{
    class depth_reader
    {
        struct slot
        {
            ID3D11Texture2D* texture{};
            bool pending{};
            float a{}, b{};
            std::uint64_t sequence{};
        };
        std::array<slot, 3> slots{};
        ID3D11Device* device{};
        std::uint64_t sequence{}, accepted{};
    public:
        depth_reader() = default;
        depth_reader(const depth_reader&) = delete;
        depth_reader& operator=(const depth_reader&) = delete;
        ~depth_reader() { reset(); }
        void reset()
        {
            for (auto& s : slots) { if (s.texture) s.texture->Release(); s = {}; }
            if (device) device->Release();
            device = nullptr; sequence = accepted = 0;
        }
        // Called only from the native render pass. Never blocks or flushes the GPU.
        std::optional<float> sample(ID3D11Texture2D* source, float x, float y, float a, float b)
        {
            if (!source) { reset(); return {}; }
            D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
            if ((desc.Format != DXGI_FORMAT_R32_FLOAT && desc.Format != DXGI_FORMAT_R32_TYPELESS) || desc.SampleDesc.Count != 1 ||
                !desc.Width || !desc.Height || desc.ArraySize != 1) return {};
            ID3D11Device* owner{}; source->GetDevice(&owner);
            if (owner != device)
            {
                reset(); device = owner;
                D3D11_TEXTURE2D_DESC staging{};
                staging.Width = staging.Height = staging.MipLevels = staging.ArraySize = 1;
                staging.Format = DXGI_FORMAT_R32_FLOAT; staging.SampleDesc.Count = 1;
                staging.Usage = D3D11_USAGE_STAGING; staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                for (auto& s : slots)
                    if (FAILED(device->CreateTexture2D(&staging, nullptr, &s.texture))) { reset(); return {}; }
            }
            else owner->Release();
            ID3D11DeviceContext* context{}; device->GetImmediateContext(&context);
            std::optional<float> result;
            for (auto& s : slots) if (s.pending)
            {
                D3D11_MAPPED_SUBRESOURCE mapped{};
                const auto hr = context->Map(s.texture, 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped);
                if (SUCCEEDED(hr))
                {
                    const float depth = *static_cast<const float*>(mapped.pData);
                    context->Unmap(s.texture, 0); s.pending = false;
                    if (s.sequence > accepted) { accepted = s.sequence; result = view_depth(depth, s.a, s.b); }
                }
                else if (hr != DXGI_ERROR_WAS_STILL_DRAWING) s.pending = false;
            }
            for (auto& s : slots) if (!s.pending)
            {
                const auto px = pixel(x, desc.Width), py = pixel(y, desc.Height);
                const D3D11_BOX box{px, py, 0, px + 1, py + 1, 1};
                context->CopySubresourceRegion(s.texture, 0, 0, 0, 0, source, 0, &box);
                s.pending = true; s.a = a; s.b = b; s.sequence = ++sequence;
                break;
            }
            context->Release();
            return result;
        }
    };
}
