//
// Implementation for Yocto/Image.
//

//
// LICENSE:
//
// Copyright (c) 2016 -- 2018 Fabio Pellacini
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#include "yocto_image.h"
#include "yocto_utils.h"

#if YGL_IMAGEIO

#ifndef _WIN32
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#ifndef __clang__
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#endif

#ifndef __clang_analyzer__

#define STB_IMAGE_IMPLEMENTATION
#include "ext/stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ext/stb_image_write.h"

#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "ext/stb_image_resize.h"

#define TINYEXR_IMPLEMENTATION
#include "ext/tinyexr.h"

#endif

#ifndef _WIN32
#pragma GCC diagnostic pop
#endif

#endif

// -----------------------------------------------------------------------------
// IMPLEMENTATION FOR IMAGEIO
// -----------------------------------------------------------------------------
namespace ygl {

// Pfm load
float* load_pfm(const char* filename, int* w, int* h, int* nc, int req) {
    auto split = [](const std::string& str) {
        auto ret = std::vector<std::string>();
        if (str.empty()) return ret;
        auto lpos = (size_t)0;
        while (lpos != str.npos) {
            auto pos = str.find_first_of(" \t\n\r", lpos);
            if (pos != str.npos) {
                if (pos > lpos) ret.push_back(str.substr(lpos, pos - lpos));
                lpos = pos + 1;
            } else {
                if (lpos < str.size()) ret.push_back(str.substr(lpos));
                lpos = pos;
            }
        }
        return ret;
    };

    auto f = fopen(filename, "rb");
    if (!f) return nullptr;

    // buffer
    char buf[256];
    auto toks = std::vector<std::string>();

    // read magic
    if (!fgets(buf, 256, f)) return nullptr;
    toks = split(buf);
    if (toks[0] == "Pf")
        *nc = 1;
    else if (toks[0] == "PF")
        *nc = 3;
    else
        return nullptr;

    // read w, h
    if (!fgets(buf, 256, f)) return nullptr;
    toks = split(buf);
    *w = atoi(toks[0].c_str());
    *h = atoi(toks[1].c_str());

    // read scale
    if (!fgets(buf, 256, f)) return nullptr;
    toks = split(buf);
    auto s = atof(toks[0].c_str());

    // read the data (flip y)
    auto npixels = (*w) * (*h);
    auto nvalues = (*w) * (*h) * (*nc);
    auto nrow = (*w) * (*nc);
    auto pixels = std::unique_ptr<float[]>(new float[nvalues]);
    for (auto j = *h - 1; j >= 0; j--) {
        if (fread(pixels.get() + j * nrow, sizeof(float), nrow, f) != nrow)
            return nullptr;
    }

    // done reading
    fclose(f);

    // endian conversion
    if (s > 0) {
        for (auto i = 0; i < nvalues; ++i) {
            auto dta = (uint8_t*)(pixels.get() + i);
            std::swap(dta[0], dta[3]);
            std::swap(dta[1], dta[2]);
        }
    }

    // scale
    auto scl = (s > 0) ? s : -s;
    if (scl != 1) {
        for (auto i = 0; i < nvalues; i++) pixels[i] *= scl;
    }

    // proper number of channels
    if (!req || *nc == req) return pixels.release();

    // pack into channels
    if (req < 0 || req > 4) return nullptr;
    auto cpixels = new float[req * npixels];
    for (auto i = 0; i < npixels; i++) {
        auto vp = pixels.get() + i * (*nc);
        auto cp = cpixels + i * req;
        if (*nc == 1) {
            switch (req) {
                case 1: cp[0] = vp[0]; break;
                case 2:
                    cp[0] = vp[0];
                    cp[1] = vp[0];
                    break;
                case 3:
                    cp[0] = vp[0];
                    cp[1] = vp[0];
                    cp[2] = vp[0];
                    break;
                case 4:
                    cp[0] = vp[0];
                    cp[1] = vp[0];
                    cp[2] = vp[0];
                    cp[3] = 1;
                    break;
            }
        } else {
            switch (req) {
                case 1: cp[0] = vp[0]; break;
                case 2:
                    cp[0] = vp[0];
                    cp[1] = vp[1];
                    break;
                case 3:
                    cp[0] = vp[0];
                    cp[1] = vp[1];
                    cp[2] = vp[2];
                    break;
                case 4:
                    cp[0] = vp[0];
                    cp[1] = vp[1];
                    cp[2] = vp[2];
                    cp[3] = 1;
                    break;
            }
        }
    }
    return cpixels;
}

// save pfm
bool save_pfm(const char* filename, int w, int h, int nc, const float* pixels) {
    auto f = fopen(filename, "wb");
    if (!f) return false;

    fprintf(f, "%s\n", (nc == 1) ? "Pf" : "PF");
    fprintf(f, "%d %d\n", w, h);
    fprintf(f, "-1\n");
    if (nc == 1 || nc == 3) {
        fwrite(pixels, sizeof(float), w * h * nc, f);
    } else {
        for (auto i = 0; i < w * h; i++) {
            auto vz = 0.0f;
            auto v = pixels + i * nc;
            fwrite(v + 0, sizeof(float), 1, f);
            fwrite(v + 1, sizeof(float), 1, f);
            if (nc == 2)
                fwrite(&vz, sizeof(float), 1, f);
            else
                fwrite(v + 2, sizeof(float), 1, f);
        }
    }

    fclose(f);

    return true;
}

// check hdr extensions
bool is_hdr_filename(const std::string& filename) {
    auto ext = path_extension(filename);
    return ext == ".hdr" || ext == ".exr" || ext == ".pfm";
}

// Loads an ldr image.
image4b load_image4b(const std::string& filename) {
    auto w = 0, h = 0, c = 0;
    auto pixels =
        std::unique_ptr<byte>(stbi_load(filename.c_str(), &w, &h, &c, 4));
    if (!pixels) return {};
    return make_image4b(w, h, (vec4b*)pixels.get());
}

// Loads an hdr image.
image4f load_image4f(const std::string& filename) {
    auto ext = path_extension(filename);
    auto w = 0, h = 0, c = 0;
    auto pixels = std::unique_ptr<float>(nullptr);
    if (ext == ".exr") {
        auto pixels_ = (float*)nullptr;
        if (LoadEXR(&pixels_, &w, &h, filename.c_str(), nullptr) < 0) return {};
        pixels = std::unique_ptr<float>(pixels_);
        c = 4;
    } else if (ext == ".pfm") {
        pixels =
            std::unique_ptr<float>(load_pfm(filename.c_str(), &w, &h, &c, 4));
    } else {
        pixels =
            std::unique_ptr<float>(stbi_loadf(filename.c_str(), &w, &h, &c, 4));
    }
    if (!pixels) return {};
    return make_image4f(w, h, (vec4f*)pixels.get());
}

// Saves an ldr image.
bool save_image4b(const std::string& filename, const image4b& img) {
    if (path_extension(filename) == ".png") {
        return stbi_write_png(filename.c_str(), img.width, img.height, 4,
            (byte*)img.pixels.data(), img.width * 4);
    } else if (path_extension(filename) == ".jpg") {
        return stbi_write_jpg(filename.c_str(), img.width, img.height, 4,
            (byte*)img.pixels.data(), 75);
    } else {
        return false;
    }
}

// Saves an hdr image.
bool save_image4f(const std::string& filename, const image4f& img) {
    if (path_extension(filename) == ".hdr") {
        return stbi_write_hdr(filename.c_str(), img.width, img.height, 4,
                              (float*)img.pixels.data());
    } else if (path_extension(filename) == ".pfm") {
        return save_pfm(filename.c_str(), img.width, img.height, 4,
            (float*)img.pixels.data());
    } else if (path_extension(filename) == ".exr") {
        return !SaveEXR((float*)img.pixels.data(), img.width, img.height, 4,
            filename.c_str());
    } else {
        return false;
    }
}

// Loads an image
std::vector<float> load_imagef(
    const std::string& filename, int& width, int& height, int& ncomp) {
    auto ext = path_extension(filename);
    auto pixels = (float*)nullptr;
    if (ext == ".exr") {
        if (LoadEXR(&pixels, &width, &height, filename.c_str(), nullptr) < 0)
            return {};
        ncomp = 4;
    } else if (ext == ".pfm") {
        pixels = load_pfm(filename.c_str(), &width, &height, &ncomp, 0);
    } else {
        pixels = stbi_loadf(filename.c_str(), &width, &height, &ncomp, 0);
    }
    if (!pixels) return {};
    auto ret = std::vector<float>(pixels, pixels + width * height * ncomp);
    free(pixels);
    return ret;
}

// Loads an image
std::vector<byte> load_imageb(
    const std::string& filename, int& width, int& height, int& ncomp) {
    auto pixels = stbi_load(filename.c_str(), &width, &height, &ncomp, 0);
    if (!pixels) return {};
    auto ret = std::vector<byte>(pixels, pixels + width * height * ncomp);
    free(pixels);
    return ret;
}

// Loads an image from memory.
std::vector<float> load_imagef_from_memory(const std::string& filename,
    const byte* data, int length, int& width, int& height, int& ncomp) {
    auto pixels =
        stbi_loadf_from_memory(data, length, &width, &height, &ncomp, 0);
    if (!pixels) return {};
    auto ret = std::vector<float>(pixels, pixels + width * height * ncomp);
    free(pixels);
    return ret;
}

// Loads an image from memory.
std::vector<byte> load_imageb_from_memory(const std::string& filename,
    const byte* data, int length, int& width, int& height, int& ncomp) {
    auto pixels =
        stbi_load_from_memory(data, length, &width, &height, &ncomp, 0);
    if (!pixels) return {};
    auto ret = std::vector<byte>(pixels, pixels + width * height * ncomp);
    free(pixels);
    return ret;
}

// Saves an image
bool save_imagef(const std::string& filename, int width, int height, int ncomp,
    const float* hdr) {
    if (path_extension(filename) == ".hdr") {
        return stbi_write_hdr(filename.c_str(), width, height, ncomp, hdr);
    } else if (path_extension(filename) == ".pfm") {
        return save_pfm(filename.c_str(), width, height, ncomp, hdr);
    } else {
        return false;
    }
}

// Saves an image
bool save_imageb(const std::string& filename, int width, int height, int ncomp,
    const byte* ldr) {
    if (path_extension(filename) == ".png") {
        return stbi_write_png(
            filename.c_str(), width, height, ncomp, ldr, width * ncomp);
    } else if (path_extension(filename) == ".jpg") {
        return stbi_write_jpg(filename.c_str(), width, height, ncomp, ldr, 75);
    } else {
        return false;
    }
}

// Save an HDR or LDR image with tonemapping based on filename
bool save_image(const std::string& filename, const image4f& hdr,
    tonemap_type tonemapper, float exposure) {
    if (is_hdr_filename(filename)) {
        return save_image4f(filename, hdr);
    } else {
        auto ldr = tonemap_image(hdr, tonemapper, exposure);
        return save_image4b(filename, ldr);
    }
}

// Resize image.
void resize_image(const image4f& img, image4f& res_img, resize_filter filter,
    resize_edge edge, bool premultiplied_alpha) {
    static const auto filter_map = std::map<resize_filter, stbir_filter>{
        {resize_filter::def, STBIR_FILTER_DEFAULT},
        {resize_filter::box, STBIR_FILTER_BOX},
        {resize_filter::triangle, STBIR_FILTER_TRIANGLE},
        {resize_filter::cubic_spline, STBIR_FILTER_CUBICBSPLINE},
        {resize_filter::catmull_rom, STBIR_FILTER_CATMULLROM},
        {resize_filter::mitchell, STBIR_FILTER_MITCHELL}};

    static const auto edge_map =
        std::map<resize_edge, stbir_edge>{{resize_edge::def, STBIR_EDGE_CLAMP},
            {resize_edge::clamp, STBIR_EDGE_CLAMP},
            {resize_edge::reflect, STBIR_EDGE_REFLECT},
            {resize_edge::wrap, STBIR_EDGE_WRAP},
            {resize_edge::zero, STBIR_EDGE_ZERO}};

    stbir_resize_float_generic((float*)img.pixels.data(), img.width, img.height,
        sizeof(vec4f) * img.width, (float*)res_img.pixels.data(), res_img.width,
        res_img.height, sizeof(vec4f) * res_img.width, 4, 3,
        (premultiplied_alpha) ? STBIR_FLAG_ALPHA_PREMULTIPLIED : 0,
        edge_map.at(edge), filter_map.at(filter), STBIR_COLORSPACE_LINEAR,
        nullptr);
}

// Resize image.
void resize_image(const image4b& img, image4b& res_img, resize_filter filter,
    resize_edge edge, bool premultiplied_alpha) {
    static const auto filter_map = std::map<resize_filter, stbir_filter>{
        {resize_filter::def, STBIR_FILTER_DEFAULT},
        {resize_filter::box, STBIR_FILTER_BOX},
        {resize_filter::triangle, STBIR_FILTER_TRIANGLE},
        {resize_filter::cubic_spline, STBIR_FILTER_CUBICBSPLINE},
        {resize_filter::catmull_rom, STBIR_FILTER_CATMULLROM},
        {resize_filter::mitchell, STBIR_FILTER_MITCHELL}};

    static const auto edge_map =
        std::map<resize_edge, stbir_edge>{{resize_edge::def, STBIR_EDGE_CLAMP},
            {resize_edge::clamp, STBIR_EDGE_CLAMP},
            {resize_edge::reflect, STBIR_EDGE_REFLECT},
            {resize_edge::wrap, STBIR_EDGE_WRAP},
            {resize_edge::zero, STBIR_EDGE_ZERO}};

    stbir_resize_uint8_generic((unsigned char*)img.pixels.data(), img.width,
        img.height, sizeof(vec4b) * img.width,
        (unsigned char*)res_img.pixels.data(), res_img.width, res_img.height,
        sizeof(vec4b) * res_img.width, 4, 3,
        (premultiplied_alpha) ? STBIR_FLAG_ALPHA_PREMULTIPLIED : 0,
        edge_map.at(edge), filter_map.at(filter), STBIR_COLORSPACE_LINEAR,
        nullptr);
}

}  // namespace ygl

