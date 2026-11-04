#pragma once
#include <Windows.h>
namespace cs2n {
inline COLORREF themeColor(COLORREF c,int theme) {
 if(theme==0)return c;
 const int r=GetRValue(c),g=GetGValue(c),b=GetBValue(c),v=(r+g+b)/3;
 if(theme==1){int l=(r*21+g*72+b*7)/100;return RGB(l,l,l);}
 const bool kitty=theme==2;
 if(r<10 && g<30 && b<45)return kitty?RGB(255,255,255):RGB(13,18,15);
 if(b-r>65 && g>160)return kitty?RGB(181,36,98):RGB(166,219,132);
 if(v>170)return kitty?RGB(69,40,59):RGB(225,231,220);
 if(v>75)return kitty?RGB(130,85,108):RGB(160,173,152);
 if(v>45)return kitty?RGB(233,173,198):RGB(55,71,53);
 if(v>25)return kitty?RGB(255,230,239):RGB(25,32,25);
 return kitty?RGB(255,247,250):RGB(13,18,15);
}
}
