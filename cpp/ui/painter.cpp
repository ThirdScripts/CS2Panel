#include "ui/painter.h"
#include "ui/theme.h"
#include <algorithm>
#include <cmath>
namespace cs2n {
void Painter::set(const DrawModel& m) {std::lock_guard<std::mutex> lock(mutex_);model_=m;}
DrawModel Painter::get() const {std::lock_guard<std::mutex> lock(mutex_);return model_;}
void Painter::setTransform(double s,double x,double y) const {scale_=s;offsetX_=x;offsetY_=y;}
void Painter::paint(HDC dc,int width,int height,int theme) const {
  auto themed=[theme](COLORREF c){return themeColor(c,theme);};
  auto m=get();RECT rect{0,0,width,height};
  auto bg=CreateSolidBrush(themed(RGB(12,19,30)));FillRect(dc,&rect,bg);DeleteObject(bg);
  if(!m.mesh || !m.mesh->valid()) {
    SetBkMode(dc,TRANSPARENT);SetTextColor(dc,themed(RGB(111,143,168)));
    DrawTextW(dc,L"Маршрут появится после запуска",-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    return;
  }
  Vec2 low=m.mesh->boundsMin(),high=m.mesh->boundsMax();
  double fit=std::min((width-32)/(high.x-low.x),(height-32)/(high.y-low.y));
  auto point=[&](Vec2 p){return POINT{static_cast<LONG>(16+(p.x-low.x)*fit),static_cast<LONG>(16+(high.y-p.y)*fit)};};
  HPEN edge=CreatePen(PS_SOLID,1,themed(RGB(47,62,80)));HBRUSH fill=CreateSolidBrush(themed(RGB(29,42,57)));
  auto oldPen=SelectObject(dc,edge);auto oldBrush=SelectObject(dc,fill);
  for(const auto& area:m.mesh->areas()) {
    std::vector<POINT> pts;for(auto p:area.polygon) pts.push_back(point(p));
    Polygon(dc,pts.data(),static_cast<int>(pts.size()));
  }
  SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(edge);DeleteObject(fill);
  auto route=CreatePen(PS_SOLID,2,themed(RGB(68,194,255)));oldPen=SelectObject(dc,route);
  for(std::size_t i=0;i<m.path.size();++i) {auto p=point(m.path[i].position);if(i==0)MoveToEx(dc,p.x,p.y,nullptr);else LineTo(dc,p.x,p.y);}
  SelectObject(dc,oldPen);DeleteObject(route);
  if(m.havePose) {
    auto p=point(m.position);auto view=point(m.position+Vec2{std::cos(m.yaw),std::sin(m.yaw)}*160);
    auto player=CreatePen(PS_SOLID,3,themed(RGB(98,235,152)));oldPen=SelectObject(dc,player);
    oldBrush=SelectObject(dc,GetStockObject(HOLLOW_BRUSH));
    Ellipse(dc,p.x-5,p.y-5,p.x+5,p.y+5);MoveToEx(dc,p.x,p.y,nullptr);LineTo(dc,view.x,view.y);
    SelectObject(dc,oldBrush);SelectObject(dc,oldPen);DeleteObject(player);
  }
  setTransform(fit,16-low.x*fit,16+high.y*fit);
}
}
