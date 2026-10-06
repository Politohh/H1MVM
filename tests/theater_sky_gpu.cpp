#include "component/demo/theater_sky_gpu.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
int main(int argc,char** argv)
{
    if(argc!=2) return 2;
    ID3D11Device* device{};ID3D11DeviceContext* context{};
    if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,nullptr,&context))) return 3;
    unsigned count=0;std::set<std::string> names;
    try
    {
        for(const auto& file:std::filesystem::recursive_directory_iterator(argv[1]))
        {
            if(file.path().extension()!=".h1sky" || !names.insert(file.path().filename().string()).second) continue;
            std::ifstream input(file.path(),std::ios::binary);theater_sky::header h{};
            input.read(reinterpret_cast<char*>(&h),sizeof(h));(void)theater_sky::layout(h);
            if(!input || std::filesystem::file_size(file.path())!=sizeof(h)+h.size) throw std::runtime_error("Bad sky file length");
            std::vector<char> pixels(h.size);input.read(pixels.data(),pixels.size());
            if(!input) throw std::runtime_error("Truncated sky pixels");
            ID3D11Texture2D* texture{};ID3D11ShaderResourceView* view{};
            if(FAILED(theater_sky::create_texture(device,h,pixels.data(),&texture,&view))) throw std::runtime_error("GPU rejected cubemap");
            D3D11_TEXTURE2D_DESC actual{};texture->GetDesc(&actual);
            if(actual.ArraySize!=6 || actual.Width!=h.width || actual.Format!=static_cast<DXGI_FORMAT>(h.format) || actual.MipLevels!=h.mips) throw std::runtime_error("GPU descriptor mismatch");
            view->Release();texture->Release();++count;
        }
    }
    catch(const std::exception& e) {std::cerr<<e.what()<<"\n";context->Release();device->Release();return 1;}
    context->Release();device->Release();
    std::cout<<"PASS hardware D3D11 cubemap creation and shader views: "<<count<<" native skies\n";
    return count?0:1;
}