// -----------------------------------------------------------------------------
// IMPLEMENTATION OF IMAGE OPERATIONS
// -----------------------------------------------------------------------------
namespace ygl {

inline vec3f tonemap_gamma(const vec3f& x) {
    return {pow(x.x, 1 / 2.2f), pow(x.y, 1 / 2.2f), pow(x.z, 1 / 2.2f)};
}

inline float tonemap_srgb(float x) {
    if (x <= 0.0031308f) return 12.92f * x;
    return 1.055f * pow(x, 1 / 2.4f) - 0.055f;
}

inline vec3f tonemap_srgb(const vec3f& x) {
    return {tonemap_srgb(x.x), tonemap_srgb(x.y), tonemap_srgb(x.z)};
}

inline vec3f tonemap_filmic1(const vec3f& hdr) {
    // http://filmicworlds.com/blog/filmic-tonemapping-operators/
    auto x = vec3f{max(0.0f, hdr.x - 0.004f), max(0.0f, hdr.y - 0.004f),
        max(0.0f, hdr.z - 0.004f)};
    return (x * (6.2f * x + vec3f{0.5f, 0.5f, 0.5f})) /
           (x * (6.2f * x + vec3f{1.7f, 1.7f, 1.7f}) +
               vec3f{0.06f, 0.06f, 0.06f});
}

inline vec3f tonemap_filmic2(const vec3f& hdr) {
    // https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
    auto x = hdr;
    // x *= 0.6; // brings it back to ACES range
    x = (x * (2.51f * x + vec3f{0.03f, 0.03f, 0.03f})) /
        (x * (2.43f * x + vec3f{0.59f, 0.59f, 0.59f}) +
            vec3f{0.14f, 0.14f, 0.14f});
    return tonemap_gamma(x);
}

inline vec3f tonemap_filmic3(const vec3f& hdr) {
    // https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl

    // sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
    static const mat3f ACESInputMat = transpose(mat3f{
        vec3f{0.59719, 0.35458, 0.04823}, vec3f{0.07600, 0.90834, 0.01566},
        vec3f{0.02840, 0.13383, 0.83777}});

    // ODT_SAT => XYZ => D60_2_D65 => sRGB
    static const mat3f ACESOutputMat = transpose(mat3f{
        vec3f{1.60475, -0.53108, -0.07367}, vec3f{-0.10208, 1.10813, -0.00605},
        vec3f{-0.00327, -0.07276, 1.07602}});

    auto x = hdr;
    x = 2 * x;  // matches standard range
    x = ACESInputMat * x;
    // Apply RRT and ODT
    vec3f a = x * (x + vec3f{0.0245786f, 0.0245786f, 0.0245786f}) -
              vec3f{0.000090537f, 0.000090537f, 0.000090537f};
    vec3f b = x * (0.983729f * x + vec3f{0.4329510f, 0.4329510f, 0.4329510f}) +
              vec3f{0.238081f, 0.238081f, 0.238081f};
    x = a / b;
    x = ACESOutputMat * x;
    return tonemap_gamma(x);
}

// Tone mapping HDR to LDR images.
image4b tonemap_image(
    const image4f& hdr, tonemap_type tonemapper, float exposure) {
    auto ldr = make_image4b(hdr.width, hdr.height);
    auto scale = pow(2.0f, exposure);
    for (auto j = 0; j < hdr.height; j++) {
        for (auto i = 0; i < hdr.width; i++) {
            auto h4 = hdr.at(i, j);
            auto h = vec3f{h4.x, h4.y, h4.z} * scale;
            auto a = h4.w;
            switch (tonemapper) {
                case tonemap_type::linear: break;
                case tonemap_type::gamma: h = tonemap_gamma(h); break;
                case tonemap_type::srgb: h = tonemap_srgb(h); break;
                case tonemap_type::filmic1: h = tonemap_filmic1(h); break;
                case tonemap_type::filmic2: h = tonemap_filmic2(h); break;
                case tonemap_type::filmic3: h = tonemap_filmic3(h); break;
            }
            ldr.at(i, j) = float_to_byte({h.x, h.y, h.z, a});
        }
    }
    return ldr;
}

// Image over operator
void image_over(
    vec4f* img, int width, int height, int nlayers, vec4f** layers) {
    for (auto i = 0; i < width * height; i++) {
        img[i] = {0, 0, 0, 0};
        auto weight = 1.0f;
        for (auto l = 0; l < nlayers; l++) {
            img[i].x += layers[l][i].x * layers[l][i].w * weight;
            img[i].y += layers[l][i].y * layers[l][i].w * weight;
            img[i].z += layers[l][i].z * layers[l][i].w * weight;
            img[i].w += layers[l][i].w * weight;
            weight *= (1 - layers[l][i].w);
        }
        if (img[i].w) {
            img[i].x /= img[i].w;
            img[i].y /= img[i].w;
            img[i].z /= img[i].w;
        }
    }
}

// Image over operator
void image_over(
    vec4b* img, int width, int height, int nlayers, vec4b** layers) {
    for (auto i = 0; i < width * height; i++) {
        auto comp = zero4f;
        auto weight = 1.0f;
        for (auto l = 0; l < nlayers && weight > 0; l++) {
            auto w = layers[l][i].w / 255.0f;
            comp.x += layers[l][i].x / 255.0f * w * weight;
            comp.y += layers[l][i].y / 255.0f * w * weight;
            comp.z += layers[l][i].z / 255.0f * w * weight;
            comp.w += w * weight;
            weight *= (1 - w);
        }
        if (comp.w) {
            img[i] = float_to_byte(
                {comp.x / comp.w, comp.y / comp.w, comp.z / comp.w, comp.w});
        } else {
            img[i] = {0, 0, 0, 0};
        }
    }
}

// Convert HSV to RGB
// Implementatkion from
// http://stackoverflow.com/questions/3018313/algorithm-to-convert-rgb-to-hsv-and-hsv-to-rgb-in-range-0-255-for-both
vec4b hsv_to_rgb(const vec4b& hsv) {
    vec4b rgb = {0, 0, 0, hsv.w};
    byte region, remainder, p, q, t;

    byte h = hsv.x, s = hsv.y, v = hsv.z;

    if (s == 0) {
        rgb.x = v;
        rgb.y = v;
        rgb.z = v;
        return rgb;
    }

    region = h / 43;
    remainder = (h - (region * 43)) * 6;

    p = (v * (255 - s)) >> 8;
    q = (v * (255 - ((s * remainder) >> 8))) >> 8;
    t = (v * (255 - ((s * (255 - remainder)) >> 8))) >> 8;

    switch (region) {
        case 0:
            rgb.x = v;
            rgb.y = t;
            rgb.z = p;
            break;
        case 1:
            rgb.x = q;
            rgb.y = v;
            rgb.z = p;
            break;
        case 2:
            rgb.x = p;
            rgb.y = v;
            rgb.z = t;
            break;
        case 3:
            rgb.x = p;
            rgb.y = q;
            rgb.z = v;
            break;
        case 4:
            rgb.x = t;
            rgb.y = p;
            rgb.z = v;
            break;
        default:
            rgb.x = v;
            rgb.y = p;
            rgb.z = q;
            break;
    }

    return rgb;
}

}  // namespace ygl

