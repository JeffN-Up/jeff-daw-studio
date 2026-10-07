#include "ui/OrbaLayout.h"
#include <algorithm>
#include <cmath>
namespace jeff::daw {float distance(PointF a,PointF b)noexcept{return std::hypot(b.x-a.x,b.y-a.y);}OrbaLayout calculateOrbaLayout(RectangleF b)noexcept{OrbaLayout l;auto d=std::max(0.f,std::min(b.width,b.height));l.centre={b.x+b.width/2,b.y+b.height/2};l.bodyRadius=d*.45f;l.innerRadius=l.bodyRadius*.3f;l.outerRadius=l.bodyRadius*.88f;auto orbit=(l.innerRadius+l.outerRadius)/2,hit=(l.outerRadius-l.innerRadius)*.38f;constexpr float pi=3.14159265359f,slice=2*pi/8;for(int i=0;i<8;++i){auto a=-pi/2+slice*i;l.pads[i]={{l.centre.x+std::cos(a)*orbit,l.centre.y+std::sin(a)*orbit},hit,a-slice*.45f,a+slice*.45f};}return l;}}
