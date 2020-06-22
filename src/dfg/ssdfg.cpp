#include <iomanip>
#include <list>
#include <set>
#include <string>
#include <vector>

#include "dsa/dfg/visitor.h"
#include "dsa/dfg/ssdfg.h"
#include "dfg-parser.tab.h"
#include "dsa/mapper/schedule.h"
#include "../utils/model_parsing.h"
#include "../utils/vector_utils.h"

using namespace std;
using namespace dsa;

/// { misc

std::string SSDfgEdge::name() {
  std::stringstream ss;
  ss << def()->name() << "." << _value->index() << "[" << l() << ", " << r() << "]"
     << "->" << use()->name();
  return ss.str();
}

SSDfgEdge::SSDfgEdge(SSDfgValue* val, SSDfgNode* use, SSDfg* ssdfg, int l, int r)
    : _ID(ssdfg->inc_edge_id()), _ssdfg(ssdfg), _l(l), _r(r) {
  _value = val;
  _use = use;
}

SSDfgValue* SSDfgEdge::val() const { return _value; }

SSDfgNode* SSDfgEdge::def() const { return _value->node(); }

SSDfgNode* SSDfgEdge::use() const { return _use; }

SSDfgNode* SSDfgEdge::get(int x) const {
  if (x == 0) return def();
  if (x == 1) return _use;
  assert(0);
}

int SSDfgEdge::id() { return _ID; }

int is_subset_at_pos(SSDfgEdge* alt_edge, int pos) { return 0; }

int SSDfgEdge::bitwidth() { return r() - l() + 1; }

int SSDfgEdge::l() { return _l; }

int SSDfgEdge::r() { return _r; }

/// }

/// { SSDfgOperand

SSDfgOperand::SSDfgOperand(SSDfgEdge* e) : edges{e}, fifos(1) {}

SSDfgOperand::SSDfgOperand(std::vector<SSDfgEdge*> es) : edges(es), fifos(es.size()) {}

SSDfgOperand::SSDfgOperand(uint64_t imm_) : imm(imm_) {}

SSDfgEdge* SSDfgOperand::get_first_edge() const {
  return edges.empty() ? nullptr : edges[0];
}

bool SSDfgOperand::is_imm() { return edges.empty(); }

bool SSDfgOperand::valid() {
  for (SSDfgEdge* e : edges) {
    SSDfgNode* n = e->def();
    if (n->invalid()) return false;
  }
  return true;
}

/// }

/// { SSDfgNode

void SSDfgNode::backprop_ctrl_dep() {
  if (!_needs_ctrl_dep) {
    _needs_ctrl_dep = true;
    for (auto* e : _inc_edge_list) {
      e->def()->backprop_ctrl_dep();
    }
  }
}

uint64_t SSDfgNode::invalid() { return _invalid; }

// Add edge to operand in least to most significant bit order
void SSDfgNode::addOperand(unsigned operand_slot, SSDfgEdge* e, int pos_within_op) {
  assert(operand_slot <= 1000);
  if (_ops.size() <= operand_slot) {
    _ops.resize(operand_slot + 1);
  }
  e->set_operand_slot(operand_slot);
  auto& edge_vec = _ops[operand_slot].edges;
  if (pos_within_op == -1) {
    edge_vec.push_back(e);
  } else {
    edge_vec.insert(edge_vec.begin() + pos_within_op, e);
  }

  _inc_edge_list.push_back(e);
}

SSDfg* SSDfgValue::ssdfg() { return _node->ssdfg(); }

void SSDfgValue::addOutEdge(SSDfgEdge* edge) {
  _uses.push_back(edge);
  _node->addOutEdge(edge);  // also add to host node
}

void SSDfgValue::slice(SSDfgEdge* e, int bitwidth) {
  if (e->bitwidth() == bitwidth) return;
  assert(e->bitwidth() > bitwidth);
  assert(bitwidth != 0);

  int orig_bitwidth = e->bitwidth();

  cout << "Slicing edge \"" << e->name() << "\" into " << bitwidth << " bits pieces\n";

  // Modify existing edge to be of smaller bitwidth
  e->set_r(e->l() + bitwidth - 1);

  // Need to find what position within the vector of edges to put the
  // newly created edges in the destination
  auto& edge_list_for_use_op = e->use()->ops()[e->operand_slot()].edges;
  unsigned ith_edge = 0;
  for (; ith_edge < edge_list_for_use_op.size(); ++ith_edge) {
    if (edge_list_for_use_op[ith_edge] == e) {
      break;
    }
  }

  // Add additional edges and update the operand structures
  for (int i = bitwidth; i < orig_bitwidth; i += bitwidth) {
    ssdfg()->connect(this, e->use(), e->operand_slot(), EdgeType::data, e->l() + i,
                     e->l() + i + bitwidth - 1, ith_edge + 1);
    ith_edge++;
  }
}

