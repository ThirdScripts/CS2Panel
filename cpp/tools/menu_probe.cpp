// Read-only foreground menu diagnostic. No input, activation or game commands.
#include <fstream>
#include <iostream>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
#include "vision/capture.h"
int main(int argc,char** argv){
 if(argc!=2 && argc!=3)return 2;
 try{
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  int w=0,h=0;std::string error;cs2n::Image frame;
  if(argc==3){if(!cs2n::Image::loadPnm(argv[2],frame,error)){std::cerr<<error;return 4;}w=frame.width();h=frame.height();}
  else {
   if(!cs2n::RadarCapture::cs2ClientSize(w,h)){std::cerr<<"CS2 must be foreground\n";return 3;}
   cs2n::RadarCapture capture;auto image=capture.grab({0,0,w,h},error);
   if(!image){std::cerr<<error;return 4;}frame=std::move(*image);
   if(!cs2n::Image::savePpm(frame,std::string(argv[1])+".ppm",error))return 5;
  }
  // Upscale small menu fonts before OCR; report boxes in original client pixels.
  const int scale=w<1000?3:1;
  if(w*scale>int(winrt::Windows::Media::Ocr::OcrEngine::MaxImageDimension()) || h*scale>int(winrt::Windows::Media::Ocr::OcrEngine::MaxImageDimension()))return 7;
  std::vector<unsigned char> pixels;pixels.reserve(size_t(w)*h*scale*scale*4);
  for(int y=0;y<h*scale;++y)for(int x=0;x<w*scale;++x){const auto& p=frame.at(x/scale,y/scale);pixels.push_back(p.b);pixels.push_back(p.g);pixels.push_back(p.r);pixels.push_back(255);}
  winrt::Windows::Storage::Streams::DataWriter writer;writer.WriteBytes(pixels);
  auto bitmap=winrt::Windows::Graphics::Imaging::SoftwareBitmap::CreateCopyFromBuffer(writer.DetachBuffer(),winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,w*scale,h*scale,winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Ignore);
  std::ofstream out(std::string(argv[1])+".txt");
  for(const auto* lang:{L"ru",L"en-US"}){
   auto ocr=winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromLanguage(winrt::Windows::Globalization::Language(lang));
   if(!ocr){out<<"OCR unavailable\n";continue;}
   auto result=ocr.RecognizeAsync(bitmap).get();out<<"LANG "<<winrt::to_string(lang)<<"\n";
   for(auto line:result.Lines())for(auto word:line.Words()){
    auto b=word.BoundingRect();out<<b.X/scale<<","<<b.Y/scale<<","<<b.Width/scale<<","<<b.Height/scale<<" "<<winrt::to_string(word.Text())<<"\n";
   }
  }
  std::cout<<"Captured "<<w<<"x"<<h<<"; OCR saved locally\n";return 0;
 }catch(const winrt::hresult_error& e){std::cerr<<winrt::to_string(e.message());return 6;}
}
