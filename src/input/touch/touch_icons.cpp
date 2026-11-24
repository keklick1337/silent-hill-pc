// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/touch/touch_icons.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace sh::input::touch {
namespace {

std::optional<std::string_view> attribute(std::string_view tag, std::string_view name) {
    std::size_t at = 0;
    while ((at = tag.find(name, at)) != std::string_view::npos) {
        const bool starts = at > 0 && std::isspace(static_cast<unsigned char>(tag[at - 1]));
        std::size_t p = at + name.size();
        while (p < tag.size() && std::isspace(static_cast<unsigned char>(tag[p]))) ++p;
        if (starts && p < tag.size() && tag[p] == '=') {
            ++p;
            while (p < tag.size() && std::isspace(static_cast<unsigned char>(tag[p]))) ++p;
            if (p < tag.size() && (tag[p] == '"' || tag[p] == '\'')) {
                const char quote = tag[p];
                const std::size_t end = tag.find(quote, p + 1);
                if (end == std::string_view::npos) return std::nullopt;
                return tag.substr(p + 1, end - p - 1);
            }
        }
        at += name.size();
    }
    return std::nullopt;
}

// Numbers of an attribute such as points or viewBox: separated by spaces or commas.
std::vector<float> numbers(std::string_view text) {
    std::vector<float> out;
    std::string copy(text);
    const char* p = copy.c_str();
    while (*p != '\0') {
        while (*p != '\0' && (std::isspace(static_cast<unsigned char>(*p)) || *p == ',')) ++p;
        if (*p == '\0') break;
        char* end = nullptr;
        const float v = std::strtof(p, &end);
        if (end == p) break;
        out.push_back(v);
        p = end;
    }
    return out;
}

float number_attribute(std::string_view tag, std::string_view name, float fallback) {
    const std::optional<std::string_view> v = attribute(tag, name);
    if (!v) return fallback;
    const std::vector<float> n = numbers(*v);
    return n.empty() ? fallback : n[0];
}

bool painted(std::string_view tag, std::string_view name, bool by_default) {
    const std::optional<std::string_view> v = attribute(tag, name);
    return v ? *v != "none" : by_default;
}

float cross(Point o, Point a, Point b) { return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x); }

bool inside_triangle(Point p, Point a, Point b, Point c) {
    const float d1 = cross(a, b, p), d2 = cross(b, c, p), d3 = cross(c, a, p);
    const bool neg = d1 < 0.0f || d2 < 0.0f || d3 < 0.0f;
    const bool pos = d1 > 0.0f || d2 > 0.0f || d3 > 0.0f;
    return !(neg && pos);
}

struct Shape {
    std::vector<Point> points;
    bool closed = false;
};

// The subpaths of a path's d (straight commands only); an unknown command ends the path.
Result<std::vector<Shape>> path_shapes(std::string_view d) {
    std::vector<Shape> shapes;
    Point at{}, start{};
    char command = 0;
    std::size_t i = 0;
    const auto skip = [&]() {
        while (i < d.size() && (std::isspace(static_cast<unsigned char>(d[i])) || d[i] == ',')) ++i;
    };
    const auto number = [&](float& out) {
        skip();
        std::string rest(d.substr(i, 32));
        char* end = nullptr;
        out = std::strtof(rest.c_str(), &end);
        if (end == rest.c_str()) return false;
        i += static_cast<std::size_t>(end - rest.c_str());
        return true;
    };
    while (true) {
        skip();
        if (i >= d.size()) break;
        if (std::isalpha(static_cast<unsigned char>(d[i]))) command = d[i++];
        const bool rel = std::islower(static_cast<unsigned char>(command)) != 0;
        float x = 0.0f, y = 0.0f;
        switch (std::toupper(static_cast<unsigned char>(command))) {
            case 'M':
                if (!number(x) || !number(y)) return Result<std::vector<Shape>>::fail("a move needs x and y");
                at = rel ? Point{at.x + x, at.y + y} : Point{x, y};
                start = at;
                shapes.push_back({{at}, false});
                command = rel ? 'l' : 'L';  // further pairs are lines
                break;
            case 'L':
                if (!number(x) || !number(y)) return Result<std::vector<Shape>>::fail("a line needs x and y");
                at = rel ? Point{at.x + x, at.y + y} : Point{x, y};
                if (shapes.empty()) shapes.push_back({{start}, false});
                shapes.back().points.push_back(at);
                break;
            case 'H':
                if (!number(x)) return Result<std::vector<Shape>>::fail("H needs x");
                at.x = rel ? at.x + x : x;
                if (shapes.empty()) shapes.push_back({{start}, false});
                shapes.back().points.push_back(at);
                break;
            case 'V':
                if (!number(y)) return Result<std::vector<Shape>>::fail("V needs y");
                at.y = rel ? at.y + y : y;
                if (shapes.empty()) shapes.push_back({{start}, false});
                shapes.back().points.push_back(at);
                break;
            case 'Z':
                if (!shapes.empty()) shapes.back().closed = true;
                at = start;
                command = 0;
                break;
            default:
                return Result<std::vector<Shape>>::fail(std::string("path command '") + command +
                                                        "' is not in the icons' subset (M L H V Z)");
        }
    }
    return Result<std::vector<Shape>>::ok(std::move(shapes));
}