// Gauranteed here the e1->l() is < e2->l()
void SSDfgValue::slice_overlapping_edge(SSDfgEdge* e1, SSDfgEdge* e2) {
  std::vector<int> points;
  points.push_back(e1->l());
  points.push_back(e1->r() + 1);
  points.push_back(e2->l());
  points.push_back(e2->r() + 1);
  std::sort(points.begin(), points.end());
  int prev_p = points[0];
  int max_diff = 0;
  for (int p : points) {
    if (p == prev_p) continue;
    int diff = p - prev_p;
    if (max_diff < diff) max_diff = diff;
    prev_p = p;
  }

  slice(e1, max_diff);
  slice(e2, max_diff);
}

void SSDfgValue::slice_overlapping_edges() {
  // Just do n^2 algorithm for now
  bool change = true;
  while (change) {
    change = false;
    for (SSDfgEdge* e1 : _uses) {
      for (SSDfgEdge* e2 : _uses) {
        if (e1 == e2) continue;
        if (e1->l() == e2->l() && e1->r() == e2->r()) continue;
        if (e1->l() <= e2->r() && e1->r() >= e2->l()) {
          slice_overlapping_edge(e1, e2);
          change = true;
          break;
        }
      }
      if (change) break;
    }
  }
}

void SSDfgNode::addOutEdge(SSDfgEdge* edge) { _uses.push_back(edge); }

bool SSDfgNode::is_temporal() { return _ssdfg->group_prop(_group_id).is_temporal; }

SSDfgNode::SSDfgNode(SSDfg* ssdfg, V_TYPE v)
    : _ssdfg(ssdfg), _ID(ssdfg->inc_node_id()), _vtype(v) {
  _group_id = _ssdfg->num_groups() - 1;
}

SSDfgNode::SSDfgNode(SSDfg* ssdfg, V_TYPE v, const std::string& name)
    : _ssdfg(ssdfg), _ID(ssdfg->inc_node_id()), _name(name), _vtype(v) {
  _group_id = _ssdfg->num_groups() - 1;
}

SSDfgEdge* SSDfgNode::getLinkTowards(SSDfgNode* to) {
  auto pred = [to](SSDfgEdge* e) -> bool { return e->use() == to; };
  auto res = std::find_if(_uses.begin(), _uses.end(), pred);
  return (res == _uses.end()) ? nullptr : *res;
}

int SSDfgNode::maxThroughput() {
  if (_max_thr == 0) {
    for (auto elem : _uses) {
      _max_thr = std::max(_max_thr, elem->use()->maxThroughput());
    }
  }
  return _max_thr;
}

/// }

/// { Parsing data structure

void CtrlBits::set(uint64_t val, Control b) {
  if (b == CtrlBits::B1 || b == CtrlBits::B2) {
    const_cast<bool&>(is_dynamic) = true;
  }

  int loc = val * Total + b;
  assert(loc >= 0 && loc < 64);
  mask |= (1 << loc);
}

bool CtrlBits::test(uint64_t val, Control b) {
  int loc = val * Total + b;
  assert(loc >= 0 && loc < 64);
  return (mask >> loc) & 1;
}

void CtrlBits::test(uint64_t val, std::vector<bool>& back_array, bool& discard,
                    bool& predicate, bool& reset) {
  if (!mask) return;
  back_array[0] = back_array[0] || test(val, CtrlBits::B1);
  back_array[1] = back_array[1] || test(val, CtrlBits::B2);
  discard = discard || test(val, CtrlBits::Discard);
  predicate = predicate && !(test(val, CtrlBits::Abstain));
  reset = reset || test(val, CtrlBits::Reset);
}

CtrlBits::CtrlBits(const std::map<int, std::vector<std::string>>& raw) {
  for (auto& elem : raw)
    for (auto& s : elem.second) set(elem.first, str_to_enum(s));
}

/// }

/// { SSDfgInst

