#ifndef __SS_SCHEDULER_H__
#define __SS_SCHEDULER_H__

#include <stdlib.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

#include <boost/functional.hpp>

#include "ss-config/model.h"
#include "schedule.h"
#include "ssdfg.h"

#define MAX_ROUTE 100000000

using usec = std::chrono::microseconds;
using get_time = std::chrono::steady_clock;

int rand_bt(int s, int e);
int rand_bt_large(int s, int e);

template <typename T, typename U>
std::pair<T, U> operator+(const std::pair<T, U>& l, const std::pair<T, U>& r) {
  return {l.first + r.first, l.second + r.second};
}

template <typename T, typename U>
std::pair<T, U> operator-(const std::pair<T, U>& l, const std::pair<T, U>& r) {
  return {l.first - r.first, l.second - r.second};
}


// This class contains all info which you might want to remember about
class WorkloadSchedules {
public:
  std::vector<Schedule> sched_array; 
  
  float estimate_performance() {return 0;}
};

class SchedStats {
public:
  int lat = INT_MAX, latmis = INT_MAX;
  int agg_ovr = INT_MAX, ovr = INT_MAX, max_util = INT_MAX;
};

//Class which captures a codesign between hardware and software through
//scheduling.  Note here the the Model and all schedules are "owned" by
//the codesign instance, ie. they will be deleted when the class is deleted
//
// TODO: There are an infinite number of things to do here, starting with:
// 1. Explore full design space with randomness
// 2. 

class CodesignInstance {
  SSModel _ssModel;
  public:
  SSModel* ss_model() {return &_ssModel;}
  std::vector<WorkloadSchedules> workload_array;
  std::vector<Schedule*> res;

  CodesignInstance(SSModel* model) : _ssModel(*model) {
    verify();
  }

  // Check that everything is okay
  void verify() {
    for_each_sched([&](Schedule& sched) { 
      sched.validate();

      assert(&_ssModel == sched.ssModel());
      assert(_ssModel.subModel()->node_list().size() >= sched.node_prop().size());
      assert(_ssModel.subModel()->link_list().size() >= sched.link_prop().size());

      for(auto& ep : sched.edge_prop()) {
        for(auto& p : ep.links) {
          assert(p.second->id() < (int)_ssModel.subModel()->link_list().size());
          assert(p.second->id() < (int)sched.link_prop().size());
          assert(_ssModel.subModel()->link_list()[p.second->id()] == p.second);
        }
      }


    });
    for(auto& node : _ssModel.subModel()->node_list()) {
      assert(node->subnet_table().size() == node->out_links().size());
      if (auto fu = dynamic_cast<ssfu*>(node)) {
        assert(fu->fu_def());
      }
    }
    for(unsigned i = 0; i < _ssModel.subModel()->link_list().size(); ++i) {
      assert(_ssModel.subModel()->link_list()[i]->id() == (int)i);
    }
  }

  //Verify, plus also check:
  //1. there are no hangers (elements with no purpose, usually after delete)
  void verify_strong() {
    verify();
  }

  void add_random_edges_to_node(ssnode* n, int min_in, int max_in, 
                                           int min_out, int max_out) {
    auto* sub = _ssModel.subModel();

    if (sub->node_list().empty())
      return;

    int n_ins = rand() % (max_in - min_in) + min_in;
    for(int i = 0; i < n_ins; ++i) {
      int src_node_index = rand() % sub->nodes<ssnode*>().size();
      ssnode* src = sub->node_list()[src_node_index];
      if(src->is_output() || src == n) {
        i--;
        continue;
      }
      sub->add_link(src,n);
    }
    int n_outs = rand() % (max_out - min_out) + min_out;
    for(int i = 0; i < n_outs; ++i) {
      int dst_node_index = rand() % sub->node_list().size();
      ssnode* dst = sub->node_list()[dst_node_index];
      if(dst->is_input() || dst == n) {
        i--;
        continue;
      }
      sub->add_link(n,dst);
    }
  }

  template<typename T>
  bool delete_nodes(std::function<bool(T*)> f) {
    bool res = false;
    auto* sub = _ssModel.subModel();
    std::for_each(sub->nodes<T*>().begin(),
                  sub->nodes<T*>().end(),
                  [this, &res, f](T* n) {
                    if (f(n)) {
                      this->delete_hw(n);
                      res = true;
                    }
                  });
    return res;
  }

