#include "model_parsing.h"
#include "ssdfg.h"
#include <vector>
#include <set>
#include <iomanip>
#include <string>
#include <list>
#include "schedule.h"
#include "dfg-parser.tab.h"

using namespace std;
using namespace SS_CONFIG;

/// { misc

void checked_system(const char* command) {
  int ret = system(command);
  if(ret) {
    std::cout << "Command: \"" << command 
              << "\" failed with return value: " << ret << "\n";
  }
}

/// }

/// { SSDfgEdge

std::string SSDfgEdge::name() {
  std::stringstream ss;
  ss << def()->name() << "->" << use()->name();
  return ss.str();
}

// -- Gams names --
std::string SSDfgEdge::gamsName() {
  std::stringstream ss;
  ss << def()->gamsName() << "_" << use()->gamsName() << "i" << _ID ;
  return ss.str();
}

SSDfgEdge::SSDfgEdge(SSDfgNode* def, SSDfgNode* use, EdgeType etype, SSDfg* ssdfg, int l, int r) :
   _ID(ssdfg->inc_edge_id()), _ssdfg(ssdfg), _etype(etype),  _l(l), _r(r) {
  nodes[0] = def;
  nodes[1] = use;
}

void SSDfgEdge::compute_after_push(bool print, bool verif){
  if(_data_buffer.size()==1){
    use()->inc_inputs_ready_backcgra(print, verif);
  }
}

void SSDfgEdge::compute_after_pop(bool print, bool verif){
  if(_data_buffer.size()>0){
    use()->inc_inputs_ready_backcgra(print, verif);
  }
}

SSDfgNode *SSDfgEdge::def() const {
  return nodes[0];
}

SSDfgNode *SSDfgEdge::use() const {
  return nodes[1];
}

SSDfgNode *SSDfgEdge::get(int x) const {
  return nodes[x];
}

int SSDfgEdge::id() {
  return _ID;
}

uint64_t SSDfgEdge::extract_value(uint64_t val) {
  if (_r - _l + 1 == 64) { //this is special cased because << 64 is weird in c
    return val;
  } else {
    uint64_t mask = (((uint64_t) 1 << bitwidth()) - 1);
    return (val >> _l) & mask; // little endian machine
  }
}

uint64_t SSDfgEdge::get_value() {
  assert(def());
  return extract_value(def()->get_value()); //the whole value
}

void SSDfgEdge::push_in_buffer(uint64_t v, bool valid, bool print, bool verif) {
  assert(_data_buffer.size() < buf_len && "Trying to push in full buffer\n");
  _data_buffer.push(std::make_pair(v, valid));
  compute_after_push(print, verif);
}

bool SSDfgEdge::is_buffer_full() {
  return (_data_buffer.size() == buf_len);
}

bool SSDfgEdge::is_buffer_empty() {
  return _data_buffer.empty();
}

uint64_t SSDfgEdge::get_buffer_val() {
  assert(!_data_buffer.empty());
  return extract_value(_data_buffer.front().first);
}

bool SSDfgEdge::get_buffer_valid() {
  assert(!_data_buffer.empty());
  return _data_buffer.front().second;
}


void SSDfgEdge::pop_buffer_val(bool print, bool verif) {
  assert(!_data_buffer.empty() && "Trying to pop from empty queue\n");
  // std::cout << "came here to pop buffer val\n";
  _data_buffer.pop();
  compute_after_pop(print, verif);
}

int SSDfgEdge::bitwidth() { return _r - _l + 1; }

int SSDfgEdge::l() { return _l; }

int SSDfgEdge::r() { return _r; }

void SSDfgEdge::reset_associated_buffer() {
  decltype(_data_buffer) empty;
  std::swap(_data_buffer, empty);
}

/// }


/// { SSDfgOperand

SSDfgOperand::SSDfgOperand(SSDfgEdge *e) : edges{e} {}

SSDfgOperand::SSDfgOperand(std::vector<SSDfgEdge *> es) : edges(es) {}

SSDfgOperand::SSDfgOperand(uint64_t imm_) : imm(imm_) {}

SSDfgEdge * SSDfgOperand::get_first_edge() const {
  return edges.empty() ? nullptr : edges[0];
}

void SSDfgOperand::clear() {
  imm = 0;
  edges.clear();
}

bool SSDfgOperand::is_ctrl() {
  for (SSDfgEdge *e : edges) {
    if (e->etype() == SSDfgEdge::ctrl) {
      return true;
    }
  }
  return false;
}

bool SSDfgOperand::is_imm() { return edges.empty(); }

bool SSDfgOperand::is_composed() { return edges.size() > 1; }


//Functions which manipulate dynamic state
uint64_t SSDfgOperand::get_value() { //used by simple simulator
  uint64_t base = imm;
  int cur_bit_pos = 0;
  for (SSDfgEdge *e : edges) {
    base |= e->get_value() << cur_bit_pos;
    cur_bit_pos += e->bitwidth();
  }
  assert(cur_bit_pos <= 64); // max bitwidth is 64
  return base;
}

uint64_t SSDfgOperand::get_buffer_val() { //used by backcgra simulator
  uint64_t base = imm;
  int cur_bit_pos = 0;
  for (SSDfgEdge *e : edges) {
    base |= e->get_buffer_val() << cur_bit_pos;
    cur_bit_pos += e->bitwidth();
  }
  assert(cur_bit_pos <= 64); // max bitwidth is 64
  return base;
}

uint64_t SSDfgOperand::get_buffer_valid() { //used by backcgra simulator
  for (SSDfgEdge *e : edges) {
    if (!e->get_buffer_valid()) {
      return false;
    }
  }
  return true;
}

uint64_t SSDfgOperand::is_buffer_empty() { //used by backcgra simulator
  for (SSDfgEdge *e : edges) {
    if (e->is_buffer_empty()) {
      return true;
    }
  }
  return false;
}

void SSDfgOperand::pop_buffer_val(bool print, bool verif) {
  for (SSDfgEdge *e : edges) {
    e->pop_buffer_val(print, verif);
  }
}

bool SSDfgOperand::valid() {
  for(SSDfgEdge *e : edges) {
    SSDfgNode *n = e->def();
    if(n->invalid()) return false;
  }
  return true;
}

/// }


/// { SSDfgNode

uint64_t SSDfgNode::invalid() {
  return _invalid;
}

//Add edge to operand in least to most significant bit order
void SSDfgNode::addOperand(unsigned pos, SSDfgEdge *e) {
  assert(pos <= 4);
  if (_ops.size() <= pos) {
    _ops.resize(pos + 1);
  }
  _ops[pos].edges.push_back(e);
  _inc_edge_list.push_back(e);
}

void SSDfgNode::addOutEdge(SSDfgEdge *edge) {
  _uses.push_back(edge);
}

void SSDfgNode::validate() {
  for (size_t i = 0; i < _ops.size(); ++i) {
    SSDfgEdge *edge = _inc_edge_list[i];
    assert(edge == nullptr || edge->use() == this);
  }
  for (size_t i = 0; i < _uses.size(); ++i) {
    SSDfgEdge *edge = _uses[i];
    assert(edge->def() == this);
  }
}

void SSDfgNode::remove_edge(SSDfgNode *node, int idx) {
  for (unsigned i = 0; i < _ops.size(); ++i) {
    for (auto I = _ops[i].edges.begin(), E = _ops[i].edges.end(); I != E; ++i) {
      if ((*I)->get(idx) == node) {
        _ops[i].edges.erase(I);
        return;
      }
    }
  }
  assert(false && "edge was not found");
}