int SSDfgInst::maxThroughput() {
  if (_max_thr == 0) {
    _max_thr = inst_thr(inst());
    for (auto it = _uses.begin(); it != _uses.end(); it++) {
      _max_thr = std::max(_max_thr, (*it)->use()->maxThroughput());
    }
  }
  return _max_thr;
}

std::string SSDfgInst::name() {
  std::stringstream ss;
  ss << _name << "(" << dsa::name_of_inst(opcode) << " " << id() << ")";
  return ss.str();
}

/// }

/// { SSDfg

void SSDfg::printGraphviz(const char* fname, Schedule* sched) {
  std::ofstream os(fname);
  assert(os.good());
  printGraphviz(os, sched);
  os.flush();
}

void SSDfg::check_for_errors() {
  struct ErrorChecker : dsa::dfg::Visitor {
    void Visit(SSDfgVecInput *vi) {
      CHECK(vi->num_out()) << "No user on input " << vi->name();
    }
    void Visit(SSDfgVecOutput *vi) {
      CHECK(!vi->ops().empty()) << "No operand on output " << vi->name();
    }
    void Visit(SSDfgInst *vi) {
      CHECK(vi->num_out()) << "No user on instruction " << vi->name();
      CHECK(!vi->ops().empty()) << "No operand on instruction " << vi->name();
    }
  };
  ErrorChecker ec;
  Apply(&ec);
}

// Calculates max group throughput based on functional unit type
int SSDfg::maxGroupThroughput(int g) {
  int res = 0;

  assert(g < num_groups());
  for (auto &node : _nodes) {
    if (node->group_id() == g) {
      if (auto vi = dynamic_cast<SSDfgVecInput*>(node)) {
        for (auto use : vi->uses()) {
          res = std::max(res, use->use()->maxThroughput());
        }
      }
    }
  }
  return res;
}

SSDfg::SSDfg() {}

void SSDfg::set_pragma(const std::string& c, const std::string& s) {
  if (c == string("dfg")) {
    cout << "No pragmas yet for dfg\n";
  } else if (c == string("group")) {
    if (s == "temporal") {
      assert(!_groupProps.empty());
      _groupProps[_groupProps.size() - 1].is_temporal = true;
    }
  } else if (c == "frequency" || c == "unroll") {
    std::istringstream iss(s);
    auto& ref =
        c == "frequency" ? _groupProps.back().frequency : _groupProps.back().unroll;
    iss >> ref;
  } else {
    cout << "Context \"" << c << "\" not recognized.";
  }
}

void SSDfg::start_new_dfg_group() { _groupProps.emplace_back(GroupProp()); }

SSDfg::SSDfg(string filename_) : filename(filename_) {
  string line;
  start_new_dfg_group();
  parse_dfg(filename_.c_str(), this);
  preprocess_graph();
  calc_minLats();
  check_for_errors();
}

SSDfgVec::SSDfgVec(V_TYPE v, int len, int bitwidth, const std::string& name,
                   int id, SSDfg* ssdfg, const dsa::dfg::MetaPort& meta_)
    : SSDfgNode(ssdfg, v, name), _bitwidth(bitwidth), _vp_len(len), meta(meta_, this) {
  _group_id = ssdfg->num_groups() - 1;
  _port_width = _bitwidth;
}

uint64_t SSDfgInst::do_compute(bool& discard) {
  last_execution = _ssdfg->cur_cycle();

#define EXECUTE(bw)                                                                     \
  case bw: {                                                                            \
    std::vector<uint##bw##_t> input =                                                   \
      vector_utils::cast_vector<uint64_t, uint##bw##_t>(_input_vals);                   \
    std::vector<uint##bw##_t> outputs(values().size());                                 \
    output = dsa::execute##bw(opcode, input, outputs, (uint##bw##_t*)&_reg[0],          \
                              discard, _back_array);                                    \
    outputs[0] = output;                                                                \
    _output_vals = vector_utils::cast_vector<uint##bw##_t, uint64_t>(outputs);                        \
    return output;                                                                      \
  }

  uint64_t output;
  switch (bitwidth()) {
    EXECUTE(64)
    EXECUTE(32)
    EXECUTE(16)
    EXECUTE(8)
  }

#undef EXECUTE

  CHECK(false) << "Weird bitwidth: " << bitwidth() << "\n";
  throw;
}

void SSDfgInst::resize_vals(int n) { _input_vals.resize(n); }

void SSDfgInst::print_output(std::ostream& os) {
  os << " = ";
  for (int i = 0; i < (int)_values.size(); ++i) {
    os << _output_vals[i];
    os << " ";
  }
}