  //Delete FUs, Switches, and Vports that can't possible be useful
  bool delete_hangers() {
    bool deleted_something = false;

    auto* sub = _ssModel.subModel();


    for(ssfu* fu : sub->fu_list()) {
      if(fu->in_links().size() <=1 || fu->out_links().size() < 1) {
        delete_hw(fu);
        deleted_something=true;
      }
    }
    for(ssvport* ivport : sub->input_list()) {
      if(ivport->out_links().size() < 1) {
        delete_hw(ivport);
        deleted_something=true;
      }
    }
    for(ssvport* ovport : sub->output_list()) {
      if(ovport->in_links().size() < 1) {
        delete_hw(ovport);
        deleted_something=true;
      }
    }

    return deleted_something;
  }


  void make_random_modification() {
    auto* sub = _ssModel.subModel();

    if (rand() % 8 == 1) {
      int v = _ssModel.indirect();
      _ssModel.indirect(v ^ 1);
    }

    verify_strong();
    //Choose a set of Items to remove
    int n_items = rand() % 8 + 1;
    for(int i = 0; i < n_items; ++i) {
      int item_class = rand() % 100;
      if(item_class < 65) {
        //delete a link
        if(sub->link_list().empty()) continue;
        int index = rand() % sub->link_list().size();
        sslink* l = sub->link_list()[index];
        if(delete_linkp_list.count(l)) continue; //don't double delete
        delete_link(l);
      } else if(item_class < 80) {
        //delete a switch
        if(sub->switch_list().empty()) continue;
        int index = rand() % sub->switch_list().size();
        ssswitch* sw = sub->switch_list()[index];
        if(delete_nodep_list.count(sw)) continue; //don't double delete
        delete_hw(sw);
      } else if (item_class < 90) {
        //delete an FU
        if(sub->fu_list().empty()) continue;
        int index = rand() % sub->fu_list().size();
        ssfu* fu = sub->fu_list()[index];
        if(delete_nodep_list.count(fu)) continue; //don't double delete
        delete_hw(fu);
      } else if (item_class < 95) {
        //delete an VPort
        if(sub->input_list().size()==0) continue;
        int index = rand() % sub->input_list().size();
        ssvport* vport = sub->input_list()[index];
        if(delete_nodep_list.count(vport)) continue; //don't double delete
        delete_hw(vport);
      } else { // (item_class < 100) 
        //delete an VPort
        if(sub->output_list().size()==0) continue;
        int index = rand() % sub->output_list().size();
        ssvport* vport = sub->output_list()[index];
        if(delete_nodep_list.count(vport)) continue; //don't double delete
        delete_hw(vport);
      }
    }

    // TODO: delete some entries in routing tables?

    // Lets finalize the delete here so that the datastructre is consistent
    // again when we are adding things -- simpler
    finalize_delete();

    while(delete_hangers()) { 
      finalize_delete(); // TODO part of finalize delete isredundant now
    }

    verify_strong();


    //Modifiers
    n_items = rand() % 8;
    for(int i = 0; i < n_items; ++i) {
      int item_class = rand() % 100;  
      if(item_class < 40) {
        /*
        // This was for link flow-control, which I don't want any more
        int link_index  = rand_bt(0,sub->link_list().size());
        sslink* link = sub->link_list()[link_index];
        link->set_flow_control(!link->flow_control());

        // If we de-flow-controlled this edge, unassign any edges
        // which need control dependence (transitively)
        if(!link->flow_control()) {
          for_each_sched([&](Schedule& sched){ 
            for(int slot = 0; slot < sched.num_slots(link); ++slot) {
              for(auto& p : sched.dfg_edges_of(slot,link)) {
                if(p.first->use()->needs_ctrl_dep()) {
                  sched.unassign_dfgnode(p.first->def());
                  sched.unassign_dfgnode(p.first->use());
                }
              }
            }
          });
        }*/
        if (sub->node_list().empty()) continue;
        int node_index  = rand() % sub->node_list().size();
        ssnode* node = sub->node_list()[node_index];
        if(node->is_input() || node->is_output()) continue;
 
        node->set_flow_control(!node->flow_control());
        if(!node->flow_control()) {
          for_each_sched([&](Schedule& sched){ 
            for(int slot = 0; slot < sched.num_slots(node); ++slot) {
              for(auto& p : sched.dfg_nodes_of(slot,node)) {
                if(p.first->needs_ctrl_dep()) {
                  sched.unassign_dfgnode(p.first);
                }
              }
            }
          });
        }

      } else if(item_class < 65) {

        if (sub->fu_list().empty()) continue;
        // Modify FU delay-fifo depth
        int diff = rand() % 16 - 8;
        if(diff==0) continue;
        int fu_index  = rand() % sub->fu_list().size();
        ssfu* fu = sub->fu_list()[fu_index];
  
        int old_util = fu->max_util();

        int new_util = std::max(1,old_util+diff);
        if(diff < -4) new_util=1;

        if(old_util == 1 && new_util > 1) {
          fu->set_flow_control(true);
        }

        fu->set_max_util(new_util);

        // if we are constraining the problem, then lets re-assign anything
        // mapped to this FU
        if(new_util < old_util) {
          for_each_sched([&](Schedule& sched){ 
            for(int slot = 0; slot < sched.num_slots(fu); ++slot) {
              for(auto& p : sched.dfg_nodes_of(slot,fu)) {
                sched.unassign_dfgnode(p.first);
              }
            }
          });
        }

      } else if(item_class < 75) {

        if (sub->fu_list().empty()) continue;
        // Modify FU delay-fifo depth
        int diff = -(rand() % 3);
        int fu_index  = rand() % sub->fu_list().size();
        ssfu* fu = sub->fu_list()[fu_index];
        int new_delay_fifo_depth = std::max(0,fu->delay_fifo_depth() + diff);
        fu->set_delay_fifo_depth(new_delay_fifo_depth);

        // if we are constraining the problem, then lets re-assign anything
        // mapped to this FU
        if(diff < 0) {
          for_each_sched([&](Schedule& sched){ 
            for(int slot = 0; slot < sched.num_slots(fu); ++slot) {
              for(auto& p : sched.dfg_nodes_of(slot,fu)) {
                sched.unassign_dfgnode(p.first);
              }
            }
          });
        }

      } else if(item_class < 100) {
      
      }
    }

    //Items to add
    n_items = rand() % 8;
    for(int i = 0; i < n_items; ++i) {
      int item_class = rand() % 100;
      if(item_class < 65) {
        // Add a random link -- really? really
        if(sub->node_list().empty()) continue;
        int src_node_index = rand() % sub->node_list().size();
        int dst_node_index = rand() % sub->node_list().size();
        ssnode* src = sub->node_list()[src_node_index];
        ssnode* dst = sub->node_list()[dst_node_index];
        if(src->is_output() || dst->is_input() || src == dst) continue;

        //sslink* link = 
        sub->add_link(src,dst);
        //std::cout << "adding link: " << link->name() << "\n"; 
      } else if(item_class < 80) {
        // Add a random switch
        ssswitch* sw = sub->add_switch();       
        add_random_edges_to_node(sw,1,9,1,9); 

        //std::cout << "adding switch" << sw->name() << " ins/outs:" 
        //          << sw->in_links().size() << "/" << sw->out_links().size() << "\n";
      } else if (item_class < 90) {

         //Randomly pick an FU type from the set
         auto& fu_defs = _ssModel.fuModel()->fu_defs();
         if (fu_defs.empty()) continue;
         ssfu* fu = sub->add_fu();
         std::cout << "adding fu" << fu->id() << " ----------------------------\n";
         int fu_def_index = rand() % fu_defs.size();
         func_unit_def* def = &fu_defs[fu_def_index];
         fu->setFUDef(def);

         add_random_edges_to_node(fu,1,9,1,9); 
       
      } else if (item_class < 95) {
        // Add a random input vport
         ssvport* vport = sub->add_vport(true);
         add_random_edges_to_node(vport,0,1,5,12); 
         //std::cout << "adding input vport: " << vport->name() << "\n";
      } else { // (item_class < 100) 
        // Add a random output vport
         ssvport* vport = sub->add_vport(false);
         add_random_edges_to_node(vport,5,12,0,1); 
         //std::cout << "adding output vport: " << vport->name() << "\n";
      } 
    }

    verify_strong();

    for_each_sched([&](Schedule& sched) { 
      sched.allocate_space();
    });


    verify_strong();

    // TODO: add some entries in routing tables?

    //End this by making sure all the routing tables are generated
    for(auto* node : sub->node_list()) {
      node->setup_routing_memo();
    }

    verify_strong();
  }

