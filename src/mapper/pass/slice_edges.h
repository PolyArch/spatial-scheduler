#include "dsa/dfg/ssdfg.h"

namespace dsa {
namespace dfg {
namespace pass {

void split_edge(const std::vector<int> &v, SSDfg *dfg, int eid) {
  std::vector<Edge> &edges = dfg->edges;
  int l = edges[eid].l;
  int r = edges[eid].r;
  auto &uses = dfg->nodes[edges[eid].sid]->values[edges[eid].vid].uses;
  auto use_idx = std::find(uses.begin(), uses.end(), eid) - uses.begin();
  CHECK(use_idx != uses.size());

  Operand *op = nullptr;
  int op_idx = -1;
  {
    for (auto &elem : dfg->nodes[edges[eid].uid]->ops()) {
      auto iter = std::find(elem.edges.begin(), elem.edges.end(), eid);
      if (iter != elem.edges.end()) {
        op = &elem;
        op_idx = iter - elem.edges.begin();
        break;
      }
    }
    CHECK(op && op_idx != -1);
  }

  auto &edge = edges[eid];
  for (int i = 0, n = v.size(); i < n; i += 2) {
    if (i == 0) {
      edge.l = v[i];
      edge.r = v[i + 1];
    } else {
      edges.emplace_back(dfg, edge.sid, edge.vid, edge.uid, v[i], v[i + 1]);
      uses.insert(uses.begin() + use_idx, edges.back().id);
      op->edges.insert(op->edges.begin() + op_idx, edges.back().id);
    }
    ++use_idx;
    ++op_idx;
  }
}

inline void SliceOverlappedEdges(SSDfg *dfg) {
  auto &edges = dfg->edges;
  auto &nodes = dfg->nodes;
  for (int i = 0, n = edges.size(); i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      if (edges[i].sid == edges[j].sid && edges[j].vid == edges[j].vid) {
        if (edges[i].r < edges[j].l || edges[j].r < edges[i].l) {
          continue;
        }
        if (edges[i].l == edges[j].l && edges[i].r == edges[j].r) {
          continue;
        }
        std::vector<int> v;
        int l = std::min(edges[i].l, edges[j].l);
        int r = std::max(edges[i].r, edges[j].r);
        v.push_back(l);
        v.push_back(r);
#define PUSH_POINT(v, mid, l, r, is_left) \
  do {                                    \
    if (l < mid && mid < r) {             \
      if (is_left) {                      \
        v.push_back(mid - 1);             \
        v.push_back(mid);                 \
      } else {                            \
        v.push_back(mid);                 \
        v.push_back(mid + 1);             \
      }                                   \
    }                                     \
  } while (false)
        PUSH_POINT(v, edges[i].l, l, r, true);
        PUSH_POINT(v, edges[j].l, l, r, true);
        PUSH_POINT(v, edges[i].r, l, r, false);
        PUSH_POINT(v, edges[j].r, l, r, false);
        std::sort(v.begin(), v.end());
        auto tail = std::unique(v.begin(), v.end());
        v.erase(tail, v.end());
        LOG(SLICE) << l << ", " << r;
        LOG(SLICE) << edges[i].name();
        LOG(SLICE) << edges[j].name();
        for (auto elem : v) {
          LOG(SLICE) << elem;
        }
        CHECK(v.size() % 2 == 0) << v.size();
        split_edge(v, dfg, i);
        split_edge(v, dfg, j);
#undef PUSH_POINT
      }
    }
  }

  for (auto &edge : dfg->edges) {
    LOG(SLICE) << edge.name();
  }
}


}
}
}