//------------------------------------------------------------------

void SSDfgNode::printGraphviz(ostream& os, Schedule* sched) {
  string ncolor = "black";
  os << "N" << _ID << " [ label = \"" << name();

  if (is_temporal()) {
    os << ":TMP";
  }

  if (_needs_ctrl_dep) {
    os << ":C";
  }

  if (sched) {
    os << "\\n lat=" << sched->latOf(this) << " ";
  }

  // os << "min:" << _min_lat;

  if (sched) {
    auto p = sched->lat_bounds(this);
    os << "\\n bounds=" << p.first << " to " << p.second;
    os << "\\n vio=" << sched->vioOf(this);
  }

  os << "\", color= \"" << ncolor << "\"]; ";

  os << "\n";

  // print edges
  for (auto v : _values) {
    for (auto e : v->edges()) {
      ncolor = "black";

      SSDfgNode* n = e->use();
      os << "N" << _ID << " -> N" << n->_ID << "[ color=";
      os << ncolor;
      os << " label = \"";
      if (v->index() != 0) {
        os << "v" << v->index() << " ";
      }
      if (sched) {
        os << "l:" << sched->link_count(e) << "\\nex:" << sched->edge_delay(e)
           << "\\npt:" << sched->num_passthroughs(e);
      }
      os << e->l() << ":" << e->r();
      os << "\"];\n";
    }
  }

  os << "\n";
}

// Connect two nodes in DFG
// assumption is that each operand's edges are
// added to in least to most significant order!
SSDfgEdge* SSDfg::connect(SSDfgValue* orig, SSDfgNode* dest, int dest_slot,
                          EdgeType etype, int l, int r, int operand_pos) {
  SSDfgEdge* new_edge = new SSDfgEdge(orig, dest, this, l, r);
  dest->addOperand(dest_slot, new_edge, operand_pos);
  dest->ops().back().type = etype;
  orig->addOutEdge(new_edge);  // this also adds to the node
  _edges.push_back(new_edge);

  return new_edge;
}

void SSDfg::printGraphviz(ostream& os, Schedule* sched) {
  os << "Digraph G { \nnewrank=true;\n ";

  // Insts
  for (auto node : _nodes) {
    node->printGraphviz(os, sched);
  }

  os << "\t{ rank = same; ";
  for (auto in : _vecInputs) {
    os << "N" << in->id() << " ";
  }
  os << "}\n";

  os << "\t{ rank = same; ";
  for (auto out : _vecOutputs) {
    os << "N" << out->id() << " ";
  }
  os << "}\n";

  os << "}\n";
}

// After parsing, preprocess graph to establish reuqired invariants
void SSDfg::preprocess_graph() {
  for (auto node : _nodes) {
    for (auto val : node->values()) {
      val->slice_overlapping_edges();
    }
  }
}

void SSDfg::calc_minLats() {
  list<SSDfgNode*> openset;
  set<bool> seen;
  for (auto elem : _vecInputs) {
    openset.push_back(elem);
    seen.insert(elem);
  }

  // populate the schedule object
  while (!openset.empty()) {
    SSDfgNode* n = openset.front();
    openset.pop_front();

    int cur_lat = 0;

    for (auto elem : n->in_edges()) {
      SSDfgNode* dn = elem->def();
      if (dn->min_lat() > cur_lat) {
        cur_lat = dn->min_lat();
      }
    }

    if (SSDfgInst* inst_n = dynamic_cast<SSDfgInst*>(n)) {
      cur_lat += inst_lat(inst_n->inst()) + 1;
    } else if (dynamic_cast<SSDfgVecInput*>(n)) {
      cur_lat = 0;
    } else if (dynamic_cast<SSDfgVecOutput*>(n)) {
      cur_lat += 1;
    }

    n->set_min_lat(cur_lat);

    for (auto elem : n->uses()) {
      SSDfgNode* un = elem->use();

      bool ready = true;
      for (auto elem : un->in_edges()) {
        SSDfgNode* dn = elem->def();
        if (!seen.count(dn)) {
          ready = false;
          break;
        }
      }
      if (ready) {
        seen.insert(un);
        openset.push_back(un);
      }
    }
  }
}

/// }

using dsa::SpatialFabric;

void SSDfg::Apply(dsa::dfg::Visitor *visitor) {
  for (auto elem : _nodes) {
    elem->Accept(visitor);
  }
}

