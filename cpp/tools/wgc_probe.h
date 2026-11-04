#pragma once
#include <d3d11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <thread>
#include <chrono>
inline cs2n::Image captureWgc(HWND window){
 using namespace winrt;
 using namespace winrt::Windows::Graphics::Capture;
 using namespace winrt::Windows::Graphics::DirectX;
 struct Apartment {Apartment(){winrt::init_apartment(winrt::apartment_type::multi_threaded);} ~Apartment(){winrt::uninit_apartment();}};
 thread_local Apartment apartment; // Keep the apartment alive across consecutive captures.
 if(!GraphicsCaptureSession::IsSupported())throw std::runtime_error("Windows Graphics Capture unsupported");
 com_ptr<ID3D11Device> device;com_ptr<ID3D11DeviceContext> context;
 check_hresult(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,D3D11_CREATE_DEVICE_BGRA_SUPPORT,nullptr,0,D3D11_SDK_VERSION,device.put(),nullptr,context.put()));
 auto dxgi=device.as<IDXGIDevice>();com_ptr<IInspectable> inspectable;
 check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(),inspectable.put()));
 auto graphicsDevice=inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();
 auto interop=get_activation_factory<GraphicsCaptureItem,IGraphicsCaptureItemInterop>();
 GraphicsCaptureItem item{nullptr};check_hresult(interop->CreateForWindow(window,guid_of<GraphicsCaptureItem>(),put_abi(item)));
 auto size=item.Size();if(size.Width<=0||size.Height<=0||size.Width>4096||size.Height>4096)throw std::runtime_error("Invalid capture dimensions");
 auto pool=Direct3D11CaptureFramePool::CreateFreeThreaded(graphicsDevice,DirectXPixelFormat::B8G8R8A8UIntNormalized,2,size);
 auto session=pool.CreateCaptureSession(item);
 struct Close {GraphicsCaptureSession session;Direct3D11CaptureFramePool pool;~Close(){try{session.Close();pool.Close();}catch(...){}}} close{session,pool};
 session.StartCapture();
 Direct3D11CaptureFrame frame{nullptr};auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(4);
 while(std::chrono::steady_clock::now()<deadline){frame=pool.TryGetNextFrame();if(frame)break;std::this_thread::sleep_for(std::chrono::milliseconds(10));}
 if(!frame)throw std::runtime_error("WGC: no frame within 4 seconds");
 auto access=frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
 com_ptr<ID3D11Texture2D> texture;check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D),texture.put_void()));
 D3D11_TEXTURE2D_DESC desc{};texture->GetDesc(&desc);
 desc.Usage=D3D11_USAGE_STAGING;desc.BindFlags=0;desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;desc.MiscFlags=0;
 com_ptr<ID3D11Texture2D> staging;check_hresult(device->CreateTexture2D(&desc,nullptr,staging.put()));context->CopyResource(staging.get(),texture.get());
 D3D11_MAPPED_SUBRESOURCE mapped{};check_hresult(context->Map(staging.get(),0,D3D11_MAP_READ,0,&mapped));
 struct Unmap{ID3D11DeviceContext* context;ID3D11Texture2D* texture;~Unmap(){context->Unmap(texture,0);}} unmap{context.get(),staging.get()};
 auto actual=frame.ContentSize();if(actual.Width<=0||actual.Height<=0||actual.Width>int(desc.Width)||actual.Height>int(desc.Height))throw std::runtime_error("Capture resized; retry required");
 cs2n::Image result(actual.Width,actual.Height);
 for(int y=0;y<actual.Height;++y){auto row=reinterpret_cast<const std::uint32_t*>(static_cast<const unsigned char*>(mapped.pData)+y*mapped.RowPitch);for(int x=0;x<actual.Width;++x)result.at(x,y)=cs2n::rgbFromDib(row[x]);}
 return result;
}