// -----------------------------------------------------------------------------
// IMPLEMENTATION FOR IMAGE EXAMPLES
// -----------------------------------------------------------------------------
namespace ygl {

// Make a grid image
image4b make_grid_image(
    int width, int height, int tile, const vec4b& c0, const vec4b& c1) {
    auto img = make_image4b(width, height);
    for (int j = 0; j < width; j++) {
        for (int i = 0; i < height; i++) {
            auto c = i % tile == 0 || i % tile == tile - 1 || j % tile == 0 ||
                     j % tile == tile - 1;
            img.at(i, j) = (c) ? c0 : c1;
        }
    }
    return img;
}

// Make a checkerboard image
image4b make_checker_image(
    int width, int height, int tile, const vec4b& c0, const vec4b& c1) {
    auto img = make_image4b(width, height);
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            auto c = (i / tile + j / tile) % 2 == 0;
            img.at(i, j) = (c) ? c0 : c1;
        }
    }
    return img;
}

// Make an image with bumps and dimples.
image4b make_bumpdimple_image(int width, int height, int tile) {
    auto img = make_image4b(width, height);
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            auto c = (i / tile + j / tile) % 2 == 0;
            auto ii = i % tile - tile / 2, jj = j % tile - tile / 2;
            auto r =
                sqrt(float(ii * ii + jj * jj)) / sqrt(float(tile * tile) / 4);
            auto h = 0.5f;
            if (r < 0.5f) { h += (c) ? (0.5f - r) : -(0.5f - r); }
            img.at(i, j) = float_to_byte({h, h, h, 1});
        }
    }
    return img;
}