std::vector<std::pair<int, ssnode*>> SSDfgInst::candidates(Schedule* sched,
                                                           SSModel* ssmodel, int n) {
  SpatialFabric* model = ssmodel->subModel();
  std::vector<std::pair<int, ssnode*>> spots;
  std::vector<std::pair<int, ssnode*>> not_chosen_spots;

  std::vector<ssfu*> fus = model->nodes<dsa::ssfu*>();
  // For Dedicated-required Instructions
  for (size_t i = 0; i < fus.size(); ++i) {
    ssfu* cand_fu = fus[i];

    if (!cand_fu->fu_type_.Capable(inst()) ||
        (cand_fu->out_links().size() < this->values().size())) {
      continue;
    }

    if (!is_temporal()) {
      if (sched->isPassthrough(0, cand_fu))  // FIXME -- this can't be right
        continue;
      // Normal Dedidated Instructions

      if (cand_fu->is_shared() && !spots.empty()) {
        continue;
      }

      for (int k = 0; k < 8; k += this->bitwidth() / 8) {
        int cnt = 0;
        for (int sub_slot = k; sub_slot < k + this->bitwidth() / 8; ++sub_slot) {
          cnt += sched->dfg_nodes_of(sub_slot, cand_fu).size();
        }
        cnt = cnt / 8 + 1;

        if (rand() % (cnt * cnt) == 0) {
          spots.emplace_back(k, fus[i]);
        } else {
          not_chosen_spots.emplace_back(k, fus[i]);
        }
      }

    } else if (cand_fu->is_shared()) {
      // For temporaly-shared instructions
      // For now the approach is to *not* consume dedicated resources, although
      // this can be changed later if that's helpful.
      if ((int)sched->dfg_nodes_of(0, cand_fu).size() + 1 < cand_fu->max_util()) {
        spots.emplace_back(0, fus[i]);
      } else {
        not_chosen_spots.emplace_back(0, fus[i]);
      }
    }
  }

  // If we couldn't find any good spots, we can just pick a bad spot for now
  if (spots.size() == 0) {
    spots = not_chosen_spots;
  }

  std::random_shuffle(spots.begin(), spots.end());

  if (n > (int)spots.size() || n == 0) n = spots.size();

  candidates_cnt_ = n;
  return std::vector<std::pair<int, ssnode*>>(spots.begin(), spots.begin() + n);
}

// TODO: Marge these two functions.
std::vector<std::pair<int, ssnode*>> SSDfgVecInput::candidates(Schedule* sched,
                                                               SSModel* model, int n) {
  auto vports = model->subModel()->input_list();
  // Lets write size in units of bits
  std::vector<std::pair<int, ssnode*>> spots;
  for (size_t i = 0; i < vports.size(); ++i) {
    auto cand = vports[i];
    if ((int)cand->bitwidth_capability() >= phys_bitwidth()) {
      spots.push_back(make_pair(0, cand));
    }
  }
  candidates_cnt_ = spots.size();
  return spots;
}

std::vector<std::pair<int, ssnode*>> SSDfgVecOutput::candidates(Schedule* sched,
                                                                SSModel* model, int n) {
  auto vports = model->subModel()->output_list();
  // Lets write size in units of bits
  std::vector<std::pair<int, ssnode*>> spots;
  for (size_t i = 0; i < vports.size(); ++i) {
    auto cand = vports[i];
    if ((int)cand->bitwidth_capability() >= phys_bitwidth()) {
      spots.push_back(make_pair(0, cand));
    }
  }
  candidates_cnt_ = spots.size();
  return spots;
}

void SSDfgValue::push(uint64_t val, bool valid, int delay) {
  // TODO: Support FIFO length backpressure.
  fifo.push(simulation::Data(_node->_ssdfg->cur_cycle() + delay, val, valid));

  // static int64_t last_print = 0;
  // if (node()->is_temporal()) {
  //  if (last_print != node()->ssdfg()->cur_cycle()) {
  //    std::cerr << " Cycle[" << node()->ssdfg()->cur_cycle() << "]:" << node()->name()
  //              << " Available @" << node()->ssdfg()->cur_cycle() + delay << " "
  //              << (node()->ssdfg()->cur_cycle() - last_print)
  //              << std::endl;
  //    last_print = node()->ssdfg()->cur_cycle();
  //  }
  //}
}