  void for_each_sched(const std::function<void (Schedule&)>& f) {
    for(auto& ws : workload_array) {
      for(Schedule& sched : ws.sched_array) {
        f(sched);
      }
    }
  }

  //Overide copy constructor to enable deep copies
  CodesignInstance(const CodesignInstance& c) : _ssModel(c._ssModel) {
   
    //SSModel* copy_model = (SSModel*)&c._ssModel;
    //auto* copy_sub = copy_model->subModel();
    //std::cout << "copy from:" << copy_sub << " copy to:" << _ssModel.subModel() << "\n";
    //assert(_ssModel.subModel() != copy_sub);

    for(auto& node : _ssModel.subModel()->node_list()) {
      assert(node->subnet_table().size() == node->out_links().size());
    }

    workload_array = c.workload_array;

    for_each_sched([&](Schedule& sched){ 
      //replace this ssmodel with the copy 
      sched.swap_model(_ssModel.subModel());
      sched.set_model(&_ssModel);
    });

    for(auto& node : _ssModel.subModel()->node_list()) {
      assert(node->subnet_table().size() == node->out_links().size());
    }

  }

  //Delete link on every schedule
  void delete_link(sslink* link) {
    delete_link_list.push_back(link->id());
    delete_linkp_list.insert(link);

    // remove it from every schedule
    for_each_sched([&](Schedule& sched){ 
      for(int slot = 0; slot < sched.num_slots(link); ++slot) {
        for(auto& p : sched.dfg_edges_of(slot,link)) {
          //TODO: consider just deleteting the edge, and having the scheduler
          //try to repair the edge schedule -- this might save some time
          //sched.unassign_edge(p.first);
          sched.unassign_dfgnode(p.first->def());
          sched.unassign_dfgnode(p.first->use());
        }
      }
    });
  }

