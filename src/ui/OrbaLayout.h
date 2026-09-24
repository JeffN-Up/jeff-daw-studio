#pragma once
#include <array>
namespace jeff::daw {struct PointF{float x{},y{};};struct RectangleF{float x{},y{},width{},height{};};struct OrbaPadLayout{PointF centre;float hitRadius{},startAngle{},endAngle{};};struct OrbaLayout{PointF centre;float bodyRadius{},innerRadius{},outerRadius{};std::array<OrbaPadLayout,8>pads{};};float distance(PointF,PointF)noexcept;OrbaLayout calculateOrbaLayout(RectangleF)noexcept;}