// Make a uv colored grid
image4b make_ramp_image(
    int width, int height, const vec4b& c0, const vec4b& c1, bool srgb) {
    auto img = make_image4b(width, height);
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            auto u = (float)i / (float)width;
            if (srgb) {
                img.at(i, j) = linear_to_srgb(
                    srgb_to_linear(c0) * (1 - u) + srgb_to_linear(c1) * u);
            } else {
                img.at(i, j) = float_to_byte(
                    byte_to_float(c0) * (1 - u) + byte_to_float(c1) * u);
            }
        }
    }
    return img;
}

// Make a gamma ramp image
image4b make_gammaramp_image(int width, int height) {
    auto img = make_image4b(width, height);
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            auto u = j / float(height - 1);
            if (i < width / 3) u = pow(u, 2.2f);
            if (i > (width * 2) / 3) u = pow(u, 1 / 2.2f);
            auto c = (unsigned char)(u * 255);
            img.at(i, j) = {c, c, c, 255};
        }
    }
    return img;
}

// Make a gamma ramp image
image4f make_gammaramp_imagef(int width, int height) {
    auto img = make_image4f(width, height);
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            auto u = j / float(height - 1);
            if (i < width / 3) u = pow(u, 2.2f);
            if (i > (width * 2) / 3) u = pow(u, 1 / 2.2f);
            img.at(i, j) = {u, u, u, 1};
        }
    }
    return img;
}

