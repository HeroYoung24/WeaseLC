#pragma once
// Shared upstream color loader. Candidate UI and correction previews use one parser.
#include <windows.h>
#include <WeaselIPCData.h>
#include <rime_api.h>
#include <regex>
#include <map>
#include <string>

namespace weasel { namespace style_config {
#define ARGB2ABGR(value)                                 \
  ((value & 0xff000000) | ((value & 0x000000ff) << 16) | \
   (value & 0x0000ff00) | ((value & 0x00ff0000) >> 16))
#define RGBA2ABGR(value)                                   \
  (((value & 0xff) << 24) | ((value & 0xff000000) >> 24) | \
   ((value & 0x00ff0000) >> 8) | ((value & 0x0000ff00) << 8))
typedef enum { COLOR_ABGR = 0, COLOR_ARGB, COLOR_RGBA } ColorFormat;

#ifdef USE_SHARP_COLOR_CODE
#define HEX_REGEX std::regex("^(0x|#)[0-9a-f]+$", std::regex::icase)
#define TRIMHEAD_REGEX std::regex("0x|#", std::regex::icase)
#else
#define HEX_REGEX std::regex("^0x[0-9a-f]+$", std::regex::icase)
#define TRIMHEAD_REGEX std::regex("0x", std::regex::icase)
#endif
inline COLORREF blend_colors(COLORREF fcolor, COLORREF bcolor) {
  // 提取各通道的值
  BYTE fA = (fcolor >> 24) & 0xFF;  // 获取前景的 alpha 通道
  BYTE fB = (fcolor >> 16) & 0xFF;  // 获取前景的 blue 通道
  BYTE fG = (fcolor >> 8) & 0xFF;   // 获取前景的 green 通道
  BYTE fR = fcolor & 0xFF;          // 获取前景的 red 通道
  BYTE bA = (bcolor >> 24) & 0xFF;  // 获取背景的 alpha 通道
  BYTE bB = (bcolor >> 16) & 0xFF;  // 获取背景的 blue 通道
  BYTE bG = (bcolor >> 8) & 0xFF;   // 获取背景的 green 通道
  BYTE bR = bcolor & 0xFF;          // 获取背景的 red 通道
  // 将 alpha 通道转换为 [0, 1] 的浮动值
  float fAlpha = fA / 255.0f;
  float bAlpha = bA / 255.0f;
  // 计算每个通道的加权平均值
  float retAlpha = fAlpha + (1 - fAlpha) * bAlpha;
  if (retAlpha == 0) return 0;
  // 混合红、绿、蓝通道
  BYTE retR = (BYTE)((fR * fAlpha + bR * bAlpha * (1 - fAlpha)) / retAlpha);
  BYTE retG = (BYTE)((fG * fAlpha + bG * bAlpha * (1 - fAlpha)) / retAlpha);
  BYTE retB = (BYTE)((fB * fAlpha + bB * bAlpha * (1 - fAlpha)) / retAlpha);
  // 返回合成后的颜色
  return (BYTE)(retAlpha * 255) << 24 | retB << 16 | retG << 8 | retR;
}
// parse color value, with fallback value
inline Bool _RimeGetColor(RimeApi* rime_api, RimeConfig* config,
                          const std::string key,
                          int& value,
                          const ColorFormat& fmt,
                          const unsigned int& fallback) {
  char color[256] = {0};
  if (!rime_api->config_get_string(config, key.c_str(), color, 256)) {
    value = fallback;
    return False;
  }
  const auto color_str = std::string(color);
  const auto make_opaque = [&](int& value) {
    value = (fmt != COLOR_RGBA) ? (value | 0xff000000)
                                : ((value << 8) | 0x000000ff);
  };
  const auto ConvertColorToAbgr = [](int color, ColorFormat fmt = COLOR_ABGR) {
    if (fmt == COLOR_ABGR)
      return color & 0xffffffff;
    else if (fmt == COLOR_ARGB)
      return ARGB2ABGR(color) & 0xffffffff;
    else
      return RGBA2ABGR(color) & 0xffffffff;
  };
  if (std::regex_match(color_str, HEX_REGEX)) {
    auto tmp = std::regex_replace(color_str, TRIMHEAD_REGEX, "").substr(0, 8);
    switch (tmp.length()) {
      case 6:  // color code without alpha, xxyyzz add alpha ff
        value = std::stoul(tmp, 0, 16);
        make_opaque(value);
        break;
      case 3:  // color hex code xyz => xxyyzz and alpha ff
        tmp = std::string(2, tmp[0]) + std::string(2, tmp[1]) +
              std::string(2, tmp[2]);
        value = std::stoul(tmp, 0, 16);
        make_opaque(value);
        break;
      case 4:  // color hex code vxyz => vvxxyyzz
        tmp = std::string(2, tmp[0]) + std::string(2, tmp[1]) +
              std::string(2, tmp[2]) + std::string(2, tmp[3]);
        value = std::stoul(tmp, 0, 16);
        break;
      case 7:
      case 8:  // color code with alpha
        value = std::stoul(tmp, 0, 16);
        break;
      default:  // invalid length
        value = fallback;
        return False;
    }
  } else {
    int tmp = 0;
    if (!rime_api->config_get_int(config, key.c_str(), &tmp)) {
      value = fallback;
      return False;
    } else
      value = tmp;
    make_opaque(value);
  }
  value = ConvertColorToAbgr(value, fmt);
  return True;
}
inline bool LoadColors(RimeApi* rime_api, RimeConfig* config,
                                UIStyle& style,
                                std::string color) {
  const int BUF_SIZE = 255;
  char buffer[BUF_SIZE + 1] = {0};
  std::string color_mark = "style/color_scheme";
  // color scheme
  if (rime_api->config_get_string(config, color_mark.c_str(), buffer,
                                  BUF_SIZE) ||
      !color.empty()) {
    std::string prefix("preset_color_schemes/");
    prefix += (color.empty()) ? buffer : color;
    // define color format, default abgr if not set
    ColorFormat fmt = COLOR_ABGR;
    const std::map<std::string, ColorFormat> _colorFmt = {
        {std::string("argb"), COLOR_ARGB},
        {std::string("rgba"), COLOR_RGBA},
        {std::string("abgr"), COLOR_ABGR}};
    char format[16]{};
    if (rime_api->config_get_string(config, (prefix + "/color_format").c_str(), format, sizeof(format))) {
      const auto found = _colorFmt.find(format);
      if (found != _colorFmt.end()) fmt = found->second;
    }
#define COLOR(key, value, fallback) \
  _RimeGetColor(rime_api, config, (prefix + "/" + key), value, fmt, fallback)
    COLOR("back_color", style.back_color, 0xffffffff);
    COLOR("shadow_color", style.shadow_color, 0);
    COLOR("prevpage_color", style.prevpage_color, 0);
    COLOR("nextpage_color", style.nextpage_color, 0);
    COLOR("text_color", style.text_color, 0xff000000);
    COLOR("candidate_text_color", style.candidate_text_color, style.text_color);
    COLOR("candidate_back_color", style.candidate_back_color, 0);
    COLOR("border_color", style.border_color, style.text_color);
    COLOR("hilited_text_color", style.hilited_text_color, style.text_color);
    COLOR("hilited_back_color", style.hilited_back_color, style.back_color);
    COLOR("hilited_candidate_text_color", style.hilited_candidate_text_color,
          style.hilited_text_color);
    COLOR("hilited_candidate_back_color", style.hilited_candidate_back_color,
          style.hilited_back_color);
    COLOR("hilited_candidate_shadow_color",
          style.hilited_candidate_shadow_color, 0);
    COLOR("hilited_shadow_color", style.hilited_shadow_color, 0);
    COLOR("candidate_shadow_color", style.candidate_shadow_color, 0);
    COLOR("candidate_border_color", style.candidate_border_color, 0);
    COLOR("hilited_candidate_border_color",
          style.hilited_candidate_border_color, 0);
    COLOR("label_color", style.label_text_color,
          blend_colors(style.candidate_text_color, style.candidate_back_color));
    COLOR("hilited_label_color", style.hilited_label_text_color,
          blend_colors(style.hilited_candidate_text_color,
                       style.hilited_candidate_back_color));
    COLOR("comment_text_color", style.comment_text_color,
          style.label_text_color);
    COLOR("hilited_comment_text_color", style.hilited_comment_text_color,
          style.hilited_label_text_color);
    COLOR("hilited_mark_color", style.hilited_mark_color, 0);
#undef COLOR
    return true;
  }
  return false;
}

} }
#undef ARGB2ABGR
#undef RGBA2ABGR
#undef HEX_REGEX
#undef TRIMHEAD_REGEX
