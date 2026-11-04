#pragma once
#include "vision/image.h"
#include <cmath>
namespace cs2n::session {
inline bool matchesPracticePatch(const Image& frame,const Image& ref,int x,int y){
 if(ref.empty()||x<0||y<0||ref.width()>frame.width()||ref.height()>frame.height()||x>frame.width()-ref.width()||y>frame.height()-ref.height())return false;
 double sum=0;unsigned bad=0;
 for(int j=0;j<ref.height();++j)for(int i=0;i<ref.width();++i){auto a=frame.at(x+i,y+j),b=ref.at(i,j);
  int delta=std::abs(int(a.r)-b.r)+std::abs(int(a.g)-b.g)+std::abs(int(a.b)-b.b);sum+=delta;if(delta>75)++bad;}
 return sum/(ref.pixelCount()*3)<9&&double(bad)/ref.pixelCount()<.08;
}
}