// Make an image color with red/green in the [0,1] range. Helpful to visualize
// uv texture coordinate application.
image4b make_uv_image(int width, int height) {
    auto img = make_image4b(width, height);
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            img.at(i, j) = float_to_byte(
                {i / (float)(width - 1), j / (float)(height - 1), 0, 255});
        }
    }
    return img;
}

// Make a uv colored grid
image4b make_uvgrid_image(int width, int height, int tile, bool colored) {
    auto img = make_image4b(width, height);
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            byte ph = 32 * (i / (height / 8));
            byte pv = 128;
            byte ps = 64 + 16 * (7 - j / (height / 8));
            if (i % (tile / 2) && j % (tile / 2)) {
                if ((i / tile + j / tile) % 2)
                    pv += 16;
                else
                    pv -= 16;
            } else {
                pv = 196;
                ps = 32;
            }
            img.at(i, j) = (colored) ? hsv_to_rgb({ph, ps, pv, 255}) :
                                       vec4b{pv, pv, pv, 255};
        }
    }
    return img;
}

// Make a uv recusive colored grid
image4b make_recuvgrid_image(int width, int height, int tile, bool colored) {
    auto img = make_image4b(width, height);
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            byte ph = 32 * (i / (height / 8));
            byte pv = 128;
            byte ps = 64 + 16 * (7 - j / (height / 8));
            if (i % (tile / 2) && j % (tile / 2)) {
                if ((i / tile + j / tile) % 2)
                    pv += 16;
                else
                    pv -= 16;
                if ((i / (tile / 4) + j / (tile / 4)) % 2)
                    pv += 4;
                else
                    pv -= 4;
                if ((i / (tile / 8) + j / (tile / 8)) % 2)
                    pv += 1;
                else
                    pv -= 1;
            } else {
                pv = 196;
                ps = 32;
            }
            img.at(i, j) = (colored) ? hsv_to_rgb({ph, ps, pv, 255}) :
                                       vec4b{pv, pv, pv, 255};
        }
    }
    return img;
}

