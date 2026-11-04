#include "perception/audio.h"
#include <Windows.h>
#include <TlHelp32.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <wrl.h>
#include <chrono>
#include <cmath>
#include <algorithm>
#include "core/log.h"
namespace cs2n {
using Microsoft::WRL::ComPtr;
namespace {
double seconds() {return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();}
class Activation final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,IActivateAudioInterfaceCompletionHandler,Microsoft::WRL::FtmBase> {
 public:
  HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
  HRESULT result=E_PENDING;
  ComPtr<IAudioClient> client;
  ~Activation(){if(event)CloseHandle(event);}
  STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation* operation) override {
    ComPtr<IUnknown> object;HRESULT activation=E_FAIL;
    result=operation->GetActivateResult(&activation,&object);
    if(SUCCEEDED(result))result=activation;
    if(SUCCEEDED(result))result=object.As(&client);
    SetEvent(event);return S_OK;
  }
};
DWORD gamePid() {
  HANDLE h=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(h==INVALID_HANDLE_VALUE)return 0;
  PROCESSENTRY32W p{};p.dwSize=sizeof(p);DWORD id=0;
  if(Process32FirstW(h,&p))do{if(_wcsicmp(p.szExeFile,L"cs2.exe")==0){id=p.th32ProcessID;break;}}while(Process32NextW(h,&p));
  CloseHandle(h);return id;
}
}
GameAudio::GameAudio():thread_([this]{run();}){}
GameAudio::~GameAudio(){stop_=true;if(thread_.joinable())thread_.join();}
SoundHint GameAudio::latest()const{std::lock_guard lock(mutex_);return hint_;}
void GameAudio::run() {
  const HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
  while(!stop_) {
    DWORD pid=gamePid();
    if(!pid){for(int i=0;i<20&&!stop_;++i)Sleep(100);continue;}
    auto activation=Microsoft::WRL::Make<Activation>();
    AUDIOCLIENT_ACTIVATION_PARAMS params{};params.ActivationType=AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    params.ProcessLoopbackParams.TargetProcessId=pid;
    params.ProcessLoopbackParams.ProcessLoopbackMode=PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
    PROPVARIANT variant{};variant.vt=VT_BLOB;variant.blob.cbSize=sizeof(params);variant.blob.pBlobData=reinterpret_cast<BYTE*>(&params);
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    HRESULT hr=ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,__uuidof(IAudioClient),&variant,activation.Get(),&operation);
    if(SUCCEEDED(hr)) {
      DWORD wait=WAIT_TIMEOUT;
      for(int i=0;i<50&&!stop_&&wait==WAIT_TIMEOUT;++i)wait=WaitForSingleObject(activation->event,100);
      hr=wait==WAIT_OBJECT_0?activation->result:E_FAIL;
    }
    WAVEFORMATEX format{};format.wFormatTag=WAVE_FORMAT_PCM;format.nChannels=2;format.nSamplesPerSec=44100;
    format.wBitsPerSample=16;format.nBlockAlign=4;format.nAvgBytesPerSec=176400;
    ComPtr<IAudioCaptureClient> capture;
    if(SUCCEEDED(hr))hr=activation->client->Initialize(AUDCLNT_SHAREMODE_SHARED,AUDCLNT_STREAMFLAGS_LOOPBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,200000,0,&format,nullptr);
    if(SUCCEEDED(hr))hr=activation->client->GetService(IID_PPV_ARGS(&capture));
    if(SUCCEEDED(hr))hr=activation->client->Start();
    if(FAILED(hr)) {
      Logger::instance().warn("Game audio unavailable HRESULT="+std::to_string(hr));
      for(int i=0;i<100&&!stop_;++i)Sleep(100);continue;
    }
    active_=true;Logger::instance().info("Game audio: process loopback active");
    double baseline=.0001,lastEvent=-1000;HANDLE process=OpenProcess(SYNCHRONIZE,FALSE,pid);
    while(!stop_&&(!process||WaitForSingleObject(process,0)==WAIT_TIMEOUT)) {
      UINT32 count=0;hr=capture->GetNextPacketSize(&count);if(FAILED(hr))break;
      if(!count){Sleep(10);continue;}
      BYTE* bytes=nullptr;DWORD flags=0;UINT32 frames=0;
      hr=capture->GetBuffer(&bytes,&frames,&flags,nullptr,nullptr);if(FAILED(hr))break;
      double left=0,right=0,difference=0;
      if(!(flags&AUDCLNT_BUFFERFLAGS_SILENT)&&bytes) {
        auto samples=reinterpret_cast<const short*>(bytes);
        for(UINT32 i=0;i<frames;++i){double l=samples[2*i]/32768.,r=samples[2*i+1]/32768.;left+=l*l;right+=r*r;
          if(i){double dl=l-samples[2*i-2]/32768.,dr=r-samples[2*i-1]/32768.;difference+=dl*dl+dr*dr;}}
      }
      capture->ReleaseBuffer(frames);
      left=std::sqrt(left/std::max(1u,frames));right=std::sqrt(right/std::max(1u,frames));
      double energy=(left+right)*.5,now=seconds();
      if(energy>.012&&energy>baseline*2.5&&now-lastEvent>.35) {
        const double sharpness=std::sqrt(difference/(2*std::max(1u,frames)))/(energy+1e-8);
        std::lock_guard lock(mutex_);hint_={now,(right-left)/(right+left+1e-8),energy,energy>.035&&sharpness>.3};lastEvent=now;
      }
      baseline=baseline*.94+energy*.06;
    }
    active_=false;activation->client->Stop();if(process)CloseHandle(process);
  }
  if(SUCCEEDED(com))CoUninitialize();
}
}