void SSDfgNode::removeIncEdge(SSDfgNode *orig) {
  remove_edge(orig, 0);
}

void SSDfgNode::removeOutEdge(SSDfgNode *dest) {
  remove_edge(dest, 1);
}

bool SSDfgNode::is_temporal() {
  return _ssdfg->group_prop(_group_id).is_temporal;
}


void SSDfgNode::push_buf_dummy_node(){
    _ssdfg->push_buf_transient(this->first_inc_edge(), true, 1); // can it be immediate?
}

// TODO: free all buffers and clear inputs ready
void SSDfgNode::reset_node() {
  _inputs_ready = 0;
  for (auto in_edges: _inc_edge_list) {
    (*in_edges).reset_associated_buffer();
  }
  for (auto out_edges: _uses) {
    (*out_edges).reset_associated_buffer();
  }

}

SSDfgNode::SSDfgNode(SSDfg* ssdfg, V_TYPE v) :
      _ssdfg(ssdfg), _ID(ssdfg->inc_node_id()),  _vtype(v) {
  _group_id = _ssdfg->num_groups() - 1;

}

SSDfgNode::SSDfgNode(SSDfg* ssdfg, V_TYPE v, const std::string &name) :
        _ssdfg(ssdfg), _ID(ssdfg->inc_node_id()),  _name(name), _vtype(v)  {
  _group_id = _ssdfg->num_groups() - 1;

}

int SSDfgNode::inc_inputs_ready_backcgra(bool print, bool verif) {
  if(++_inputs_ready == num_inc_edges()) {
    _ssdfg->push_ready_node(this);
  }
  return 0;
}