// Comvert a bump map to a normal map.
image4b bump_to_normal_map(const image4b& img, float scale) {
    auto norm = make_image4b(img.width, img.height);
    for (int j = 0; j < img.height; j++) {
        for (int i = 0; i < img.width; i++) {
            auto i1 = (i + 1) % img.width, j1 = (j + 1) % img.height;
            auto p00 = img.at(i, j), p10 = img.at(i1, j), p01 = img.at(i, j1);
            auto g00 = (float(p00.x) + float(p00.y) + float(p00.z)) / (3 * 255);
            auto g01 = (float(p01.x) + float(p01.y) + float(p01.z)) / (3 * 255);
            auto g10 = (float(p10.x) + float(p10.y) + float(p10.z)) / (3 * 255);
            auto n = vec3f{scale * (g00 - g10), scale * (g00 - g01), 1.0f};
            n = normalize(n) * 0.5f + vec3f{0.5f, 0.5f, 0.5f};
            auto c =
                vec4b{byte(n.x * 255), byte(n.y * 255), byte(n.z * 255), 255};
            norm.at(i, j) = c;
        }
    }
    return norm;
}

// Implementation of sunsky modified heavily from pbrt
image4f make_sunsky_image(
    int res, float thetaSun, float turbidity, bool has_sun, bool has_ground) {
    auto wSun = vec3f{0, cos(thetaSun), sin(thetaSun)};

    // sunSpectralRad =  ComputeAttenuatedSunlight(thetaS, turbidity);
    auto sunAngularRadius = 9.35e-03f / 2;  // Wikipedia
    auto thetaS = thetaSun;

    auto t1 = thetaSun, t2 = thetaSun * thetaSun,
         t3 = thetaSun * thetaSun * thetaSun;
    auto T = turbidity;
    auto T2 = turbidity * turbidity;

    auto zenith_xyY = vec3f{
        (+0.00165f * t3 - 0.00374f * t2 + 0.00208f * t1 + 0) * T2 +
            (-0.02902f * t3 + 0.06377f * t2 - 0.03202f * t1 + 0.00394f) * T +
            (+0.11693f * t3 - 0.21196f * t2 + 0.06052f * t1 + 0.25885f),
        (+0.00275f * t3 - 0.00610f * t2 + 0.00316f * t1 + 0) * T2 +
            (-0.04214f * t3 + 0.08970f * t2 - 0.04153f * t1 + 0.00515f) * T +
            (+0.15346f * t3 - 0.26756f * t2 + 0.06669f * t1 + 0.26688f),
        1000 * (4.0453f * T - 4.9710f) *
                tan((4.0f / 9.0f - T / 120.0f) * (pi - 2 * t1)) -
            .2155f * T + 2.4192f};

    auto perez_A_xyY = vec3f{-0.01925f * T - 0.25922f, -0.01669f * T - 0.26078f,
        +0.17872f * T - 1.46303f};
    auto perez_B_xyY = vec3f{-0.06651f * T + 0.00081f, -0.09495f * T + 0.00921f,
        -0.35540f * T + 0.42749f};
    auto perez_C_xyY = vec3f{-0.00041f * T + 0.21247f, -0.00792f * T + 0.21023f,
        -0.02266f * T + 5.32505f};
    auto perez_D_xyY = vec3f{-0.06409f * T - 0.89887f, -0.04405f * T - 1.65369f,
        +0.12064f * T - 2.57705f};
    auto perez_E_xyY = vec3f{-0.00325f * T + 0.04517f, -0.01092f * T + 0.05291f,
        -0.06696f * T + 0.37027f};

    auto perez_f = [thetaS](float A, float B, float C, float D, float E,
                       float theta, float gamma, float zenith) -> float {
        auto den = ((1 + A * exp(B)) *
                    (1 + C * exp(D * thetaS) + E * cos(thetaS) * cos(thetaS)));
        auto num = ((1 + A * exp(B / cos(theta))) *
                    (1 + C * exp(D * gamma) + E * cos(gamma) * cos(gamma)));
        return zenith * num / den;
    };

    auto sky = [&perez_f, perez_A_xyY, perez_B_xyY, perez_C_xyY, perez_D_xyY,
                   perez_E_xyY, zenith_xyY](auto theta, auto gamma) -> vec3f {
        auto x = perez_f(perez_A_xyY.x, perez_B_xyY.x, perez_C_xyY.x,
            perez_D_xyY.x, perez_E_xyY.x, theta, gamma, zenith_xyY.x);
        auto y = perez_f(perez_A_xyY.y, perez_B_xyY.y, perez_C_xyY.y,
            perez_D_xyY.y, perez_E_xyY.y, theta, gamma, zenith_xyY.y);
        auto Y = perez_f(perez_A_xyY.z, perez_B_xyY.z, perez_C_xyY.z,
            perez_D_xyY.z, perez_E_xyY.z, theta, gamma, zenith_xyY.z);
        return xyz_to_rgb(xyY_to_xyz({x, y, Y})) / 10000.0f;
    };

    // compute sun luminance
    // TODO: how this relates to zenith intensity?
    auto sun_ko = vec3f{0.48f, 0.75f, 0.14f};
    auto sun_kg = vec3f{0.1f, 0.0f, 0.0f};
    auto sun_kwa = vec3f{0.02f, 0.0f, 0.0f};
    auto sun_sol = vec3f{20000.0f, 27000.0f, 30000.0f};
    auto sun_lambda = vec3f{680, 530, 480};
    auto sun_beta = 0.04608365822050f * turbidity - 0.04586025928522f;
    auto sun_m =
        1.0f / (cos(thetaSun) + 0.000940f * pow(1.6386f - thetaSun, -1.253f));

    auto sun_le = zero3f;
    for (auto i = 0; i < 3; i++) {
        auto tauR =
            exp(-sun_m * 0.008735f * pow((&sun_lambda.x)[i] / 1000, -4.08f));
        auto tauA =
            exp(-sun_m * sun_beta * pow((&sun_lambda.x)[i] / 1000, -1.3f));
        auto tauO = exp(-sun_m * (&sun_ko.x)[i] * .35f);
        auto tauG = exp(-1.41f * (&sun_kg.x)[i] * sun_m /
                        pow(1 + 118.93f * (&sun_kg.x)[i] * sun_m, 0.45f));
        auto tauWA =
            exp(-0.2385f * (&sun_kwa.x)[i] * 2.0f * sun_m /
                pow(1 + 20.07f * (&sun_kwa.x)[i] * 2.0f * sun_m, 0.45f));
        (&sun_le.x)[i] = (&sun_sol.x)[i] * tauR * tauA * tauO * tauG * tauWA;
    }

    auto sun = [has_sun, sunAngularRadius, sun_le](auto theta, auto gamma) {
        return (has_sun && gamma < sunAngularRadius) ? sun_le / 10000.0f :
                                                       zero3f;
    };

    auto img = make_image4f(2 * res, res);
    for (auto j = 0; j < img.height; j++) {
        if (!has_ground && j >= img.height / 2) continue;
        auto theta = pi * ((j + 0.5f) / img.height);
        theta = clamp(theta, 0.0f, pi / 2 - flt_eps);
        for (int i = 0; i < img.width; i++) {
            auto phi = 2 * pi * (float(i + 0.5f) / img.width);
            auto w =
                vec3f{cos(phi) * sin(theta), cos(theta), sin(phi) * sin(theta)};
            auto gamma = acos(clamp(dot(w, wSun), -1.0f, 1.0f));
            auto col = sky(theta, gamma) + sun(theta, gamma);
            img.at(i, j) = {col.x, col.y, col.z, 1};
        }
    }

    return img;
}