// new compute for back cgra-----------------------------
void SSDfgInst::forward(Schedule* sched) {
  auto loc = sched->location_of(this);

  int inst_throughput = inst_thr(inst());
  if (auto value = sched->lastExecutionKv.Find(loc)) {
    if (_ssdfg->cur_cycle() - *value < (uint64_t) inst_throughput) {
      // std::cerr << _ssdfg->cur_cycle() << ": cannot issue " << name()
      //          << _ssdfg->cur_cycle() - *value << " < "
      //          << inst_throughput << " " << loc.first << ", " << loc.second <<
      //          std::endl;
      return;
    }
  }

  // Check the avaiability of output buffer
  if (!values()[0]->fifo.empty()) {
    for (auto elem : values()) {
      assert(!elem->fifo.empty());
      if (!elem->forward(true)) {
        DEBUG(FORWARD) << _ssdfg->cur_cycle() << ": "
                       << name() << " Cannot forward because of " << elem->name();
        return;
      }
    }
    for (auto elem : values()) {
      assert(elem->forward(false));
    }
    return;
  }

  assert(_ops.size() <= 3);

  _back_array.resize(_ops.size());
  std::fill(_back_array.begin(), _back_array.end(), 0);

  // Check all the operands ready to go!
  for (size_t i = 0; i < _ops.size(); ++i) {
    if (!_ops[i].ready()) {
      DEBUG(FORWARD) << ssdfg()->cur_cycle() << ": " << name() << " Cannot forward since " << (i + 1) << " th op not ready ";
      return;
    }
  }

  bool discard(false), reset(false), pred(true);
  uint64_t output = 0;

  std::ostringstream reason;
  std::ostringstream compute_dump;

  compute_dump << dsa::name_of_inst(inst()) << "(";
  _invalid = false;

  _input_vals.resize(_ops.size(), 0);


  for (unsigned i = 0; i < _ops.size(); ++i) {
    if (_ops[i].is_imm()) {
      _input_vals[i] = _ops[i].imm;
    } else {
      _input_vals[i] = _ops[i].poll();
      if (!_ops[i].predicate()) {
        _invalid = true;
        reason << "operand " << i << " not valid!";
      } else if (_ops[i].type != dsa::dfg::OperandType::data) {
        _ctrl_bits.test(_input_vals[i], _back_array, discard, pred, reset);
      }
    }
    if (i) compute_dump << ", ";
    compute_dump << _input_vals[i];
  }
  compute_dump << ") = (" << name() << ") ";

  // we set this instruction to invalid
  if (!pred) {
    _invalid = true;
  }

  if (!_invalid) {  // IF VALID

    _ssdfg->inc_total_dyn_insts(is_temporal());

    // Read in some temp value and set _val after inst_lat cycles
    output = do_compute(discard);
    sched->lastExecutionKv.Set(loc, _ssdfg->cur_cycle());
    _self_bits.test(output, _back_array, discard, pred, reset);

    compute_dump << output;

  } else {
    compute_dump << "???";
    _output_vals.resize(values().size());
  }

  // TODO/FIXME: change to all registers
  if (reset) {
    for (size_t i = 0; i < _reg.size(); ++i) _reg[i] = 0;
  }

  DEBUG(COMP) << compute_dump.str()
              << (_invalid ? " invalid " : " valid ") << reason.str() << " "
              << (discard ? " and output discard!" : "");

  for (size_t i = 0; i < _back_array.size(); ++i) {
    if (_back_array[i]) {
      DEBUG(COMP) << "backpressure on " << i << " input\n";
    } else {
      _ops[i].pop();
    }
  }

  _invalid |= discard;

  // TODO(@were): We need a better name for this flag.
  if (!_invalid || !getenv("DSCDIVLD")) {
    for (size_t i = 0; i < values().size(); ++i) {
      values()[i]->push(_output_vals[i], !_invalid, lat_of_inst());
    }
  }
  // std::cerr << _ssdfg->cur_cycle() << ": " << name() << " (" << loc.first << ", " <<
  // loc.second << ") issued!" << std::endl;

  for (auto elem : values()) {
    if (!elem->forward(true)) return;
  }

  for (auto elem : values()) {
    assert(elem->forward(false));
  }
}

bool SSDfgOperand::ready() {
  if (edges.empty()) {
    return true;
  }
  for (size_t i = 0; i < fifos.size(); ++i) {
    if (fifos[i].empty()) {
      DEBUG(FORWARD) << "no element!";
      return false;
    }
    if (edges[i]->use()->_ssdfg->cur_cycle() < fifos[i].front().available_at) {
      DEBUG(FORWARD) << "time away: " << edges[i]->use()->_ssdfg->cur_cycle()
                     << " < " << fifos[i].front().available_at;
      return false;
    }
  }
  return true;
}