void add_shape(VectorIcon& icon, const Shape& shape, std::string_view tag, bool fill_default) {
    if (shape.points.size() < 2) return;
    if (painted(tag, "fill", fill_default) && shape.points.size() >= 3) {
        const auto tris = triangulate(shape.points);
        icon.triangles.insert(icon.triangles.end(), tris.begin(), tris.end());
    }
    if (painted(tag, "stroke", false)) {
        icon.strokes.push_back({shape.points, shape.closed, number_attribute(tag, "stroke-width", 1.0f)});
    }
}

std::vector<Point> point_list(std::string_view tag) {
    std::vector<Point> out;
    const std::optional<std::string_view> v = attribute(tag, "points");
    if (!v) return out;
    const std::vector<float> n = numbers(*v);
    for (std::size_t i = 0; i + 1 < n.size(); i += 2) out.push_back({n[i], n[i + 1]});
    return out;
}

}  // namespace

std::vector<std::array<Point, 3>> triangulate(const std::vector<Point>& polygon) {
    std::vector<std::array<Point, 3>> out;
    std::vector<Point> p = polygon;
    if (p.size() >= 2 && p.front().x == p.back().x && p.front().y == p.back().y) p.pop_back();
    if (p.size() < 3) return out;
    float area = 0.0f;
    for (std::size_t i = 0; i < p.size(); ++i) area += cross({0, 0}, p[i], p[(i + 1) % p.size()]);
    if (area < 0.0f) std::reverse(p.begin(), p.end());
    // Ear clipping: a convex corner with no other point inside its triangle is cut off.
    std::size_t guard = p.size() * p.size() + 8;
    while (p.size() > 3 && guard-- > 0) {
        bool cut = false;
        for (std::size_t i = 0; i < p.size(); ++i) {
            const Point a = p[(i + p.size() - 1) % p.size()], b = p[i], c = p[(i + 1) % p.size()];
            if (cross(a, b, c) <= 0.0f) continue;
            bool empty = true;
            for (std::size_t j = 0; j < p.size() && empty; ++j) {
                const Point q = p[j];
                if ((q.x == a.x && q.y == a.y) || (q.x == b.x && q.y == b.y) || (q.x == c.x && q.y == c.y)) continue;
                empty = !inside_triangle(q, a, b, c);
            }
            if (!empty) continue;
            out.push_back({a, b, c});
            p.erase(p.begin() + static_cast<std::ptrdiff_t>(i));
            cut = true;
            break;
        }
        if (!cut) break;  // not a simple polygon: what is left is dropped
    }
    if (p.size() == 3) out.push_back({p[0], p[1], p[2]});
    return out;
}

Result<VectorIcon> parse_svg_icon(std::string_view svg) {
    VectorIcon icon;
    bool seen_svg = false;
    std::size_t at = 0;
    while ((at = svg.find('<', at)) != std::string_view::npos) {
        if (svg.substr(at, 4) == "<!--") {
            const std::size_t end = svg.find("-->", at);
            if (end == std::string_view::npos) break;
            at = end + 3;
            continue;
        }
        const std::size_t end = svg.find('>', at);
        if (end == std::string_view::npos) return Result<VectorIcon>::fail("a tag is not closed");
        std::string_view tag = svg.substr(at + 1, end - at - 1);
        at = end + 1;
        if (tag.empty() || tag[0] == '/' || tag[0] == '?' || tag[0] == '!') continue;
        std::size_t name_end = 0;
        while (name_end < tag.size() && !std::isspace(static_cast<unsigned char>(tag[name_end])) &&
               tag[name_end] != '/')
            ++name_end;
        const std::string_view name = tag.substr(0, name_end);
        if (name == "svg") {
            seen_svg = true;
            if (const std::optional<std::string_view> box = attribute(tag, "viewBox")) {
                const std::vector<float> n = numbers(*box);
                if (n.size() == 4 && n[2] > 0.0f && n[3] > 0.0f) {
                    icon.box_x = n[0];
                    icon.box_y = n[1];
                    icon.box_size = std::max(n[2], n[3]);
                }
            }
        } else if (name == "path") {
            const std::optional<std::string_view> d = attribute(tag, "d");
            if (!d) continue;
            Result<std::vector<Shape>> shapes = path_shapes(*d);
            if (!shapes) return Result<VectorIcon>::fail(shapes.error());
            for (const Shape& shape : shapes.value()) add_shape(icon, shape, tag, true);
        } else if (name == "line") {
            const Shape shape{{{number_attribute(tag, "x1", 0), number_attribute(tag, "y1", 0)},
                               {number_attribute(tag, "x2", 0), number_attribute(tag, "y2", 0)}},
                              false};
            add_shape(icon, shape, tag, false);
        } else if (name == "polyline" || name == "polygon") {
            add_shape(icon, {point_list(tag), name == "polygon"}, tag, true);
        } else if (name == "rect") {
            const float x = number_attribute(tag, "x", 0), y = number_attribute(tag, "y", 0);
            const float w = number_attribute(tag, "width", 0), h = number_attribute(tag, "height", 0);
            add_shape(icon, {{{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}, true}, tag, true);
        } else if (name == "circle") {
            const Point c{number_attribute(tag, "cx", 0), number_attribute(tag, "cy", 0)};
            const float r = number_attribute(tag, "r", 0);
            if (painted(tag, "fill", true)) icon.rings.push_back({c, r, 0.0f});
            if (painted(tag, "stroke", false)) icon.rings.push_back({c, r, number_attribute(tag, "stroke-width", 1.0f)});
        }
    }
    if (!seen_svg) return Result<VectorIcon>::fail("no <svg> element");
    return Result<VectorIcon>::ok(std::move(icon));
}

}  // namespace sh::input::touch