// Make a noise image. Wrap works only if both resx and resy are powers of two.
image4b make_noise_image(int resx, int resy, float scale, bool wrap) {
    auto wrap3i = (wrap) ? vec3i{resx, resy, 2} : zero3i;
    auto img = make_image4b(resx, resy);
    for (auto j = 0; j < resy; j++) {
        for (auto i = 0; i < resx; i++) {
            auto p = vec3f{i / (float)resx, j / (float)resy, 0.5f} * scale;
            auto g = perlin_noise(p, wrap3i);
            g = clamp(0.5f + 0.5f * g, 0.0f, 1.0f);
            img.at(i, j) = float_to_byte({g, g, g, 1});
        }
    }
    return img;
}

// Make a noise image. Wrap works only if both resx and resy are powers of two.
image4b make_fbm_image(int resx, int resy, float scale, float lacunarity,
    float gain, int octaves, bool wrap) {
    auto wrap3i = (wrap) ? vec3i{resx, resy, 2} : zero3i;
    auto img = make_image4b(resx, resy);
    for (auto j = 0; j < resy; j++) {
        for (auto i = 0; i < resx; i++) {
            auto p = vec3f{i / (float)resx, j / (float)resy, 0.5f} * scale;
            auto g = perlin_fbm_noise(p, lacunarity, gain, octaves, wrap3i);
            g = clamp(0.5f + 0.5f * g, 0.0f, 1.0f);
            img.at(i, j) = float_to_byte({g, g, g, 1});
        }
    }
    return img;
}