bool SSDfgValue::forward(bool attempt) {
  if (fifo.empty()) return false;
  simulation::Data data(fifo.front());
  for (auto user : _uses) {
    int j = 0;
    for (auto& operand : user->use()->ops()) {
      ++j;
      for (size_t i = 0; i < operand.edges.size(); ++i) {
        auto edge = operand.edges[i];
        if (edge == user) {
          if ((int)operand.fifos[i].size() + 1 < edge->buffer_size()
              /*FIXME: The buffer size should be something more serious*/) {
            if (!attempt) {
              simulation::Data entry(ssdfg()->cur_cycle() + edge->delay(), data.value, data.valid);
              DEBUG(FORWARD) << ssdfg()->cur_cycle() << ": " << name()
                             << " pushes " << data.value << "(" << data.valid << ")" << "to "
                             << user->use()->name() << "'s " << j << "th operand "
                             << operand.fifos[i].size() + 1 << "/" << edge->buffer_size()
                             << " in " << edge->delay() << " cycles(" << entry.available_at << ")";
              operand.fifos[i].push(entry);
            }
          } else {
            return false;
          }
        }
      }
    }
  }
  if (!attempt) {
    fifo.pop();
  }
  return true;
}

void SSDfgVecInput::forward(Schedule*) {
  if (is_temporal()) {
    if (!values()[current_]->forward(true)) {
      return;
    }
    values()[current_]->forward(false);
    current_ = (current_ + 1) % ((int)values().size());
    return;
  }

  for (auto& elem : values()) {
    if (!elem->forward(true)) return;
  }
  for (auto& elem : values()) {
    assert(elem->forward(false));
  }
}

int SSDfg::forward(bool asap, Schedule* sched) {
  clear_issued();
  std::vector<bool> group_ready(true, num_groups());
  int old[2] = {dyn_issued[0], dyn_issued[1]};
  if (!asap) {
    for (auto &node : _nodes) {
      if (!group_ready[node->group_id()]) {
        continue;
      }
      if (auto vi = dynamic_cast<SSDfgVecInput*>(node)) {
        bool ready = true;
        for (auto value : vi->values()) {
          if (!value->forward(true)) {
            ready = false;
            break;
          }
        }
        if (!ready) {
          group_ready[node->group_id()] = false;
        }
      }
    }
  }
  for (auto elem : nodes<SSDfgNode*>()) {
    if (auto vec = dynamic_cast<SSDfgVec*>(elem)) {
      if (asap || group_ready[elem->group_id()]) {
        elem->forward(sched);
      }
    } else {
      elem->forward(sched);
    }
  }
  ++_cur_cycle;
  return (dyn_issued[0] - old[0]) + (dyn_issued[1] - old[1]);
}

bool SSDfgVecInput::can_push() {
  for (auto elem : values()) {
    if (!elem->fifo.empty()) {
      return false;
    }
  }
  return true;
}

void SSDfgVecOutput::pop(std::vector<uint64_t>& data, std::vector<bool>& data_valid) {
  for (auto& operand : _ops) {
    data.push_back(operand.poll());
    data_valid.push_back(operand.predicate());
    operand.pop();
  }
}

bool SSDfgVecOutput::can_pop() {
  int j = 0;
  for (auto& elem : _ops) {
    ++j;
    if (!elem.ready()) {
      DEBUG(FORWARD) << ssdfg()->cur_cycle() << ": Cannot pop because " << j;
      return false;
    }
  }
  return true;
}

uint64_t SSDfgOperand::poll() {
  assert(ready());
  uint64_t res = 0;
  for (int i = fifos.size() - 1; i >= 0; --i) {
    uint64_t full = ~0ull >> (64 - edges[i]->bitwidth());
    uint64_t sliced = (fifos[i].front().value >> edges[i]->l()) & full;
    res = (res << edges[i]->bitwidth()) | sliced;
  }
  return res;
}