  //Delete fu on every schedule
  void delete_hw(ssfu* fu) {
    delete_fu_list.push_back(fu->id());
    delete_node(fu);
  }

  //Delete switch on every schedule
  void delete_hw(ssswitch* sw) {
    //std::cout << "Deleting Switch:" << sw->name() << "    ptr:" << sw << "\n";
    delete_sw_list.push_back(sw->id());
    delete_node(sw);
  }

  //Delete vector port on every schedule
  void delete_hw(ssvport* vport) {
   delete_vport_list.push_back(vport->id());
   delete_node(vport);
  }

  // This makes the delete consistent across model and schedules
  void finalize_delete() {
    //Grab a copy copy of all nodes
    auto* sub = _ssModel.subModel();
    std::vector<ssnode*> n_copy = sub->node_list(); //I hope this copies the list? 
    std::vector<sslink*> l_copy = sub->link_list(); //I hope this copies the list?

    verify(); 

    // Remove the elements from these lists
    sub->delete_nodes(delete_node_list); //these happen after above, b/c above uses id
    sub->delete_links(delete_link_list);

    // got to reorder all the links
    for_each_sched([&](Schedule& sched) { 
      sched.reorder_node_link(n_copy,l_copy);
    });

    verify(); 

    //finally, we just deleted a bunch of nodes/links, and we should
    //probably free the memory somehow?
    //that's why we tracked these datastructures
    for(auto* link : delete_linkp_list) {
      // we also need to tell the model to delete the link from its little lists
      link->orig()->unlink_outgoing(link);
      link->dest()->unlink_incomming(link);
      delete link;
    }
    for(auto* node : delete_nodep_list) delete node;

    verify();

    //finally finally, clear all datastructres used for deleting
    delete_nodep_list.clear();
    delete_linkp_list.clear();
  
    delete_node_list.clear();
    delete_link_list.clear();
    delete_fu_list.clear();
    delete_sw_list.clear();
    delete_vport_list.clear();

    verify(); 

  }

  std::pair<double, int> dse_sched_obj(Schedule* sched) {
    if (!sched)
      return {0.1, INT_MIN};
    //YES, I KNOW THIS IS A COPY OF SCHED< JUST A TEST FOR NOW
    SchedStats s;
    int num_left = sched->num_left();
    bool succeed_sched = (num_left == 0);

    sched->get_overprov(s.ovr, s.agg_ovr, s.max_util);
    sched->fixLatency(s.lat, s.latmis);
  
    int violation = sched->violation();
  
    int obj = s.agg_ovr * 1000 + violation * 200 + s.latmis * 200 + s.lat +
              (s.max_util - 1) * 3000 + sched->num_passthroughs();
    obj = obj * 100 + sched->num_links_mapped();

 
    int max_delay = sched->ssModel()->subModel()->fu_list()[0]->delay_fifo_depth();
    double performance = sched->ssdfg()->estimated_performance(sched, false);
    if (succeed_sched) {
      double eval = performance * (max_delay / (max_delay + s.latmis));
      eval /= (1.0 + s.ovr);
      return {eval, -obj};
    }

    return {-num_left, -obj};
  }

