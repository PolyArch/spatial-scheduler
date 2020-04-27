#pragma once

#include <map>
#include <tuple>
#include <algorithm>

#include "dsa/ir/ssdfg.h"

namespace cm {

inline int ColorOf(SSDfgValue *val, bool reset = false) {
  SSDfgNode* node = val->node();
  if (node->num_inc() == 1 && node->ops()[0].edges.size() == 1) {
    return ColorOf(node->ops()[0].get_first_edge()->val());
  }
  static std::map<SSDfgNode*, std::tuple<int, int, int>> colorMap;
  if (colorMap.count(node) == 0 || reset) {
    int x = 0, y = 0, z = 0;
    float lum = 0;
    while (lum < 0.36f || lum > 0.95f) {  // prevent dark colors
      //  || abs(x-y) + abs(y-z) + abs(z-x) < 100
      x = rand() % 256;
      y = rand() % 256;
      z = rand() % 256;
      lum = sqrt(x * x * 0.241f + y * y * 0.691f + z * z * 0.068f) / 255.0f;
      // lum = (x*0.299f+y*0.587f+z*0.114f)/255.0f;
    }
    colorMap[node] = {x, y, z};
  }
  auto rgb = colorMap[node];
  int r = std::max(std::get<0>(rgb) - val->index() * 15, 0);
  int g = std::max(std::get<1>(rgb) - val->index() * 10, 0);
  int b = std::max(std::get<2>(rgb) - val->index() * 20, 0);
  return r | (g << 8) | (b << 16);
}

}