// Make a noise image. Wrap works only if both resx and resy are powers of two.
image4b make_ridge_image(int resx, int resy, float scale, float lacunarity,
    float gain, float offset, int octaves, bool wrap) {
    auto wrap3i = (wrap) ? vec3i{resx, resy, 2} : zero3i;
    auto img = make_image4b(resx, resy);
    for (auto j = 0; j < resy; j++) {
        for (auto i = 0; i < resx; i++) {
            auto p = vec3f{i / (float)resx, j / (float)resy, 0.5f} * scale;
            auto g = perlin_ridge_noise(
                p, lacunarity, gain, offset, octaves, wrap3i);
            g = clamp(g, 0.0f, 1.0f);
            img.at(i, j) = float_to_byte({g, g, g, 1});
        }
    }
    return img;
}

// Make a noise image. Wrap works only if both resx and resy are powers of two.
image4b make_turbulence_image(int resx, int resy, float scale, float lacunarity,
    float gain, int octaves, bool wrap) {
    auto wrap3i = (wrap) ? vec3i{resx, resy, 2} : zero3i;
    auto img = make_image4b(resx, resy);
    for (auto j = 0; j < resy; j++) {
        for (auto i = 0; i < resx; i++) {
            auto p = vec3f{i / (float)resx, j / (float)resy, 0.5f} * scale;
            auto g =
                perlin_turbulence_noise(p, lacunarity, gain, octaves, wrap3i);
            g = clamp(g, 0.0f, 1.0f);
            img.at(i, j) = float_to_byte({g, g, g, 1});
        }
    }
    return img;
}

}  // namespace ygl