double SSDfg::estimated_performance(Schedule* sched, bool verbose) {
  std::vector<std::vector<double>> bw(num_groups(), std::vector<double>(2, 0));
  std::vector<double> coef(num_groups(), (double)1.0);

  for (auto& elem : nodes<SSDfgVecInput*>()) {
    if ((elem->meta.op >> (int) dsa::dfg::MetaPort::Operation::Read & 1) &&
        elem->meta.source != dsa::dfg::MetaPort::Data::Unknown) {
      bw[elem->group_id()][elem->meta.source == dsa::dfg::MetaPort::Data::SPad] +=
          elem->get_vp_len() * elem->get_port_width() / 8;
    } else if ((elem->meta.op >> (int) dsa::dfg::MetaPort::Operation::IndRead & 1) ||
               (elem->meta.op >> (int) dsa::dfg::MetaPort::Operation::IndWrite) & 1) {
      if (sched->ssModel()->indirect() < 1) {
        coef[elem->group_id()] = 0.1;
      }
    } else if (elem->meta.op >> (int) dsa::dfg::MetaPort::Operation::Atomic & 1) {
      if (sched->ssModel()->indirect() < 2) {
        coef[elem->group_id()] = 0.1;
      }
    }
    coef[elem->group_id()] *= elem->meta.cmd;
  }

  std::vector<int> inst_cnt(num_groups(), 0);
  for (auto& elem : nodes<SSDfgInst*>()) {
    ++inst_cnt[elem->group_id()];
  }

  double memory_bw = 64 * sched->ssModel()->io_ports;
  std::vector<double> bw_coef(num_groups(), 1.0);
  for (int i = 0; i < num_groups(); ++i) {
    for (int j = 0; j < 2; ++j) {
      //std::cout << "memory bandwidth: " << memory_bw << " ? " << bw[i][j] << std::endl;
      if (bw[i][j] > memory_bw) {
        bw_coef[i] = std::min(bw_coef[i], memory_bw / bw[i][j]);
      }
    }
  }

  std::vector<double> nmlz_freq;
  for (int i = 0; i < num_groups(); ++i) {
    nmlz_freq.push_back(group_prop(i).frequency);
  }
  double nmlz = *std::max_element(nmlz_freq.begin(), nmlz_freq.end());
  for (int i = 0; i < num_groups(); ++i) {
    nmlz_freq[i] /= nmlz;
  }

  std::vector<double> rec_lat(num_groups(), 0.0);
  std::vector<double> rec_hide(num_groups(), 0.0);
  for (auto& elem : nodes<SSDfgVecOutput*>()) {
    if (elem->meta.dest == dsa::dfg::MetaPort::Data::LocalPort) {
      double lat = sched->latOf(elem);
      double hide = elem->meta.conc / group_prop(elem->group_id()).unroll;
      if (lat > hide) {
        rec_lat[elem->group_id()] = lat;
        rec_hide[elem->group_id()] = hide;
      }
    }
  }

  double overall = 0.0;

  for (int i = 0; i < num_groups(); ++i) {
    double v =
        std::min(bw_coef[i], rec_hide[i] / rec_lat[i]) * inst_cnt[i] * nmlz_freq[i];
    if (verbose) {
      std::cout << "[Group " << i << "] Freq: " << group_prop(i).frequency
                << ", #Insts:" << inst_cnt[i] << ", Memory: " << bw[i][0]
                << ", SPad: " << bw[i][1] << ", Rec: " << rec_hide[i] << "/" << rec_lat[i]
                << ", Overall: " << v
                << ", Performance Coef: " << coef[i]
                << std::endl;
    }
    overall += v * coef[i];
  }

  return overall;
}

void SSDfgOperand::pop() {
  CHECK(ready());
  for (auto& elem : fifos) {
    elem.pop();
  }
}


std::string SSDfgValue::name() { return node()->name() + "." + std::to_string(index()); }

namespace dsa {
namespace dfg {


const char* MetaPort::DataText[] = {
    "memory",
    "spad",
    "localport",
    "remoteport",
};

const char* MetaPort::OperationText[] = {
    "read", "write", "indread", "indwrite", "atomic",
};

CompileMeta::CompileMeta(const MetaPort& meta, SSDfgVec* parent)
    : MetaPort(meta), parent(parent) {
  assert(parent);
  if (dest == Data::LocalPort && !dest_port.empty()) {
    bool found = false;
    for (auto iv : parent->ssdfg()->nodes<SSDfgVecInput*>()) {
      if (iv->name() == dest_port) {
        destination = iv;
        found = true;
      }
    }
    CHECK(found) << "No destination found!";
  } else {
    destination = nullptr;
  }
}

}  // namespace dfg
}