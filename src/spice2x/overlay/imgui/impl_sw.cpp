// Original File By Emil Ernerfeldt 2018
// https://github.com/emilk/imgui_software_renderer
// LICENSE:
//   This software is dual-licensed to the public domain and under the following
//   license: you are granted a perpetual, irrevocable license to copy, modify,
//   publish, and distribute this file as you see fit.
#include "impl_sw.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "external/imgui/imgui.h"

namespace imgui_sw {
namespace {

struct Texture
{
	const uint8_t* pixels; // 8-bit.
	int            width;
	int            height;
};

struct PaintTarget
{
	uint32_t* pixels;
	int       width;
	int       height;
	ImVec2    scale; // Multiply ImGui (point) coordinates with this to get pixel coordinates.
};

// ----------------------------------------------------------------------------

struct ColorInt
{
	uint32_t a, b, g, r;

	ColorInt() = default;

	explicit ColorInt(uint32_t x)
	{
		a = (x >> IM_COL32_A_SHIFT) & 0xFFu;
		b = (x >> IM_COL32_B_SHIFT) & 0xFFu;
		g = (x >> IM_COL32_G_SHIFT) & 0xFFu;
		r = (x >> IM_COL32_R_SHIFT) & 0xFFu;
	}

	uint32_t toUint32() const
	{
#ifdef IMGUI_USE_BGRA_PACKED_COLOR
        return (a << 24u) | (r << 16u) | (g << 8u) | b;
#else
        return (a << 24u) | (b << 16u) | (g << 8u) | r;
#endif
	}
};

ColorInt blend(ColorInt target, ColorInt source)
{
	ColorInt result;
	result.a = 0; // Whatever.
	result.b = (source.b * source.a + target.b * (255 - source.a)) / 255;
	result.g = (source.g * source.a + target.g * (255 - source.a)) / 255;
	result.r = (source.r * source.a + target.r * (255 - source.a)) / 255;
	return result;
}

// ----------------------------------------------------------------------------
// Used for interpolating vertex attributes (color and texture coordinates) in a triangle.

struct Barycentric
{
	float w0, w1, w2;
};

Barycentric operator*(const float f, const Barycentric& va)
{
	return { f * va.w0, f * va.w1, f * va.w2 };
}

void operator+=(Barycentric& a, const Barycentric& b)
{
	a.w0 += b.w0;
	a.w1 += b.w1;
	a.w2 += b.w2;
}

Barycentric operator+(const Barycentric& a, const Barycentric& b)
{
	return Barycentric{ a.w0 + b.w0, a.w1 + b.w1, a.w2 + b.w2 };
}

// ----------------------------------------------------------------------------
// Useful operators on ImGui vectors:

ImVec2 operator*(const float f, const ImVec2& v)
{
	return ImVec2{f * v.x, f * v.y};
}

ImVec2 operator+(const ImVec2& a, const ImVec2& b)
{
	return ImVec2{a.x + b.x, a.y + b.y};
}

ImVec2 operator-(const ImVec2& a, const ImVec2& b)
{
	return ImVec2{a.x - b.x, a.y - b.y};
}

bool operator!=(const ImVec2& a, const ImVec2& b)
{
	return a.x != b.x || a.y != b.y;
}

ImVec4 operator*(const float f, const ImVec4& v)
{
	return ImVec4{f * v.x, f * v.y, f * v.z, f * v.w};
}

ImVec4 operator+(const ImVec4& a, const ImVec4& b)
{
	return ImVec4{a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
}

// ----------------------------------------------------------------------------
// Copies of functions in ImGui, inlined for speed:

ImVec4 color_convert_u32_to_float4(ImU32 in)
{
	const float s = 1.0f / 255.0f;
	return ImVec4(
		((in >> IM_COL32_R_SHIFT) & 0xFF) * s,
		((in >> IM_COL32_G_SHIFT) & 0xFF) * s,
		((in >> IM_COL32_B_SHIFT) & 0xFF) * s,
		((in >> IM_COL32_A_SHIFT) & 0xFF) * s);
}

ImU32 color_convert_float4_to_u32(const ImVec4& in)
{
	ImU32 out;
    out  = uint32_t(in.x * 255.0f + 0.5f) << IM_COL32_R_SHIFT;
    out |= uint32_t(in.y * 255.0f + 0.5f) << IM_COL32_G_SHIFT;
    out |= uint32_t(in.z * 255.0f + 0.5f) << IM_COL32_B_SHIFT;
    out |= uint32_t(in.w * 255.0f + 0.5f) << IM_COL32_A_SHIFT;
	return out;
}

// ----------------------------------------------------------------------------
// For fast and subpixel-perfect triangle rendering we used fixed point arithmetic.
// To keep the code simple we use 64 bits to avoid overflows.

using Int = int64_t;
const Int kFixedBias = 256;

struct Point
{
	Int x, y;
};

Int orient2d(const Point& a, const Point& b, const Point& c)
{
	return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

Int as_int(float v)
{
	return static_cast<Int>(std::floor(v * kFixedBias));
}

Point as_point(ImVec2 v)
{
	return Point{as_int(v.x), as_int(v.y)};
}

// ----------------------------------------------------------------------------

float min3(float a, float b, float c)
{
	if (a < b && a < c) { return a; }
	return b < c ? b : c;
}

float max3(float a, float b, float c)
{
	if (a > b && a > c) { return a; }
	return b > c ? b : c;
}

float barycentric(const ImVec2& a, const ImVec2& b, const ImVec2& point)
{
	return (b.x - a.x) * (point.y - a.y) - (b.y - a.y) * (point.x - a.x);
}

inline uint8_t sample_texture(const Texture& texture, const ImVec2& uv)
{
	int tx = static_cast<int>(uv.x * (texture.width  - 1.0f) + 0.5f);
	int ty = static_cast<int>(uv.y * (texture.height - 1.0f) + 0.5f);

	// Clamp to inside of texture:
	tx = std::max(tx, 0);
	tx = std::min(tx, texture.width - 1);
	ty = std::max(ty, 0);
	ty = std::min(ty, texture.height - 1);

	return texture.pixels[ty * texture.width + tx];
}

void paint_uniform_rectangle(
	const PaintTarget& target,
	const ImVec2&      min_f,
	const ImVec2&      max_f,
	const ColorInt&    color,
	Stats*             stats)
{
	// Integer bounding box [min, max):
	int min_x_i = static_cast<int>(target.scale.x * min_f.x + 0.5f);
	int min_y_i = static_cast<int>(target.scale.y * min_f.y + 0.5f);
	int max_x_i = static_cast<int>(target.scale.x * max_f.x + 0.5f);
	int max_y_i = static_cast<int>(target.scale.y * max_f.y + 0.5f);

	// Clamp to render target:
	min_x_i = std::max(min_x_i, 0);
	min_y_i = std::max(min_y_i, 0);
	max_x_i = std::min(max_x_i, target.width);
	max_y_i = std::min(max_y_i, target.height);

	stats->uniform_rectangle_pixels += (max_x_i - min_x_i) * (max_y_i - min_y_i);

	// We often blend the same colors over and over again, so optimize for this (saves 25% total cpu):
	uint32_t last_target_pixel = target.pixels[min_y_i * target.width + min_x_i];
	uint32_t last_output = blend(ColorInt(last_target_pixel), color).toUint32();

	for (int y = min_y_i; y < max_y_i; ++y) {
		for (int x = min_x_i; x < max_x_i; ++x) {
			uint32_t& target_pixel = target.pixels[y * target.width + x];
			if (target_pixel == last_target_pixel) {
				target_pixel = last_output;
				continue;
			}
			last_target_pixel = target_pixel;
			target_pixel = blend(ColorInt(target_pixel), color).toUint32();
			last_output = target_pixel;
		}
	}
}

void paint_uniform_textured_rectangle(
	const PaintTarget& target,
	const Texture&     texture,
	const ImVec4&      clip_rect,
	const ImDrawVert&  min_v,
	const ImDrawVert&  max_v,
	Stats*             stats)
{
	const ImVec2 min_p = ImVec2(target.scale.x * min_v.pos.x, target.scale.y * min_v.pos.y);
	const ImVec2 max_p = ImVec2(target.scale.x * max_v.pos.x, target.scale.y * max_v.pos.y);

	// Find bounding box:
	float min_x_f = min_p.x;
	float min_y_f = min_p.y;
	float max_x_f = max_p.x;
	float max_y_f = max_p.y;

	// Clip against clip_rect:
	min_x_f = std::max(min_x_f, target.scale.x * clip_rect.x);
	min_y_f = std::max(min_y_f, target.scale.y * clip_rect.y);
	max_x_f = std::min(max_x_f, target.scale.x * clip_rect.z - 0.5f);
	max_y_f = std::min(max_y_f, target.scale.y * clip_rect.w - 0.5f);

	// Integer bounding box [min, max):
	int min_x_i = static_cast<int>(min_x_f);
	int min_y_i = static_cast<int>(min_y_f);
	int max_x_i = static_cast<int>(max_x_f + 1.0f);
	int max_y_i = static_cast<int>(max_y_f + 1.0f);

	// Clip against render target:
	min_x_i = std::max(min_x_i, 0);
	min_y_i = std::max(min_y_i, 0);
	max_x_i = std::min(max_x_i, target.width);
	max_y_i = std::min(max_y_i, target.height);

	stats->font_pixels += (max_x_i - min_x_i) * (max_y_i - min_y_i);

	const auto topleft = ImVec2(min_x_i + 0.5f * target.scale.x,
	                            min_y_i + 0.5f * target.scale.y);

	const ImVec2 delta_uv_per_pixel = {
		(max_v.uv.x - min_v.uv.x) / (max_p.x - min_p.x),
		(max_v.uv.y - min_v.uv.y) / (max_p.y - min_p.y),
	};
	const ImVec2 uv_topleft = {
		min_v.uv.x + (topleft.x - min_v.pos.x) * delta_uv_per_pixel.x,
		min_v.uv.y + (topleft.y - min_v.pos.y) * delta_uv_per_pixel.y,
	};
	ImVec2 current_uv = uv_topleft;

	for (int y = min_y_i; y < max_y_i; ++y, current_uv.y += delta_uv_per_pixel.y) {
		current_uv.x = uv_topleft.x;
		for (int x = min_x_i; x < max_x_i; ++x, current_uv.x += delta_uv_per_pixel.x) {
			uint32_t& target_pixel = target.pixels[y * target.width + x];
			const uint8_t texel = sample_texture(texture, current_uv);

			// The font texture is all black or all white, so optimize for this:
			if (texel == 0) { continue; }

			// Other textured rectangles
			ColorInt source_color = ColorInt(min_v.col);
			source_color.a = source_color.a * texel / 255;
			target_pixel = blend(ColorInt(target_pixel), source_color).toUint32();
		}
	}
}

// When two triangles share an edge, we want to draw the pixels on that edge exactly once.
// The edge will be the same, but the direction will be the opposite
// (assuming the two triangles have the same winding order).
// Which edge wins? This functions decides.
bool is_dominant_edge(ImVec2 edge)
{
	// return edge.x < 0 || (edge.x == 0 && edge.y > 0);
	return edge.y > 0 || (edge.y == 0 && edge.x < 0);
}

// Handles triangles in any winding order (CW/CCW)
void paint_triangle(
	const PaintTarget& target,
	const Texture*     texture,
	const ImVec4&      clip_rect,
	const ImDrawVert&  v0,
	const ImDrawVert&  v1,
	const ImDrawVert&  v2,
	Stats*             stats)
{
	const ImVec2 p0 = ImVec2(target.scale.x * v0.pos.x, target.scale.y * v0.pos.y);
	const ImVec2 p1 = ImVec2(target.scale.x * v1.pos.x, target.scale.y * v1.pos.y);
	const ImVec2 p2 = ImVec2(target.scale.x * v2.pos.x, target.scale.y * v2.pos.y);

	const auto rect_area = barycentric(p0, p1, p2); // Can be positive or negative depending on winding order
	if (rect_area == 0.0f) { return; }
	// if (rect_area < 0.0f) { return paint_triangle(target, texture, clip_rect, v0, v2, v1, stats); }

	// Find bounding box:
	float min_x_f = min3(p0.x, p1.x, p2.x);
	float min_y_f = min3(p0.y, p1.y, p2.y);
	float max_x_f = max3(p0.x, p1.x, p2.x);
	float max_y_f = max3(p0.y, p1.y, p2.y);

	// Clip against clip_rect:
	min_x_f = std::max(min_x_f, target.scale.x * clip_rect.x);
	min_y_f = std::max(min_y_f, target.scale.y * clip_rect.y);
	max_x_f = std::min(max_x_f, target.scale.x * clip_rect.z - 0.5f);
	max_y_f = std::min(max_y_f, target.scale.y * clip_rect.w - 0.5f);

	// Integer bounding box [min, max):
	int min_x_i = static_cast<int>(min_x_f);
	int min_y_i = static_cast<int>(min_y_f);
	int max_x_i = static_cast<int>(max_x_f + 1.0f);
	int max_y_i = static_cast<int>(max_y_f + 1.0f);

	// Clip against render target:
	min_x_i = std::max(min_x_i, 0);
	min_y_i = std::max(min_y_i, 0);
	max_x_i = std::min(max_x_i, target.width);
	max_y_i = std::min(max_y_i, target.height);

	// ------------------------------------------------------------------------
	// Set up interpolation of barycentric coordinates:

	const auto topleft = ImVec2(min_x_i + 0.5f * target.scale.x,
	                            min_y_i + 0.5f * target.scale.y);
	const auto dx = ImVec2(1, 0);
	const auto dy = ImVec2(0, 1);

	const auto w0_topleft = barycentric(p1, p2, topleft);
	const auto w1_topleft = barycentric(p2, p0, topleft);
	const auto w2_topleft = barycentric(p0, p1, topleft);

	const auto w0_dx = barycentric(p1, p2, topleft + dx) - w0_topleft;
	const auto w1_dx = barycentric(p2, p0, topleft + dx) - w1_topleft;
	const auto w2_dx = barycentric(p0, p1, topleft + dx) - w2_topleft;

	const auto w0_dy = barycentric(p1, p2, topleft + dy) - w0_topleft;
	const auto w1_dy = barycentric(p2, p0, topleft + dy) - w1_topleft;
	const auto w2_dy = barycentric(p0, p1, topleft + dy) - w2_topleft;

	const Barycentric bary_0 { 1, 0, 0 };
	const Barycentric bary_1 { 0, 1, 0 };
	const Barycentric bary_2 { 0, 0, 1 };

	const auto inv_area = 1 / rect_area;
	const Barycentric bary_topleft = inv_area * (w0_topleft * bary_0 + w1_topleft * bary_1 + w2_topleft * bary_2);
	const Barycentric bary_dx      = inv_area * (w0_dx      * bary_0 + w1_dx      * bary_1 + w2_dx      * bary_2);
	const Barycentric bary_dy      = inv_area * (w0_dy      * bary_0 + w1_dy      * bary_1 + w2_dy      * bary_2);

	Barycentric bary_current_row = bary_topleft;

	// ------------------------------------------------------------------------
	// For pixel-perfect inside/outside testing:

	const int sign = rect_area > 0 ? 1 : -1; // winding order?

	const int bias0i = is_dominant_edge(p2 - p1) ? 0 : -1;
	const int bias1i = is_dominant_edge(p0 - p2) ? 0 : -1;
	const int bias2i = is_dominant_edge(p1 - p0) ? 0 : -1;

	const auto p0i = as_point(p0);
	const auto p1i = as_point(p1);
	const auto p2i = as_point(p2);

	// ------------------------------------------------------------------------

	const bool has_uniform_color = (v0.col == v1.col && v0.col == v2.col);

	const ImVec4 c0 = color_convert_u32_to_float4(v0.col);
	const ImVec4 c1 = color_convert_u32_to_float4(v1.col);
	const ImVec4 c2 = color_convert_u32_to_float4(v2.col);

	// We often blend the same colors over and over again, so optimize for this (saves 10% total cpu):
	uint32_t last_target_pixel = 0;
	uint32_t last_output = blend(ColorInt(last_target_pixel), ColorInt(v0.col)).toUint32();

	for (int y = min_y_i; y < max_y_i; ++y) {
		auto bary = bary_current_row;

		bool has_been_inside_this_row = false;

		for (int x = min_x_i; x < max_x_i; ++x) {
			const auto w0 = bary.w0;
			const auto w1 = bary.w1;
			const auto w2 = bary.w2;
			bary += bary_dx;

			{
				// Inside/outside test:
				const auto p = Point{kFixedBias * x + kFixedBias / 2, kFixedBias * y + kFixedBias / 2};
				const auto w0i = sign * orient2d(p1i, p2i, p) + bias0i;
				const auto w1i = sign * orient2d(p2i, p0i, p) + bias1i;
				const auto w2i = sign * orient2d(p0i, p1i, p) + bias2i;
				if (w0i < 0 || w1i < 0 || w2i < 0) {
					if (has_been_inside_this_row) {
						break; // Gives a nice 10% speedup
					} else {
						continue;
					}
				}
			}
			has_been_inside_this_row = true;

			uint32_t& target_pixel = target.pixels[y * target.width + x];

			if (has_uniform_color && !texture) {
				stats->uniform_triangle_pixels += 1;
				if (target_pixel == last_target_pixel) {
					target_pixel = last_output;
					continue;
				}
				last_target_pixel = target_pixel;
				target_pixel = blend(ColorInt(target_pixel), ColorInt(v0.col)).toUint32();
				last_output = target_pixel;
				continue;
			}

			ImVec4 src_color;

			if (has_uniform_color) {
				src_color = c0;
			} else {
				stats->gradient_triangle_pixels += 1;
				src_color = w0 * c0 + w1 * c1 + w2 * c2;
			}

			if (texture) {
				stats->textured_triangle_pixels += 1;
				const ImVec2 uv = w0 * v0.uv + w1 * v1.uv + w2 * v2.uv;
				src_color.w *= sample_texture(*texture, uv) / 255.0f;
			}

			if (src_color.w <= 0.0f) { continue; } // Transparent.
			if (src_color.w >= 1.0f) {
				// Opaque, no blending needed:
				target_pixel = color_convert_float4_to_u32(src_color);
				continue;
			}

			ImVec4 target_color = color_convert_u32_to_float4(target_pixel);
			const auto blended_color = src_color.w * src_color + (1.0f - src_color.w) * target_color;
			target_pixel = color_convert_float4_to_u32(blended_color);
		}

		bary_current_row += bary_dy;
	}
}

void paint_draw_cmd(
	const PaintTarget& target,
	const ImDrawVert*  vertices,
	const ImDrawIdx*   idx_buffer,
	const ImDrawCmd&   pcmd,
	const SwOptions&   options,
	Stats*             stats)
{
	const auto texture = reinterpret_cast<const Texture*>(pcmd.TextureId);
    const auto offset = pcmd.IdxOffset;

	// ImGui uses the first pixel for "white".
	const ImVec2 white_uv = ImVec2(0.5f / texture->width, 0.5f / texture->height);

	for (size_t i = 0; i + 3 <= pcmd.ElemCount; ) {
        const auto io = i + offset;
		const ImDrawVert& v0 = vertices[idx_buffer[io + 0]];
		const ImDrawVert& v1 = vertices[idx_buffer[io + 1]];
		const ImDrawVert& v2 = vertices[idx_buffer[io + 2]];

		// Text is common, and is made of textured rectangles. So let's optimize for it.
		// This assumes the ImGui way to layout text does not change.
		if (options.optimize_text && i + 6 <= pcmd.ElemCount &&
		    idx_buffer[io + 3] == idx_buffer[io + 0] && idx_buffer[io + 4] == idx_buffer[io + 2]) {
			const ImDrawVert& v3 = vertices[idx_buffer[io + 5]];

			if (v0.pos.x == v3.pos.x &&
			    v1.pos.x == v2.pos.x &&
			    v0.pos.y == v1.pos.y &&
			    v2.pos.y == v3.pos.y &&
			    v0.uv.x == v3.uv.x &&
			    v1.uv.x == v2.uv.x &&
			    v0.uv.y == v1.uv.y &&
			    v2.uv.y == v3.uv.y)
			{
				const bool has_uniform_color =
					v0.col == v1.col &&
					v0.col == v2.col &&
					v0.col == v3.col;

				const bool has_texture =
					v0.uv != white_uv ||
					v1.uv != white_uv ||
					v2.uv != white_uv ||
					v3.uv != white_uv;

				if (has_uniform_color && has_texture)
				{
					paint_uniform_textured_rectangle(target, *texture, pcmd.ClipRect, v0, v2, stats);
					i += 6;
					continue;
				}
			}
		}

		// A lot of the big stuff are uniformly colored rectangles,
		// so we can save a lot of CPU by detecting them:
		if (options.optimize_rectangles && i + 6 <= pcmd.ElemCount) {
			const ImDrawVert& v3 = vertices[idx_buffer[io + 3]];
			const ImDrawVert& v4 = vertices[idx_buffer[io + 4]];
			const ImDrawVert& v5 = vertices[idx_buffer[io + 5]];

			ImVec2 min, max;
			min.x = min3(v0.pos.x, v1.pos.x, v2.pos.x);
			min.y = min3(v0.pos.y, v1.pos.y, v2.pos.y);
			max.x = max3(v0.pos.x, v1.pos.x, v2.pos.x);
			max.y = max3(v0.pos.y, v1.pos.y, v2.pos.y);

			// Not the prettiest way to do this, but it catches all cases
			// of a rectangle split into two triangles.
			// TODO: Stop it from also assuming duplicate triangles is one rectangle.
			if ((v0.pos.x == min.x || v0.pos.x == max.x) &&
				(v0.pos.y == min.y || v0.pos.y == max.y) &&
				(v1.pos.x == min.x || v1.pos.x == max.x) &&
				(v1.pos.y == min.y || v1.pos.y == max.y) &&
				(v2.pos.x == min.x || v2.pos.x == max.x) &&
				(v2.pos.y == min.y || v2.pos.y == max.y) &&
				(v3.pos.x == min.x || v3.pos.x == max.x) &&
				(v3.pos.y == min.y || v3.pos.y == max.y) &&
				(v4.pos.x == min.x || v4.pos.x == max.x) &&
				(v4.pos.y == min.y || v4.pos.y == max.y) &&
				(v5.pos.x == min.x || v5.pos.x == max.x) &&
				(v5.pos.y == min.y || v5.pos.y == max.y))
			{
				const bool has_uniform_color =
					v0.col == v1.col &&
					v0.col == v2.col &&
					v0.col == v3.col &&
					v0.col == v4.col &&
					v0.col == v5.col;

				const bool has_texture =
					v0.uv != white_uv ||
					v1.uv != white_uv ||
					v2.uv != white_uv ||
					v3.uv != white_uv ||
					v4.uv != white_uv ||
					v5.uv != white_uv;

				min.x = std::max(min.x, pcmd.ClipRect.x);
				min.y = std::max(min.y, pcmd.ClipRect.y);
				max.x = std::min(max.x, pcmd.ClipRect.z - 0.5f);
				max.y = std::min(max.y, pcmd.ClipRect.w - 0.5f);

				if (max.x < min.x || max.y < min.y) { i += 6; continue; } // Completely clipped

				const auto num_pixels = (max.x - min.x) * (max.y - min.y) * target.scale.x * target.scale.y;

				if (has_uniform_color) {
					if (has_texture) {
						stats->textured_rectangle_pixels += num_pixels;
					} else {
						paint_uniform_rectangle(target, min, max, ColorInt(v0.col), stats);
						i += 6;
						continue;
					}
				} else {
					if (has_texture) {
						// I have never encountered these.
						stats->gradient_textured_rectangle_pixels += num_pixels;
					} else {
						// Color picker. TODO: Optimize
						stats->gradient_rectangle_pixels += num_pixels;
					}
				}
			}
		}

		const bool has_texture = (v0.uv != white_uv || v1.uv != white_uv || v2.uv != white_uv);
		paint_triangle(target, has_texture ? texture : nullptr, pcmd.ClipRect, v0, v1, v2, stats);
		i += 3;
	}
}

void paint_draw_list(const PaintTarget& target, const ImDrawList* cmd_list, const SwOptions& options, Stats* stats)
{
	const ImDrawIdx* idx_buffer = &cmd_list->IdxBuffer[0];
	const ImDrawVert* vertices = cmd_list->VtxBuffer.Data;

	for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.size(); cmd_i++)
	{
		const ImDrawCmd& pcmd = cmd_list->CmdBuffer[cmd_i];
		if (pcmd.UserCallback) {
			pcmd.UserCallback(cmd_list, &pcmd);
		} else {
			paint_draw_cmd(target, vertices, idx_buffer, pcmd, options, stats);
		}
	}
}

} // namespace

void make_style_fast()
{
	ImGuiStyle& style = ImGui::GetStyle();

	style.AntiAliasedLines = false;
	style.AntiAliasedFill = false;
	style.WindowRounding = 0;
}

void restore_style()
{
	ImGuiStyle& style = ImGui::GetStyle();
	const ImGuiStyle default_style = ImGuiStyle();
	style.AntiAliasedLines = default_style.AntiAliasedLines;
	style.AntiAliasedFill = default_style.AntiAliasedFill;
	style.WindowRounding = default_style.WindowRounding;
}

void bind_imgui_painting()
{
    // make sure it doesn't get called twice
    static bool done = false;
    if (done) {
        return;
    }
    done = true;

	// Load default font (embedded in code):
    ImGuiIO& io = ImGui::GetIO();
	uint8_t* tex_data;
	int font_width, font_height;
	io.Fonts->GetTexDataAsAlpha8(&tex_data, &font_width, &font_height);
	const auto texture = new Texture{tex_data, font_width, font_height};
	io.Fonts->TexID = reinterpret_cast<ImTextureID>(texture);
}

static Stats s_stats; // TODO: pass as an argument?

void paint_imgui(uint32_t* pixels, int width_pixels, int height_pixels, const SwOptions& options)
{
	const float width_points = ImGui::GetIO().DisplaySize.x;
	const float height_points = ImGui::GetIO().DisplaySize.y;
	const ImVec2 scale{width_pixels / width_points, height_pixels / height_points};
	PaintTarget target{pixels, width_pixels, height_pixels, scale};
	const ImDrawData* draw_data = ImGui::GetDrawData();

	s_stats = Stats{};
	for (int i = 0; i < draw_data->CmdListsCount; ++i) {
		paint_draw_list(target, draw_data->CmdLists[i], options, &s_stats);
	}
}

void unbind_imgui_painting()
{
	ImGuiIO& io = ImGui::GetIO();
	delete reinterpret_cast<Texture*>(io.Fonts->TexID);
	io.Fonts = nullptr;
}

bool show_options(SwOptions* io_options)
{
	bool changed = false;
	changed |= ImGui::Checkbox("optimize_text", &io_options->optimize_text);
	changed |= ImGui::Checkbox("optimize_rectangles", &io_options->optimize_rectangles);
	return changed;
}

void show_stats()
{
	ImGui::Text("uniform_triangle_pixels:            %7d",   s_stats.uniform_triangle_pixels);
	ImGui::Text("textured_triangle_pixels:           %7d",   s_stats.textured_triangle_pixels);
	ImGui::Text("gradient_triangle_pixels:           %7d",   s_stats.gradient_triangle_pixels);
	ImGui::Text("font_pixels:                        %7d",   s_stats.font_pixels);
	ImGui::Text("uniform_rectangle_pixels:           %7.0f", s_stats.uniform_rectangle_pixels);
	ImGui::Text("textured_rectangle_pixels:          %7.0f", s_stats.textured_rectangle_pixels);
	ImGui::Text("gradient_rectangle_pixels:          %7.0f", s_stats.gradient_rectangle_pixels);
	ImGui::Text("gradient_textured_rectangle_pixels: %7.0f", s_stats.gradient_textured_rectangle_pixels);
}

Stats get_stats() {
    return s_stats;
}

} // namespace imgui_sw