SSDfgEdge *SSDfgNode::getLinkTowards(SSDfgNode *to) {
  auto pred = [to] (SSDfgEdge *e) -> bool { return e->use() == to; };
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

void SSDfgNode::depInsts(std::vector<SSDfgInst *> &insts) {
  for (auto it : _uses) {
    SSDfgNode *use = it->use();
    if (std::find(insts.begin(), insts.end(), use) != insts.end()) {
      use->depInsts(insts);
    }
  }
}

bool SSDfgNode::get_bp() {
  bool bp = false;
  for (auto elem : _uses)
    if (elem->is_buffer_full())
      bp = true;
  return bp;
}

void SSDfgNode::set_node(uint64_t v, bool valid, bool avail, bool print, bool verif) {
  set_outputnode(v, valid, avail);

  // no need to do anything for output node
  if (this->num_out() == 0)
    return;
  if (avail) {
    if (!get_bp()) {
      for (auto iter = _uses.begin(); iter != _uses.end(); iter++) {
        (*iter)->push_in_buffer(v, valid, print, verif);
      }
      _avail = false;
    } else {
      set_value(v, valid, avail, 1); // after 1 cycle
    }
  }
}

int SSDfgNode::inc_inputs_ready(bool print, bool verif) {
  if (++_inputs_ready == num_inc_edges()) {
    int num_computed = compute(print, verif);
    _inputs_ready = 0;
    return num_computed;
  }
  return 0;
}

/// }


/// { Parsing data structure

void CtrlBits::set(uint64_t val, Control b) {
  int loc = val * Total + b;
  assert(loc >= 0 && loc < 64);
  mask |= (1 << loc);
}

bool CtrlBits::test(uint64_t val, Control b) {
  int loc = val * Total + b;
  assert(loc >= 0 && loc < 64);
  return (mask >> loc) & 1;
}

void CtrlBits::test(uint64_t val, std::vector<bool> &back_array, bool &discard,
                    bool &predicate, bool &reset) {
  if (!mask)
    return;
  back_array[0] = back_array[0] || test(val, CtrlBits::B1);
  back_array[1] = back_array[1] || test(val, CtrlBits::B2);
  discard = discard || test(val, CtrlBits::Discard);
  predicate = predicate && !(test(val, CtrlBits::Abstain));
  reset = reset || test(val, CtrlBits::Reset);
}

CtrlBits::CtrlBits(const std::map<int, std::vector<std::string>> &raw) {
  for (auto &elem : raw)
    for (auto &s : elem.second)
      set(elem.first, str_to_enum(s));
}

void ControlEntry::set_flag(const std::string &s) {
  if (s == "pred") {
    flag = SSDfgEdge::ctrl_true;
  } else if (s == "inv_pred") {
    flag = SSDfgEdge::ctrl_false;
  } else if (s == "control" || s == "self") {
    flag = SSDfgEdge::ctrl;
  } else {
    printf("qualifier: %s unknown", s.c_str());
    assert(0 && "Invalid argument qualifier");
  }
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

void SSDfgInst::depInsts(std::vector<SSDfgInst *> &insts) {
  insts.push_back(this);
  for (auto it = _uses.begin(); it != _uses.end(); it++) {
    SSDfgNode *use = (*it)->use();
    if (std::find(insts.begin(), insts.end(), use) != insts.end()) {
      use->depInsts(insts);
    }
  }
}

std::string SSDfgInst::name() {
  std::stringstream ss;
  ss << _name << "(" << SS_CONFIG::name_of_inst(_ssinst) << " " << id() << ")";
  return ss.str();
}

/// }

/// { SSDfgIO

std::string SSDfgInput::name() {
  std::stringstream ss;
  ss << _name << "(input " << id() << ")";
  return ss.str();
}

int SSDfgInput::compute_backcgra(bool print, bool verif) {
  int num_computed = 0;
  for (auto iter = _uses.begin(); iter != _uses.end(); iter++) {
    SSDfgNode *use = (*iter)->use();
    num_computed += use->inc_inputs_ready_backcgra(print, verif);
  }
  return num_computed;
}

int SSDfgInput::compute(bool print, bool verif) {
  int num_computed = 0;
  for (auto iter = _uses.begin(); iter != _uses.end(); iter++) {
    SSDfgNode *use = (*iter)->use();

    num_computed += use->inc_inputs_ready(print, verif);
  }
  return num_computed;
}

std::string SSDfgOutput::name() {
  std::stringstream ss;
  ss << _name << "(output " << id() << ")";
  return ss.str();
}

std::string SSDfgVecInput::gamsName() {
  std::stringstream ss;
  ss << "IPV_" << _name;
  return ss.str();
}

std::string SSDfgVecOutput::gamsName() {
  std::stringstream ss;
  ss << "OPV_" << _name;
  return ss.str();
}

bool SSDfgVecInput::backPressureOn() {
    return false;
}

/// }

/// { EntryTable

void EntryTable::set(const std::string &s, ParseResult *pr, bool override) {
  if (!symbol_table_.count(s))
    symbol_table_[s] = pr;
  else if (override)
    symbol_table_[s] = pr;
  else {
    std::cerr << "duplicated symbol: " << s << std::endl;
    assert(0 && "Add existing symbol entrying w/o overriding");
  }
}

ParseResult *EntryTable::get_sym(const std::string &s) {
  assert(symbol_table_.count(s));
  return symbol_table_[s];
}
/// }


/// { SSDfg

std::vector<SSDfgInst*> &SSDfg::ordered_insts() {
  if (_orderedInsts.size() == 0) {
    std::set<SSDfgInst *> done_nodes;
    for (SSDfgOutput *out : _outputs) {
      if (SSDfgInst *producing_node = out->out_inst()) {
        order_insts(producing_node, done_nodes, _orderedInsts);
      }
    }
  }
  return _orderedInsts;
}

void SSDfg::order_insts(SSDfgInst* inst,
                 std::set<SSDfgInst*>& done_nodes,         //done insts
                 std::vector<SSDfgInst*>& ordered_insts) {

  if(done_nodes.count(inst)) {
    return;
  }

  //insert the new inst
  done_nodes.insert(inst);

  //incoming edges to a node
  int index=0;
  for(auto edge : inst->in_edges()) {
    if(!edge) {
      assert(inst->immSlot()==index);
      continue;
    }

    //if there is a defintion node
    if(SSDfgInst* op_inst = dynamic_cast<SSDfgInst*>(edge->def()) ) {
      order_insts(op_inst, done_nodes, ordered_insts);
      //recursive call until the top inst with the last incoming edge
    }
    ++index;
  }

  ordered_insts.push_back(inst);
}


void SSDfg::printGraphviz(const char *fname, Schedule *sched) {
  std::ofstream os(fname);
  assert(os.good());
  printGraphviz(os, sched);
  os.flush();
}

void SSDfg::check_for_errors() {
  printGraphviz("viz/error_check_dfg.dot");

  bool error = false;
  for (auto elem : _inputs) {
    if (elem->num_out() == 0) {
      cerr << "Error: No uses on input " << elem->name() << "\n";
      error = true;
    }
  }

  for (auto elem : _insts) {
    if (elem->num_out() == 0) {
      cerr << "Error: No uses on inst " << elem->name() << "\n";
      error = true;
    }
    if (elem->num_inc() == 0) {
      cerr << "Error: No operands on inst " << elem->name() << "\n";
      error = true;
    }
  }

  for (auto elem : _outputs) {
    if (elem->num_inc() == 0) {
      cerr << "Error: No operands on output " << elem->name() << "\n";
      error = true;
    }
  }

  assert(!error && "ERROR: BAD DFG");
}

// This function is called from the simulator to
int SSDfg::compute(bool print, bool verif, int g) {

  int num_computed=0;

  assert(g < (int)_vecInputGroups.size());
  for(unsigned i = 0; i < _vecInputGroups[g].size(); ++i) {
    SSDfgVecInput* vec = _vecInputGroups[g][i];
    for (auto elem : vec->vector()) {
      num_computed += elem->compute(print,verif); //calling some other compute
    }
  }
  return num_computed;
}

//Calculates max group throughput based on functional unit type
int SSDfg::maxGroupThroughput(int g) {
  int maxgt=0;

  assert(g < (int)_vecInputGroups.size());
  for(unsigned i = 0; i < _vecInputGroups[g].size(); ++i) {
    SSDfgVecInput* vec = _vecInputGroups[g][i];
    for (auto elem : vec->vector())
      maxgt=std::max(maxgt, elem->maxThroughput());
  }
  return maxgt;
}

void SSDfg::instsForGroup(int g, std::vector<SSDfgInst*>& insts) {
  assert(g < (int)_vecInputGroups.size());
  for(unsigned i = 0; i < _vecInputGroups[g].size(); ++i) {
    SSDfgVecInput* vec = _vecInputGroups[g][i];
    for (auto elem : vec->vector())
      elem->depInsts(insts);
  }
}

//Necessary for BOOST::SERIALIZATION
SSDfg::SSDfg() {}


//COMMA IF NOT FIRST
void CINF(std::ostream& os, bool& first) {
  if (first) {
    first = false;
  } else {
    os << ", ";
  }
}

bool conv_to_int(std::string s, uint64_t& ival) {
  try {
    ival = (uint64_t) stol(s, 0, 0);
    return true;
  } catch (...) {}
  return false;
}

bool conv_to_double(std::string s, double& dval) {
  try {
    dval = stod(s);
    return true;
  } catch (...) {}
  return false;
}

ParseResult *SSDfg::create_inst(std::string opcode, std::vector<ParseResult*> &args) {

  SS_CONFIG::ss_inst_t inst = inst_from_string(opcode.c_str());
  auto *dfg_inst = new SSDfgInst(this, inst);

  for (unsigned i = 0; i < args.size(); ++i) {
    if (auto data = dynamic_cast<ConstDataEntry*>(args[i])) {
      dfg_inst->setImm(data->data);
      dfg_inst->setImmSlot(i);
    } else if (auto ne =dynamic_cast<NodeEntry*>(args[i])) {
      connect(ne->node, dfg_inst, i, SSDfgEdge::data, ne->l, ne->r);
    } else if (auto ce = dynamic_cast<ConvergeEntry*>(args[i])) {
      for (auto elem : ce->entries) {
        if (auto ne = dynamic_cast<NodeEntry*>(elem))
          connect(ne->node, dfg_inst, i, SSDfgEdge::data, ne->l, ne->r);
      }
    } else if (auto ce = dynamic_cast<ControlEntry*>(args[i])) {
      // External control
      if (ce->controller) {
        auto ne = dynamic_cast<NodeEntry*>(ce->controller);
        connect(ne->node, dfg_inst, i, ce->flag, ne->l, ne->r);
        dfg_inst->set_ctrl_bits(ce->bits);
      } else {
        // Self control
        dfg_inst->set_self_ctrl(ce->bits);
      }
    } else {
      assert(false && "Invalide Node type");
    }
  }


  ParseResult *res = new NodeEntry(dfg_inst, 0, SS_CONFIG::bitwidth[inst] - 1);
  add<SSDfgInst>(dfg_inst);
  return res;
}

void SSDfg::set_pragma(std::string& c, std::string& s) {
  if (c == string("dfg")) {
    cout << "No pragmas yet for dfg\n";
  } else if (c == string("group")) {
    if (s == "temporal") {
      assert(!_groupProps.empty());
      _groupProps[_groupProps.size() - 1].is_temporal = true;
    }
  } else {
    cout << "Context \"" << c << "\" not recognized.";
  }
}

void SSDfg::start_new_dfg_group() {
  _vecInputGroups.emplace_back(std::vector<SSDfgVecInput*>());
  _vecOutputGroups.emplace_back(std::vector<SSDfgVecOutput*>());
  _groupProps.emplace_back(GroupProp());
}

SSDfg::SSDfg(string filename) : SSDfg() {
  string line;
  start_new_dfg_group();
  parse_dfg(filename.c_str(),this);
  calc_minLats();
  check_for_errors();
}

std::string SSDfgInput::gamsName() {
  std::stringstream ss;
  ss << "IV" << _ID;
  return ss.str();
}

std::string SSDfgOutput::gamsName() {
  std::stringstream ss;
  ss << "OV" << _ID;
  return ss.str();
}

std::string SSDfgInst::gamsName() {
  std::stringstream ss;
  ss << "FV" << _ID;
  return ss.str();
}
void SSDfgInst::setImmSlot(int i) {
  assert(i < 4);

  if ((int) _ops.size() <= i) {
    _ops.resize(i + 1);
  }

  _imm_slot = i;
}

uint64_t SSDfgInst::do_compute(bool &discard) {
  //This is a really cheezy way to do this, but i'm a little tired
  uint64_t output;
  switch(bitwidth()) {
    case 64:
      output=SS_CONFIG::execute64(_ssinst,_input_vals,&_reg[0],discard,_back_array);
      break;
    case 32:
      _input_vals_32.resize(_input_vals.size());
      for(int i = 0; i < (int)_input_vals.size(); ++i) {
        _input_vals_32[i] = _input_vals[i];
      }
      output=SS_CONFIG::execute32(_ssinst,_input_vals_32,(uint32_t*)&_reg[0],discard,_back_array);
      break;
    case 16:
      _input_vals_16.resize(_input_vals.size());
      for(int i = 0; i < (int)_input_vals.size(); ++i) {
        _input_vals_16[i] = _input_vals[i];
      }
      output=SS_CONFIG::execute16(_ssinst,_input_vals_16,(uint16_t*)&_reg[0],discard,_back_array);
      break;
    case 8:
      _input_vals_8.resize(_input_vals.size());
      for(int i = 0; i < (int)_input_vals.size(); ++i) {
        _input_vals_8[i] = _input_vals[i];
      }
      output=SS_CONFIG::execute8(_ssinst,_input_vals_8,(uint8_t*)&_reg[0],discard,_back_array);
      break;
    default:
      cout << "Weird bitwidth: " << bitwidth() << "\n";
      assert(0 && "weird bitwidth");
  }
  return output;
}

//compute:actual compute called from SSDfg class (slightly modify this)
int SSDfgInst::compute(bool print, bool verif) {
  assert(_ops.size() <=3);

  if(_input_vals.size()==0) {
    _input_vals.resize(_ops.size());
  }
  assert(_input_vals.size() <= _ops.size());

  if(print) {
    _ssdfg->dbg_stream() << name() << " (" << _ID << "): ";
  }

  _invalid=false;

  for(unsigned i = 0; i < _ops.size(); ++i) {
    if(immSlot() == (int)i) {
      _input_vals[i]=imm();
    } else {
      _input_vals[i] = _ops[i].get_value();
      if(!_ops[i].valid()) {
        _invalid=true;
      }
    }
    if(print) {
      _ssdfg->dbg_stream() << std::hex << _input_vals[i] << " ";
    }
  }

  _val = do_compute(_invalid);

  if(print) {
    _ssdfg->dbg_stream() << " = " << _val << "\n";
  }

  if(verif) {
    //if (!_verif_stream.is_open()) {
    //  checked_system("mkdir -p verif");
    //  _verif_stream.open(("verif/fu" + _verif_id + ".txt").c_str());
    //  assert(_verif_stream.is_open());
    //}
    //_verif_stream << hex << setw(16) << setfill('0') << _val << "\n";
    //_verif_stream.flush();
  }

  int num_computed = !_invalid;

  for(auto iter = _uses.begin(); iter != _uses.end(); iter++) {
      SSDfgNode* use = (*iter)->use();
      num_computed += use->inc_inputs_ready(print, verif); //recursively call compute
  }

  return num_computed;

}

// new compute for back cgra-----------------------------
int SSDfgInst::compute_backcgra(bool print, bool verif) {

  assert(_ops.size() <=3);

  if(_input_vals.size()==0) {
    _input_vals.resize(_ops.size());
  }

  // initializing back pressure
  _back_array.clear();
  _back_array.resize(_ops.size(), 0);

  if(print) {
    _ssdfg->dbg_stream() << name() << " (" << _ID << "): ";
  }

  bool discard(false), reset(false), pred(true);
  uint64_t output = 0;

  _invalid=false;

  for(unsigned i = 0; i < _ops.size(); ++i) {
    if(_ops[i].is_imm()) {
      _input_vals[i]=imm();
    } else if (_ops[i].is_ctrl()) {
      int c_val = _ops[i].get_buffer_val();
      // FIXME: confirm that this is correct
      _input_vals[i] = c_val;
      if (!_ops[i].get_buffer_valid()) {
        _invalid = true;
      } else {
        _ctrl_bits.test(c_val, _back_array, discard, pred, reset);
      }
    } else {
      _input_vals[i] = _ops[i].get_buffer_val();
      if(!_ops[i].get_buffer_valid()) {
        _invalid = true;
      }
    }
  }

  // we set this instruction to invalid
  // pred = 1; // for now--check why is it here?
  if (!pred) {
    _invalid=true;
  }

  if(!_invalid) { //IF VALID
    if(print)
      for (size_t i = 0; i < _ops.size(); ++i)
        _ssdfg->dbg_stream() << std::hex << _input_vals[i] << " ";

     _ssdfg->inc_total_dyn_insts();

    // Read in some temp value and set _val after inst_lat cycles
    output = do_compute(discard);
    _self_bits.test(output, _back_array, discard, pred, reset);

    if(print) {
      _ssdfg->dbg_stream() << " = " << output << "\n";
    }
  }

  //TODO/FIXME: change to all registers
  if (reset) {
    std::cout << "Reset the register file! " << _reg[0] << "\n";
    _reg[0] = 0;
  }

  if(print) {
    for (size_t i = 0; i < _back_array.size(); ++i)
      if (_back_array[i])
        std::cout << "backpressure on " << i << " input\n";
  }

  if(this->name() == ":Phi") {
    _ssdfg->inc_total_dyn_insts();
    assert(_input_vals.size()==3 && "Not enough input in phi node");
    for(unsigned i = 0; i < _ops.size(); ++i) {
      if(_ops[i].get_buffer_valid()) {
         output = _ops[i].get_buffer_val();
      }
    }

    if(print) {
      _ssdfg->dbg_stream() << " = " << output << "\n";
    }
    discard=false;
    _invalid=false;
  }

  _inputs_ready = 0;

  if(print) {
    std::cout << (_invalid ? "instruction invalid " : "instruction valid ")
              << (discard ? " and output discard!\n" : "\n");
  }

  if(!discard) {
    this->set_value(output, !_invalid, true, inst_lat(inst()));
  }


  // pop the inputs after inst_thr+back_press here
  int inst_throughput = inst_thr(inst());


  for(unsigned i = 0; i < _ops.size(); ++i) {
    if(_ops[i].is_imm()) {
      continue;
    }
    if (_back_array[i]) {
      _inputs_ready++;
      //_inputs_ready += _ops[i].edges.size();
    } else {
      //TODO:FIXME:CHECK:IMPORTANT
      //Iterate over edges in an operand and push transients for all of them?
      for(SSDfgEdge* e : _ops[i].edges) {
        _ssdfg->push_buf_transient(e, false, inst_throughput);
      }
    }
  }


  if(verif) {
    //if (!_verif_stream.is_open()) {
    //  checked_system("mkdir -p verif");
    //  _verif_stream.open(("verif/fu" + _verif_id + ".txt").c_str());
    //  assert(_verif_stream.is_open());
    //}
    //_verif_stream << hex << setw(16) << setfill('0') << _val << "\n";
    //_verif_stream.flush();
  }

  return 1;
}

// Virtual function-------------------------------------------------

int SSDfgInst::update_next_nodes(bool print, bool verif){
    return 0;
}

bool SSDfgVec::is_temporal() {
  return _ssdfg->group_prop(_group_id).is_temporal;
}

SSDfgIO::SSDfgIO(SSDfg *ssdfg, const std::string &name, SSDfgVec *vec_, SSDfgNode::V_TYPE v) : SSDfgNode(ssdfg, v, name), vec_(vec_) {

}

SSDfgVec::SSDfgVec(int len, const std::string &name, int id, SSDfg* ssdfg) : _name(name), _ID(id), _ssdfg(ssdfg) {
  _group_id = ssdfg->num_groups() - 1;
}

SSDfgVecOutput *SSDfgOutput::output_vec() {
  auto res = dynamic_cast<SSDfgVecOutput*>(vec_);
  assert(res);
  return res;
}

SSDfgVecInput *SSDfgInput::input_vec() {
  auto res = dynamic_cast<SSDfgVecInput*>(vec_);
  assert(res);
  return res;
}


void SSDfgNode::set_value(uint64_t v, bool valid, bool avail, int cycle) {
  _ssdfg->push_transient(this, v,valid, avail, cycle);
}

//------------------------------------------------------------------


void SSDfgNode::printGraphviz(ostream& os, Schedule* sched) {

  string ncolor = "black";
  os << "N" << _ID << " [ label = \"" << name();

  if(sched) {
    os << "\\n lat=" << sched->latOf(this)  << " ";
  }
  os << "min:" << _min_lat;

  if(sched) {
    auto p = sched->lat_bounds(this);
    os << "\\n bounds=" << p.first << " to " << p.second;
    os << "\\n vio=" << sched->vioOf(this);
  }

  os  << "\", color= \"" << ncolor << "\"]; ";

  os << "\n";

  //print edges
  for (auto e : _uses) {

    if(e->etype()==SSDfgEdge::data) {
       ncolor="black";
    } else if(e->etype()==SSDfgEdge::ctrl_true) {
       ncolor="blue";
    } else if(e->etype()==SSDfgEdge::ctrl_false) {
       ncolor="red";
    }

    SSDfgNode* n = e->use();
    os << "N" << _ID << " -> N" << n->_ID << "[ color=";
    os << ncolor;
    os << " label = \"";
    if(sched) {
      os << "l:" << sched->link_count(e)
         << "\\nex:" << sched->edge_delay(e)
         << "\\npt:" << sched->num_passthroughs(e);
    }
    os << e->l() << ":" << e->r();
    os << "\"];\n";
  }

  os << "\n";

}

//Connect two nodes in DFG
//assumption is that each operand's edges are
//added to in least to most significant order!
SSDfgEdge* SSDfg::connect(SSDfgNode* orig, SSDfgNode* dest, int slot,
                           SSDfgEdge::EdgeType etype, int l, int r) {
  assert(orig != dest && "we only allow acyclic dfgs");

  SSDfgEdge* new_edge = 0; //check if it's a removed edge first
  auto edge_it = removed_edges.find(make_pair(orig,dest));
  if(edge_it != removed_edges.end()) {
    new_edge = edge_it->second;
  } else {
    new_edge = new SSDfgEdge(orig, dest, etype, this, l, r);
  }

  dest->addOperand(slot,new_edge);
  orig->addOutEdge(new_edge);
  _edges.push_back(new_edge);

  return new_edge;
}

//Disconnect two nodes in DFG
void SSDfg::disconnect(SSDfgNode* orig, SSDfgNode* dest) {
  assert(orig != dest && "we only allow acyclic dfgs");

  dest->removeIncEdge(orig);
  orig->removeOutEdge(dest);
  for (auto it=_edges.begin(); it!=_edges.end(); it++) {
    if ((*it)->def() == orig && (*it)->use() == dest) {
        removed_edges[make_pair(orig,dest)] = *it;
        _edges.erase(it);
        return;
    }
  }
  assert(false && "edge was not found");
}

bool SSDfg::remappingNeeded() {
  if(dummy_map.empty()) {
    //Count the number of dummy nodes needed
    for (auto dfg_out : _outputs) {
      SSDfgInst* inst = dfg_out->out_inst();
      SSDfgNode* node = dfg_out->first_op_node();
      //if producing instruction is an input or
      // if producing instruction has more than one uses
      if (!inst || inst->num_out() > 1) {
        SSDfgInst* newNode = new SSDfgInst(this, SS_CONFIG::ss_inst_t::SS_Copy, true);
        //TODO: insert information about this dummy node
        disconnect(node, dfg_out);
        connect(node, newNode, 0, SSDfgEdge::data);
        connect(newNode, dfg_out, 0, SSDfgEdge::data);
        add<SSDfgInst>(newNode);
        dummy_map[dfg_out] = newNode;
      }
    }
  }
  return !dummy_map.empty();
}

void SSDfg::removeDummies() {
  _orderedInsts.clear(); //invalidate dummies

  for (auto Ii=_insts.begin(),Ei=_insts.end();Ii!=Ei;++Ii)  {
    SSDfgInst* inst = *Ii;
    if(inst->isDummy()) {
       SSDfgNode* input = inst->first_op_node();
       SSDfgNode* output = inst->first_use();
       disconnect(input,inst);
       disconnect(inst,output);
       connect(input,output,0,SSDfgEdge::data);
    }
  }

  for(auto i : dummy_map) {
    removeInst(i.second); //remove from list of nodes
  }

  dummies.clear();
  dummiesOutputs.clear();
  dummys_per_port.clear();
}


void SSDfg::remap(int num_HW_FU) {
  //First Disconnect any Dummy Nodes  (this is n^2 because of remove, but w/e)
  removeDummies();

  for (auto Iout=_outputs.begin(),Eout=_outputs.end();Iout!=Eout;++Iout)  {
    SSDfgOutput* dfg_out = (*Iout);
    SSDfgInst* inst = dfg_out->out_inst();
    SSDfgNode* node = dfg_out->first_op_node();
    bool not_composed = !dfg_out->first_operand().is_composed();

    if (not_composed && (!inst || inst->num_out() > 1) && ((rand()&3)==0) ) {
      //25% chance
      disconnect(node, dfg_out);
      SSDfgInst* newNode = dummy_map[dfg_out]; //get the one we saved earlier
      connect(node, newNode, 0, SSDfgEdge::data);
      connect(newNode, dfg_out, 0, SSDfgEdge::data);
      add<SSDfgInst>(newNode); //add to list of nodes
      dummies.insert(newNode);
      dummiesOutputs.insert(dfg_out);

      if ((int)_insts.size() + (int)dummies.size() > num_HW_FU) {
        //cerr <<"No more FUs left, so we can't put more dummy nodes,\n"
        //     <<"  so probabily we will face problems when it comes to fix timing!\n";
        break;
      }

    }
  }
}

//We may have forgotten which dummies we included in the DFG, if we went on to
//some other solution.  This function recalls dummies and reconnects things
//appropriately
void SSDfg::rememberDummies(std::set<SSDfgOutput*> d) {
  removeDummies();

  for (auto Iout=_outputs.begin(),Eout=_outputs.end();Iout!=Eout;++Iout)  {
    SSDfgOutput* dfg_out = (*Iout);
    SSDfgNode* node = dfg_out->first_op_node();

    if (d.count(dfg_out)) {
      disconnect(node, dfg_out);
      SSDfgInst* newNode = dummy_map[dfg_out]; //get the one we saved earlier
      connect(node, newNode, 0, SSDfgEdge::data);
      connect(newNode, dfg_out, 0, SSDfgEdge::data);
      add<SSDfgInst>(newNode); //add to list of nodes
      dummies.insert(newNode);
      dummiesOutputs.insert(dfg_out);
    }
  }
}

//TODO: @vidushi, does this clear all the transient state, in case we need to do a reset?
void SSDfg::reset_simulation_state() {
  for(auto& list : transient_values) {
    list.clear();
  }
  for(auto& list : buf_transient_values) {
    list.clear();
  }
  _complex_fu_free_cycle.clear();
  _ready_nodes.clear();
  for (auto in : _nodes) {
    (*in).reset_node();
  }
}

void SSDfg::printGraphviz(ostream& os, Schedule* sched)
{
  os << "Digraph G { \nnewrank=true;\n " ;

  //Insts
  for (auto insts : _insts) {
    insts->printGraphviz(os,sched);
  }

  //Inputs
  for (auto in : _inputs) {
    in->printGraphviz(os,sched);
  }

  //Outputs
  for (auto out : _outputs) {
    out->printGraphviz(os,sched);
  }

  int cluster_num=0;

  os << "\n";
  for(auto& i : _vecInputs) {
    os << "subgraph cluster_" << cluster_num++ << " {" ;
    for (auto ssin : i->vector()) {
      os << "N" << ssin->id() << " ";
    }
    os << "}\n";
  }

  for(auto& i : _vecOutputs) {
    os << "subgraph cluster_" << cluster_num++ << " {" ;
    for (auto ssout : i->vector()) {
      os << "N" << ssout->id() << " ";
    }
    os << "}\n";
  }
  os << "\n";


  os << "\t{ rank = same; ";
  for (auto in : _inputs)   { os << "N" << in->id() << " ";  }
  os << "}\n";

  os << "\t{ rank = same; ";
  for (auto out:_outputs)   { os << "N" << out->id() << " "; }
  os << "}\n";

  os << "}\n";
}

void SSDfg::calc_minLats() {
  list<SSDfgNode* > openset;
  set<bool> seen;
  for (auto elem : _inputs) {
    openset.push_back(elem);
    seen.insert(elem);
  }

  //populate the schedule object
  while(!openset.empty()) {
    SSDfgNode* n = openset.front();
    openset.pop_front();

    int cur_lat = 0;

    for(auto elem : n->in_edges()) {
      SSDfgNode* dn = elem->def();
      if(dn->min_lat() > cur_lat) {
        cur_lat = dn->min_lat();
      }
    }

    if(SSDfgInst* inst_n = dynamic_cast<SSDfgInst*>(n)) {
      cur_lat += inst_lat(inst_n->inst()) + 1;
    } else if(dynamic_cast<SSDfgInput*>(n)) {
      cur_lat=0;
    } else if(dynamic_cast<SSDfgOutput*>(n)) {
      cur_lat+=1;
    }

    n->set_min_lat(cur_lat);

    for(auto elem : n->uses()) {
      SSDfgNode* un = elem->use();

      bool ready = true;
      for(auto elem : un->in_edges()) {
        SSDfgNode* dn = elem->def();
        if(!seen.count(dn)) {
          ready = false;
          break;
        }
      }
      if(ready) {
        seen.insert(un);
        openset.push_back(un);
      }
    }
  }
}

//Gams related
void SSDfg::printGams(std::ostream& os,
                      std::unordered_map<string,SSDfgNode*>& node_map,
                      std::unordered_map<std::string,SSDfgEdge*>& edge_map,
                      std::unordered_map<std::string, SSDfgVec*>& port_map) {

  os << "$onempty\n";

  {
    bool is_first = true;
    os << "set v \"verticies\" \n /";   // Print the set of Nodes:
    for (auto elem : _nodes) {
      if (!is_first) os << ", ";
      os << elem->gamsName();
      assert(elem);
      node_map[elem->gamsName()] = elem;
      is_first = false;
    }
    os << "/;\n";
  }

  {
    bool is_first = true;
    os << "set inV(v) \"input verticies\" /";   // Print the set of Nodes:
    for (auto elem : _inputs) {
      if (!is_first)
        os << ", ";
      assert(elem);
      os << elem->gamsName();
      is_first = false;
    }
    os << "/;\n";
  }

  {
    bool is_first = true;
    os << "set outV(v) \"output verticies\" /";   // Print the set of Nodes:
    for (auto elem : _outputs) {
      if (!is_first)
        os << ", ";
      os << elem->gamsName();
      assert(elem);
      is_first = false;
    }
    os << "/;\n";
  }

  {
    os << "parameter minT(v) \"Minimum Vertex Times\" \n /";
    for (auto Ii = _nodes.begin(), Ei = _nodes.end(); Ii != Ei; ++Ii) {
      if (Ii != _nodes.begin()) os << ", ";
      SSDfgNode *n = *Ii;
      int l = n->min_lat();
      if (SSDfgInst *inst = dynamic_cast<SSDfgInst *>(n)) {
        l -= inst_lat(inst->inst());
      }
      os << n->gamsName() << " " << l;
    }
    os << "/;\n";
  }


  os << "set iv(v) \"instruction verticies\";\n";
  os << "iv(v) = (not inV(v)) and (not outV(v));\n";

  for(int i = 2; i < SS_NUM_TYPES; ++i) {
    ss_inst_t ss_inst = (ss_inst_t)i;

    os << "set " << name_of_inst(ss_inst) << "V(v) /";
    bool first=true;

    for (auto dfg_inst : _insts) {

      if(ss_inst == dfg_inst->inst()) {
        CINF(os,first);
        os << dfg_inst->gamsName();
      }
    }
    os << "/;\n";
  }

  bool first=true;
  os << "set pv(*) \"Port Vectors\" \n /";   // Print the set of port vertices:
  for(auto& i : _vecInputs) {
    CINF(os,first);
    os << i->gamsName() << " ";
    port_map[i->gamsName()]=i;
  }
  for(auto& i : _vecOutputs) {
    CINF(os,first);
    os << i->gamsName() << " ";
    port_map[i->gamsName()]=i;
  }
  os << "/;\n";

  first=true;
  os << "parameter VI(pv,v) \"Port Vector Definitions\" \n /";   // Print the set of port vertices mappings:
  for(auto& i : _vecInputs) {
    int ind=0;
    for (auto ssin : i->vector()) {
      CINF(os,first);
      os << i->gamsName() << "." << ssin->gamsName() << " " << ind+1;
    }
  }
  for(auto& i : _vecOutputs) {
    int ind=0;
    for (auto ssout : i->vector()) {
      CINF(os,first);
      os << i->gamsName() << "." << ssout->gamsName() << " " << ind+1;
    }
  }
  os << "/;\n";

  // -------------------edges ----------------------------
  os << "set e \"edges\" \n /";   // Print the set of edges:

  for (auto Ie=_edges.begin(),Ee=_edges.end();Ie!=Ee;++Ie)  {
    if(Ie!=_edges.begin()) os << ", ";
    os << (*Ie)->gamsName();
    edge_map[(*Ie)->gamsName()]=*Ie;
  }
  os << "/;\n";

  //create the kindC Set
  os << "set kindV(K,v) \"Vertex Type\"; \n";

  // --------------------------- Enable the Sets ------------------------
  os << "kindV('Input', inV(v))=YES;\n";
  os << "kindV('Output', outV(v))=YES;\n";

  for(int i = 2; i < SS_NUM_TYPES; ++i) {
    ss_inst_t ss_inst = (ss_inst_t)i;
    os << "kindV(\'" << name_of_inst(ss_inst) << "\', " << name_of_inst(ss_inst) << "V(v))=YES;\n";
  }

  // --------------------------- Print the linkage ------------------------
  os << "parameter Gve(v,e) \"vetex to edge\" \n /";   // def edges
  for (auto Ie=_edges.begin(),Ee=_edges.end();Ie!=Ee;++Ie)  {
    if(Ie!=_edges.begin()) os << ", ";

    SSDfgEdge* edge = *Ie;
    os << edge->def()->gamsName() << "." << edge->gamsName() << " 1";
  }
  os << "/;\n";

  os << "parameter Gev(e,v) \"edge to vertex\" \n /";   // use edges
  for (auto Ie=_edges.begin(),Ee=_edges.end();Ie!=Ee;++Ie)  {
    if(Ie!=_edges.begin()) os << ", ";

    SSDfgEdge* edge = *Ie;
    os << edge->gamsName() << "." << edge->use()->gamsName() << " 1";
  }
  os << "/;\n";

  os << "set intedges(e) \"edges\" \n /";   // Internal Edges
  first =true;
  for (auto Ie=_edges.begin(),Ee=_edges.end();Ie!=Ee;++Ie)  {
    SSDfgEdge* edge = *Ie;

    if(!dynamic_cast<SSDfgInput*>(edge->def()) && !dynamic_cast<SSDfgOutput*>(edge->use()) ) {
      if (first) first = false;
      else os << ", ";
      os << edge->gamsName();
    }
  }
  os << "/;\n";

  os << "parameter delta(e) \"delay of edge\" \n /";   // Print the set of edges:
  for (auto Ie=_edges.begin(),Ee=_edges.end();Ie!=Ee;++Ie)  {
    if(Ie!=_edges.begin()) os << ", ";
    SSDfgEdge* edge = *Ie;

    if(SSDfgInst* dfginst = dynamic_cast<SSDfgInst*>(edge->def())) {
       os << (*Ie)->gamsName() << " " << inst_lat(dfginst->inst());
    } else {
       os << (*Ie)->gamsName() << " " << "0";  //TODO: WHAT LATENCY SHOULD I USE??
    }
  }
  os << "/;\n";
}

void SSDfg::addVecOutput(const std::string &name, int len, EntryTable &syms, int width) {
  add_parsed_vec<SSDfgVecOutput>(name, len, syms, width);
}

void SSDfg::addVecInput(const std::string &name, int len, EntryTable &syms, int width) {
  add_parsed_vec<SSDfgVecInput>(name, len, syms, width);
}

double SSDfg::count_starving_nodes() {
  double count = 0;
  double num_unique_dfg_nodes = 0;
  for(auto it : _vecInputs) {
    SSDfgVecInput *vec_in = it;
    for(auto elem : vec_in->vector()) { // each scalar node
      // for(auto node : elem->uses()) {
      SSDfgNode *node = elem->first_use(); // FIXME: how does it work for dgra
      num_unique_dfg_nodes += 1/node->num_inc();
      if(node->num_inputs_ready()) { count += 1/node->num_inc(); }
      // }
    }
  }
  assert(num_unique_dfg_nodes>=0);
  if(num_unique_dfg_nodes==0) return 0; // What is this case? none nodes
  return count / num_unique_dfg_nodes;
}

bool SSDfg::push_vector(SSDfgVecInput *vec_in, std::vector<uint64_t> data, std::vector<bool> valid, bool print, bool verif) {
  if (!vec_in->is_temporal()) {
    if((int) data.size() != vec_in->get_vp_len()) {
      std::cout << "DATA FROM GEM5: " << data.size()
                << " VEC VP SIZE: " << vec_in->get_vp_len() << "\n";
      assert(false && "insufficient data available");
    }
  } else {
    //if((int) data.size() != vec_in->get_vp_len())
    //  return false;
  }

  int npart = 64/vec_in->get_port_width();
  int x = static_cast<int>(vec_in->get_vp_len());

  uint64_t val=0;

  for (int i = 0; i < (int)vec_in->vector().size(); ++i) {
    int n_times = std::min(npart, x-i*npart); 
    for(int j = n_times-1+i*npart; j >= i*npart; --j) { 
      val = data[j] | (val << vec_in->get_port_width());
    }
    SSDfgInput *ss_node = vec_in->at(i);
    ss_node->set_node(val, valid[i], true, print, verif);
    val = 0;
  }
  return true;
}

bool SSDfg::can_push_input(SSDfgVecInput *vec_in) {
  for (auto elem : vec_in->vector())
    if (elem->get_avail())
      return false;
  return true;
}

bool SSDfg::can_pop_output(SSDfgVecOutput *vec_out, unsigned int len) {

  assert(len > 0 && "Cannot pop 0 length output\n");
  if(vec_out->length() != len) {
    std::cout << "DATA FROM GEM5: " << len << " VEC VP SIZE: " << vec_out->length() << "\n";
  }
  assert(vec_out->length() == len
         && "asked for different number of outputs than the supposed length\n");

  size_t ready_outputs = 0;
  for (auto elem: vec_out->vector()) {
    SSDfgOperand &operand = elem->first_operand();
    if (!operand.is_buffer_empty()) {
      ready_outputs++;
    }
  }
  // std::cout << "ready outputs: " << ready_outputs << " len: " << len << "\n";
  if (ready_outputs == len) {
    return true;
  } else {
    return false;
  }
}

void SSDfg::pop_vector_output(SSDfgVecOutput *vec_out, std::vector<uint64_t> &data,
                              std::vector<bool> &data_valid, unsigned int len, bool print,
                              bool verif){
    assert(vec_out->length() == len && "insufficient output available\n");

    // we don't need discard now!
    for (auto elem: vec_out->vector()) {
      SSDfgOperand &operand = elem->first_operand();
      data.push_back(operand.get_buffer_val());
      data_valid.push_back(operand.get_buffer_valid()); // I can read different validity here
      operand.pop_buffer_val(print, verif);
    }
  }

int SSDfg::cycle(bool print, bool verif) {
  // int num_computed=0;
  for (auto it = buf_transient_values[cur_buf_ptr].begin();
       it != buf_transient_values[cur_buf_ptr].end();) {
    // set the values
    buffer_pop_info *temp = *it;
    SSDfgEdge *e = temp->e;
    e->pop_buffer_val(print, verif);

    it = buf_transient_values[cur_buf_ptr].erase(it);
  }

  for (auto it = transient_values[cur_node_ptr].begin();
       it != transient_values[cur_node_ptr].end();) {
    struct cycle_result *temp = *it;
    SSDfgNode *ss_node = temp->n;
    ss_node->set_node(temp->val, temp->valid, temp->avail, print, verif);
    it = transient_values[cur_buf_ptr].erase(it);
  }

  /// FIXME(@were): I think this can be simplified; I think this local variable can be deleted
  std::unordered_set<int> nodes_complete;


  for (auto I = _ready_nodes.begin(); I != _ready_nodes.end();) {
    SSDfgNode *n = *I;

    int node_id = n->node_id();
    bool should_fire = (node_id == -1) || (nodes_complete.count(node_id) == 0);

    unsigned inst_throughput = 1;

    //If inst_throughput cycles is great than 1, lets mark the throughput
    //make sure nobody else can also schedule a complex instruction during
    //that period
    if (should_fire && node_id != -1) {
      if (SSDfgInst *inst = dynamic_cast<SSDfgInst *>(n)) {
        inst_throughput = inst_thr(inst->inst());
        if (inst_throughput > 1) {
          if (_complex_fu_free_cycle[node_id] > _cur_cycle) {
            should_fire = false;
          }
        }
      }
    }

    if (should_fire && n->get_avail() == 0) {
      n->compute_backcgra(print, verif);
      I = _ready_nodes.erase(I);
      nodes_complete.insert(node_id);

      if (node_id != -1 && inst_throughput > 1) {
        _complex_fu_free_cycle[node_id] = _cur_cycle + inst_throughput;
      }

    } else {
      ++I;
    }
  }

  cur_buf_ptr = (cur_buf_ptr + 1) % get_max_lat();
  cur_node_ptr = (cur_node_ptr + 1) % get_max_lat();
  _cur_cycle = _cur_cycle + 1;
  int temp = _total_dyn_insts;
  _total_dyn_insts = 0;
  return temp;
}
/// }

using SS_CONFIG::SubModel;

// This function is aggressively different from those below two, so I decide to keep this for now.
std::vector<std::pair<int, int>> SSDfgInst::candidates(Schedule *sched, SSModel *ssmodel, int n) {
  SubModel *model = ssmodel->subModel();
  std::vector<std::pair<int, int>> spots;

  std::vector<ssfu*> &fus = model->nodes<MapsTo*>();
  //For Dedicated-required Instructions
  for (size_t i = 0; i < fus.size(); ++i) {
    ssfu *cand_fu = fus[i];
    if (cand_fu->fu_def() != nullptr && !cand_fu->fu_def()->is_cap(this->inst())) {
      continue;
    }

    if (!this->is_temporal()) {
      if (sched->isPassthrough(cand_fu))
        continue;
      //Normal Dedidated Instructions
      
      auto status = sched->dfg_nodes_of(cand_fu);
      uint8_t occupied(0);
      for (auto elem : status) {
        occupied |= (1 << elem.second->bitwidth() / 8) - 1;
      }
      for (int k = 0; k < 8; k += this->bitwidth() / 8) {
        int cnt = 0, tmp = cnt >> k & ((1 << this->bitwidth()) - 1);
        while (tmp) {
          ++cnt;
          tmp -= tmp & -tmp;
        }
        spots.emplace_back(i, k);
      }

    } else {
      //For temporaly-shared instructions
      //For now the approach is to *not* consume dedicated resources, although
      //this can be changed later if that's helpful.
      if ((int)sched->thingsAssigned(cand_fu) + 1 < cand_fu->max_util()) {
        spots.emplace_back(i, 0);
      }
    }
    
    
  }

  if (this->is_temporal() && spots.empty()) {
    cout << "Warning, no spots for" << this->name() << "\n";
  }

  std::random_shuffle(spots.begin(), spots.end());
  return spots;
}

template<typename T>
int wasted_width_impl(SSDfgVec *vec, Schedule *sched, SubModel *model) {
  int mapped_ = model->io_interf().vports(T::IsInput())[sched->vecPortOf(vec).second]->size();
  return mapped_ - vec->get_port_width();
}

int SSDfgVecInput::wasted_width(Schedule *sched, SubModel *model) {
  return wasted_width_impl<SSDfgVecInput>(this, sched, model);
}

int SSDfgVecOutput::wasted_width(Schedule *sched, SubModel *model) {
  return wasted_width_impl<SSDfgVecOutput>(this, sched, model);
}

bool SSDfgVec::yield(Schedule *sched, SubModel *model) {
  int wasted = wasted_width(sched, model);
  int r = rand() % wasted * wasted + get_port_width();
  return r <= wasted * wasted;
}

template<typename T>
std::vector<std::pair<int, int>> vec_candidate_impl(SSDfgVec *vec, Schedule *sched,
                                                    SSModel *model, int n) {
  std::vector<std::pair<int, int>> res;

  auto fcompare = [](const ssio_interface::EntryType &a, const int &val) {
      return a.second->size() < val;
  };

  std::vector<ssio_interface::EntryType> &vecs = model->subModel()->io_interf().vports_vec[T::IsInput()];
  // TODO(@were): Prestore this value somewhere to enhance the performance.
  int needed = vec->is_temporal() ? 1 : vec->vector().size();
  int l = std::lower_bound(vecs.begin(), vecs.end(), (int) vec->vector().size(),
                           fcompare) - vecs.begin();
  for (int i = l; i < vecs.size(); ++i) {
    auto &cand = vecs[i];
    if (!sched->vportOf(std::make_pair(T::IsInput(), cand.first))) {
      auto &ports = cand.second->port_vec();
      assert(ports.size() <= 32);
      std::vector<int> mask_ids;
      auto &nodes = model->subModel()->nodes<typename T::Scalar::MapsTo*>();
      for (int j = 0; j < ports.size(); ++j) {
        ssnode *node = nodes[ports[j]];
        if (!sched->dfgNodeOf(node)) {
          mask_ids.push_back(j);
        }
      }
      if (mask_ids.size() >= needed) {
        std::random_shuffle(mask_ids.begin(), mask_ids.end());
        int mask = 0;
        for (int j = 0; j < needed; ++j) {
          mask |= 1 << mask_ids[j];
        }
        res.emplace_back(i, mask);

        assert(i < vecs.size());

        if (res.size() > n) {
          break;
        }
      }
    }
  }

  std::random_shuffle(res.begin(), res.end());
  return res;
}

std::vector<std::pair<int, int>> SSDfgVecInput::candidates(Schedule *sched, SSModel *model, int n) {
  return vec_candidate_impl<SSDfgVecInput>(this, sched, model, n);
}

std::vector<std::pair<int, int>> SSDfgVecOutput::candidates(Schedule *sched, SSModel *model, int n) {
  return vec_candidate_impl<SSDfgVecOutput>(this, sched, model, n);
}


std::vector<std::pair<int, SS_CONFIG::ssnode*>>
SSDfgInst::ready_to_map(SS_CONFIG::SSModel *model, const std::pair<int, int> &cand) {
  return {std::make_pair(cand.second, model->subModel()->fu_list()[cand.first])};
}

template<typename T>
std::vector<std::pair<int, SS_CONFIG::ssnode*>>
ready_to_map_impl(SSDfgVec *vec, SS_CONFIG::SSModel *model, const std::pair<int, int> &cand) {
  auto &ports =
    model->subModel()->io_interf().vports_vec[T::IsInput()][cand.first].second->port_vec();

  std::vector<std::pair<int, SS_CONFIG::ssnode*>> res(vec->vector().size());

  int j = 0;
  for (size_t i = 0; i < ports.size(); ++i) {
    if (cand.second >> i & 1) {
      res[j++] = std::make_pair(0, (model->subModel()->nodes<typename T::Scalar::MapsTo*>()[ports[i]]));
    }
  }

  if (vec->is_temporal()) {
    for (size_t j = 1; j < res.size(); ++j) {
      res[j] = res[0];
    }
  } else {
    assert(j == res.size());
  }

  return res;
}

std::vector<std::pair<int, SS_CONFIG::ssnode*>>
SSDfgVecInput::ready_to_map(SS_CONFIG::SSModel *model, const std::pair<int, int> &cand) {
  return ready_to_map_impl<SSDfgVecInput>(this, model, cand);
}

std::vector<std::pair<int, SS_CONFIG::ssnode*>>
SSDfgVecOutput::ready_to_map(SS_CONFIG::SSModel *model, const std::pair<int, int> &cand) {
  return ready_to_map_impl<SSDfgVecOutput>(this, model, cand);
}