  float dse_obj() {
    std::pair<double, int> total_score = std::make_pair((double) 1.0, 0);
    res.resize(workload_array.size());

    bool meaningful = true;
    for(auto& ws : workload_array) {
      res[&ws - &workload_array[0]] = nullptr;
      std::pair<double, int> score = std::make_pair((double) 1e-3, INT_MIN);
      bool yes = false;
      for(Schedule& sched : ws.sched_array) {
        std::pair<double, int> new_score = dse_sched_obj(&sched);
        if(new_score > score) {
          score=new_score;
          res[&ws - &workload_array[0]] = &sched;
          yes = true;
        }
      }

      if (!yes)
        meaningful = false;
      //std::cout << "!!!: " << total_score.first << " * " << score.first << std::endl;
      total_score.first *= score.first;
    }

    if (!meaningful)
      return 0.0;
    //std::cout << "Before: " << total_score.first << std::endl;
    total_score.first = pow(total_score.first, (1.0 / (int) workload_array.size()));
    //std::cout << "After: " << total_score.first << std::endl;

    float obj = total_score.first * 1e6 / (_ssModel.subModel()->get_overall_area() + _ssModel.host_area());
    return obj;
  }

 private:
  //When we delete a hardware element, we need to:
  //1. deschedule anything that was assigned to that element
  //2. remove the concept of that element from the schedule (consistency)
  //3. remove the element from the hardware description
  void delete_node(ssnode* n) {
    delete_node_list.push_back(n->id());
    delete_nodep_list.insert(n);

    for_each_sched([&](Schedule& sched){ 
      for(int slot = 0; slot < sched.num_slots(n); ++slot) {
        for(auto& p : sched.dfg_nodes_of(slot,n)) {
          sched.unassign_dfgnode(p.first);
        }
      }
    });
    
    for(auto& l : n->in_links()) {
      delete_link(l);
    }
    for(auto& l : n->out_links()) {
      delete_link(l);
    }
  }

  std::unordered_set<ssnode*> delete_nodep_list;
  std::unordered_set<sslink*> delete_linkp_list;

  std::vector<int> delete_node_list;
  std::vector<int> delete_link_list;
  std::vector<int> delete_fu_list;
  std::vector<int> delete_sw_list;
  std::vector<int> delete_vport_list;


};

class Scheduler {
 public:
  Scheduler(SS_CONFIG::SSModel* ssModel)
      : _ssModel(ssModel), _optcr(0.1f), _optca(0.0f), _reslim(100000.0f) {}

  bool check_feasible(SSDfg* ssDFG, SSModel* ssmodel, bool verbose);

  bool vport_feasible(SSDfg* ssDFG, SSModel* ssmodel, bool verbose);

  virtual bool schedule(SSDfg* ssDFG, Schedule*& schedule) = 0;

  virtual bool incrementalSchedule(CodesignInstance& incr_table) {
    assert(0 && "not supported");
  }

  bool verbose;
  bool suppress_timing_print = false;

  void set_max_iters(int i) { _max_iters = i; }

  std::string str_subalg;

  std::string AUX(int x) { return (x == -1 ? "-" : std::to_string(x)); }

  double total_msec() {
    auto end = get_time::now();
    auto diff = end - _start;
    return ((double)std::chrono::duration_cast<usec>(diff).count()) / 1000.0;
  }

  void set_start_time() {
    _start = get_time::now();
  }

  virtual bool schedule_timed(SSDfg* ssDFG, Schedule*& sched) {
    set_start_time();

    bool succeed_sched = schedule(ssDFG, sched);

    if (verbose && !suppress_timing_print) {
      printf("sched_time: %0.4f seconds\n", total_msec() / 1000.0);
    }

    return succeed_sched;
  }

  void setGap(float relative, float absolute = 1.0f) {
    _optcr = relative;
    _optca = absolute;
  }

  void setTimeout(float timeout) { _reslim = timeout; }

  bool running() { return !_should_stop; }
  void stop() { _should_stop = true; }

  void set_srand(int i) { _srand = i; }

  Schedule *invoke(SSModel *model, SSDfg *dfg, bool);

 protected:
  SS_CONFIG::SSModel* getSSModel() { return _ssModel; }

  SS_CONFIG::SSModel* _ssModel;

  int _max_iters = 20000;
  bool _should_stop = false;
  int _srand = 0;

  std::vector<WorkloadSchedules*> _incr_schedules;

  float _optcr, _optca, _reslim;
  std::chrono::time_point<std::chrono::steady_clock> _start;
};

void make_directories(const std::string &s);

#endif
