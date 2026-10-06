#pragma once
#include "theater_sky_format.hpp"
#include <d3d11.h>

namespace theater_sky
{
    inline HRESULT create_texture(ID3D11Device* device,const header& h,const char* pixels,
        ID3D11Texture2D** texture,ID3D11ShaderResourceView** shader_view)
    {
        if (!device || !pixels || !texture || !shader_view) return E_INVALIDARG;
        *texture=nullptr;*shader_view=nullptr;
        const auto parts=layout(h);
        std::vector<D3D11_SUBRESOURCE_DATA> data;
        for (const auto& part:parts) data.push_back({pixels+part.offset,part.pitch,part.size});
        D3D11_TEXTURE2D_DESC description{};
        description.Width=h.width;description.Height=h.height;description.MipLevels=h.mips;
        description.ArraySize=6;description.Format=static_cast<DXGI_FORMAT>(h.format);description.SampleDesc.Count=1;
        description.Usage=D3D11_USAGE_IMMUTABLE;description.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        description.MiscFlags=D3D11_RESOURCE_MISC_TEXTURECUBE;
        auto result=device->CreateTexture2D(&description,data.data(),texture);
        if (FAILED(result)) return result;
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};view.Format=description.Format;
        view.ViewDimension=D3D11_SRV_DIMENSION_TEXTURECUBE;view.TextureCube.MipLevels=h.mips;
        result=device->CreateShaderResourceView(*texture,&view,shader_view);
        if (FAILED(result)) { (*texture)->Release();*texture=nullptr; }
        return result;
    }
}
