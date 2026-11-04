#include <windows.h>
#include "shared_manifest.h"
#include <iostream>
int wmain(int argc,wchar_t** argv) {
 try {
  if(argc==6&&std::wstring(argv[1])==L"--sync-idle") {shared_manifest::sync(argv[2],argv[3],argv[4],argv[5]);return 0;}
  std::string sample=R"("AppState" { "appid" "730" "installdir" "cs2" "StateFlags" "4" "UpdateResult" "0" "StagingSize" "0" "buildid" "123" "LastOwner" "111" "LastPlayed" "10" "AutoUpdateBehavior" "0" "AllowOtherDownloadsWhileRunning" "0" "UserConfig" { "language" "russian" } "MountedConfig" { "language" "russian" } })";
  auto target=std::regex_replace(sample,std::regex("111"),"222");
  auto result=shared_manifest::prepare(sample,target);
  if(shared_manifest::field(result,"LastOwner")!="222"||shared_manifest::field(result,"buildid")!="123") throw std::runtime_error("Identity preservation failed");
  int rejected=0;
  for(auto bad:{std::regex_replace(sample,std::regex("\"4\""),"\"1702\""),std::regex_replace(sample,std::regex("\"730\""),"\"740\""),sample+" \"buildid\" \"999\"",std::regex_replace(sample,std::regex("russian"),"english")}) {
   try {shared_manifest::prepare(bad,target);}catch(const std::exception&){++rejected;}
  }
  if(rejected!=4) throw std::runtime_error("Unsafe manifest accepted");
  auto root=std::filesystem::temp_directory_path()/(L"cs2-manifest-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
  std::filesystem::create_directories(root/L"steamapps"/L"common"/L"cs2");
  auto source=root/L"steamapps"/L"appmanifest_730.acf", destination=root/L"box"/L"appmanifest_730.acf";
  std::filesystem::create_directories(destination.parent_path());
  {std::ofstream(source)<<sample; std::ofstream(destination)<<std::regex_replace(target,std::regex("\"123\""),"\"122\"");}
  shared_manifest::sync(root/L"steam.exe",root/L"steam.exe",root/L"steamapps"/L"common"/L"cs2",destination);
  if(shared_manifest::field(shared_manifest::read(destination),"LastOwner")!="222") throw std::runtime_error("File sync changed owner");
  auto stamp=std::filesystem::last_write_time(destination);
  shared_manifest::sync(root/L"steam.exe",root/L"steam.exe",root/L"steamapps"/L"common"/L"cs2",destination);
  if(stamp!=std::filesystem::last_write_time(destination)) throw std::runtime_error("Repeated sync rewrote current manifest");
  std::filesystem::remove_all(root);
  std::cout<<"7 manifest regression cases passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
