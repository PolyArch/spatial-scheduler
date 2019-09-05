#include "scheduler_sa.h"

using namespace SS_CONFIG;
using namespace std;

#include <unordered_map>
#include <fstream>
#include <list>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>

#define DEBUG_SCHED (false)

//Major things left to try that might improve the algorithm:
//1. Overprovisioning while routing/placing 
//  (DONE -- iterative+overprovisioning is incredibly useful for large graphs)
//2. True Simulated Annealing -- include a temperature that controls randomness?
//3. Prioritizing nodes based on what their presumed effect on scheduling
//   quality might be.  This may be difficult to gauge though.
//4. Including multiple schedules to work on concurrently.  Sometimes its useful
//   to work on very different schedules, as the scheduler easily gets stuck
//   in local optima.
//5. Incorperating Multithreading -- this might work nicely with 4, where each
//   thread works on a different schedule.
//6. Better way to fix_latency.  One option is max_flow formulation as suggested
//by Jian; not sure it will be faster than the iterative heuristic (it could be
//exactly the same).  Another option is a proper ILP -- this is no fun fun to
//maintain with an interface to another language (eg. gams), but otherwise its
//okay.
//7. Introduce more randomness into routing itself?  Maybe randomly de-prioritize
//a path.
//8. For paths where you multi-cast a value (A->B A->C ...), finding the optimal
//version is a steiner-tree problem for graphs.  A direct implementation may be better
//than our approximate version (Which uses multiple djkstra's instances).  Warning
//that steiner problems often don't have polynomial solutions, so *some* approximation
//is necessary.
//9. For delay matching, it may be useful to insert dummy nodes at the places
//in the graph where there is high latency violation -- OR -- change the router
//so that it tries to route empty functional units when there it is a high-violation
//edge

void SchedulerSimulatedAnnealing::initialize(SSDfg* ssDFG, Schedule*& sched) {
  //Sort the input ports once
  SS_CONFIG::SubModel* subModel = _ssModel->subModel();
  ssio_interface& si =  subModel->io_interf();

  si.fill_vec();

  sched = new Schedule(getSSModel(),ssDFG); //just a dummy one
}

std::pair<int, int> SchedulerSimulatedAnnealing::obj(Schedule*& sched,
    int& lat, int& latmis, int& ovr, int& agg_ovr, int& max_util) {  
  int num_left = sched->num_left(); 
  bool succeed_sched = (num_left==0);

  sched->get_overprov(ovr,agg_ovr,max_util);
  sched->fixLatency(lat,latmis);

  int violation = sched->violation();

    int obj = agg_ovr * 1000 + violation * 200 + latmis * 200
      + lat + (max_util - 1) * 3000 + sched->num_passthroughs();
    obj = obj*100 + sched->num_links_mapped();

  return make_pair(succeed_sched - num_left, -obj);
}

bool SchedulerSimulatedAnnealing::length_creep(Schedule* sched,
    SSDfgEdge* edge, int& num, CandidateRoute& undo_routing) {
  bool changed=false;

  int chances_left=40;
  while(chances_left-- && num > 0) {
    auto& links = sched->links_of(edge); 
    int rand_link_no=rand_bt(0,links.size());
    auto it = links.begin(); 
    for(int i = 0; i < rand_link_no;++i) ++it;
    std::pair<int,sslink*> rand_link=*it;

    auto& edge_list = sched->edge_list(rand_link.first,rand_link.second);

    //Check to make sure everything is kosher
    //Not necessary for now, but might need later
    /*
    bool print_it=false;
    for(auto alt_edge : edge_list) {
      auto it = sched->links_of(edge).begin();
      auto it_alt = sched->links_of(alt_edge).begin();
      for(int i = 0; i < rand_link_no; ++i) {
        if(*it != *it_alt) {
          print_it=true;
          cout << "FAILURE @" << _route_times << " for " << rand_link.second->name() 
            << " with num " << rand_link_no << " and i=" << i << "\n";
          break;
        }
        ++it;
        ++it_alt;
      }
    }

    if(print_it) {
      for(auto edge : edge_list) {
        cout << "Edge: " << edge->name();
        cout << "Route times: ";
        for(uint64_t i : sched->edge_prop()[edge->id()].sched_index) cout << i << " ";
        cout << "\n";
        for(auto i : sched->links_of(edge)) {
          cout << i.second->name() 
               << " #edges:" << sched->edge_list(i.first,i.second).size() << "\n";
        } 
        cout << "--" << "\n";
      }
      assert(0);
    }
    */

    //Back to normal stuff
    bool bad_spot=false;
    for(auto alt_edge : edge_list) {
      if(sched->vioOf(alt_edge) < num+1) {
        bad_spot=true;
        break;
      }
    }
    if(bad_spot) continue;

    for(auto edge : edge_list) {
      if(!undo_routing.edges.count(edge)) 
        undo_routing.fill_edge(edge,sched);
    }

    if(ssswitch* sw = dynamic_cast<ssswitch*>(rand_link.second->dest())) {
      auto source = make_pair(rand_link.first,sw);
      int inserted = route(sched,edge,source,source,&(++it),num+1);
      if(inserted) {
        //cout << "inserted " << inserted << " links into edge " << edge->name() << "\n";
        for(auto alt_edge : edge_list) {
          sched->record_violation(alt_edge,sched->vioOf(alt_edge)-inserted);
          if(alt_edge==edge) continue;
          auto& alt_links = sched->links_of(alt_edge); 
          auto alt_it = alt_links.begin(); 
          for(int i = 0; i < rand_link_no+1;++i) {
            ++alt_it;
          }
 
          auto from_it = links.begin(); 
          for(int i = 0; i < rand_link_no+1;++i) ++from_it;
          for(int i = 0; i < inserted; ++i) {
            std::pair<int,sslink*> from_link=*from_it;
            sched->assign_edgelink(alt_edge, from_link.first, from_link.second,alt_it);
            if (auto fu = dynamic_cast<ssfu*>(from_link.second->dest())) {
              if (i+1 !=inserted) {
                sched->assign_edge_pt(alt_edge, make_pair(from_link.first,fu));
              }
            }

            from_it++; //increment the iterator to copy a new node
            //alt_it++;   -- don't need to do this because we insert before...
          }
        }
        num-=inserted;
        chances_left++;
        changed=true;
      }

      for(auto alt_edge : edge_list) {
        sched->check_links_consistency(alt_edge);
      }
    }
  }
  return changed;
}

/*
bool SchedulerSimulatedAnnealing::length_creep(Schedule* sched,
    SSDfgEdge* edge, int& num, CandidateRoute& undo_routing) {
  bool changed=false;

  int chances_left=100;
  while(chances_left-- && num > 0) {
    auto& links = sched->links_of(edge); 
    int rand_link_no=rand_bt(0,links.size());
    auto it = links.begin(); 
    for(int i = 0; i < rand_link_no;++i) ++it;
    std::pair<int,sslink*> rand_link=*it;

    auto& edge_list = sched->edge_list(rand_link.first,rand_link.second);
    if(edge_list.size() >1) continue;
    bool bad_spot=false;
    for(auto edge : edge_list) {
      if(sched->vioOf(edge) > num) {
        bad_spot=true;
        break;
      }
    }
    if(bad_spot) continue;

    //Preserve the first time you undo an edge
    for(auto edge : edge_list)
      if(!undo_routing.edges.count(edge)) 
        undo_routing.fill_edge(edge,sched);

    if(ssswitch* sw = dynamic_cast<ssswitch*>(rand_link.second->dest())) {
      auto source = make_pair(rand_link.first,sw);
      int inserted = route(sched,edge,source,source,true,num+1);
      if(inserted) {
        //cout << "inserted " << inserted << " links into edge " << edge->name() << "\n";

        for(auto alt_edge : edge_list) {
          sched->record_violation(alt_edge,sched->vioOf(alt_edge)-inserted);
          if(alt_edge==edge) continue;
          auto& alt_links = sched->links_of(alt_edge); 
          auto alt_it = alt_links.begin(); 
          for(int i = 0; i < rand_link_no;++i) {
            ++alt_it;
            assert(alt_it!=alt_links.end());
          }
 
          auto from_it = it; 
          for(int i = 0; i < inserted; ++i) {
            std::pair<int,sslink*> from_link=*from_it;
            sched->assign_edgelink(alt_edge, from_link.first, from_link.second,alt_it);
            if (auto fu = dynamic_cast<ssfu*>(from_link.second->dest())) {
              if (i+1 !=inserted) {
                sched->assign_edge_pt(alt_edge, make_pair(from_link.first,fu));
              }
            }

            from_it++; //increment the iterator to copy a new node
            alt_it++;  
          }
        }
       
        num-=inserted;
        chances_left++;
        changed=true;
      }
    }
  }
  return changed;
}
*/

std::pair<int, int> SchedulerSimulatedAnnealing::obj_creep( Schedule*& sched, 
    int& lat, int& latmis, int& ovr, int& agg_ovr, int& max_util,
    CandidateRoute& undo_routing) {

    int num_left = sched->num_left(); 
    std::pair<int,int> curScore = obj(sched,lat,latmis,ovr,agg_ovr,max_util);

    std::unordered_map<SSDfgNode*,int> accum_vio;
    if(agg_ovr==0 && num_left==0) {
      //cout << "---------------------- attempting creep --------" << "\n";
      accum_vio.clear(); //just clear this
      std::vector<SSDfgNode*> ordered_non_temp = sched->ordered_non_temporal(); 

      //cout << "VIOLATION -----------------------------------\n";
      //for(auto n : ordered_non_temp) {
      //  cout << n->name() << ": " << sched->vioOf(n) << "   --   ";
      //  for(auto it : n->in_edges()) {
      //    cout << it->name() <<":" << sched->vioOf(it) << " ";
      //  }
      //  cout << "\n";
      //}

      auto creep_it = ordered_non_temp.rbegin(); 

      while(creep_it != ordered_non_temp.rend()) {
        if(latmis==0) break;

        int r = rand_bt(0,4);
        if(r!=0) continue;

        SSDfgNode* v = *creep_it;
        //int node_vio = sched->vioOf(v);

        for(auto e : v->in_edges()) {
          int vio = sched->vioOf(e);
          if(vio) {
            vio = rand_bt(0,vio);
        bool changed=false;
            changed |= length_creep(sched,e,vio,undo_routing);
            if(changed) obj(sched,lat,latmis,ovr,agg_ovr,max_util);

          }
        }


        ++creep_it;
      }

      //while(creep_it != ordered_non_temp.rend()) {
      //  if(latmis==0) break;

      //  //int r = rand_bt(0,4);
      //  //if(r!=0) continue;

      //  SSDfgNode* v = *creep_it;
      //  //int node_vio = sched->vioOf(v);

      //  bool changed=false;
      //  for(auto e : v->in_edges()) {
      //    int vio = sched->vioOf(e);
      //    changed |= length_creep(sched,e,vio,undo_routing);
      //  }

      //  if(changed) obj(sched,lat,latmis,ovr,agg_ovr,max_util);


      //  ++creep_it;
      //}
      return obj(sched,lat,latmis,ovr,agg_ovr,max_util);
    }
    return curScore;
}


bool SchedulerSimulatedAnnealing::schedule(SSDfg* ssDFG, Schedule*& sched) {  
  initialize(ssDFG,sched);

  int max_iters_no_improvement = 100000000;
  srand(++_srand);

  Schedule* cur_sched = new Schedule(getSSModel(),ssDFG);
  std::pair<int, int> best_score = make_pair(0, 0);
  bool best_succeeded = false;
  bool best_mapped = false;

  int last_improvement_iter = 0;

  _best_lat=MAX_ROUTE;
  _best_violation=MAX_ROUTE;

  int presize = ssDFG->inst_vec().size();
  int postsize = presize;

  int iter = 0;
  for (iter = 0; iter < _max_iters; ++iter) {
    if( (total_msec() > _reslim * 1000) || _should_stop ) {
      break;
    }

    bool print_stat = (iter & (256-1)) == 0;

    if((iter & (4096-1)) == 0) {
      //Every so often, lets give up and start over from scratch
      delete cur_sched;
      cur_sched = new Schedule(getSSModel(),ssDFG);
    }

    // if we don't improve for some time, lets reset
    if(iter - last_improvement_iter > 128) {
      *cur_sched = *sched; 
    }

    bool succeed_sched = schedule_internal(ssDFG, cur_sched);

    int lat=INT_MAX,latmis=INT_MAX,agg_ovr=INT_MAX,ovr=INT_MAX,max_util=INT_MAX;
    std::pair<int,int> score = obj(cur_sched,lat,latmis,ovr,agg_ovr,max_util);

    int succeed_timing = (latmis ==0) && (ovr ==0);

    if (verbose && ((score > best_score) || print_stat)) {

      stringstream ss;
      ss << "viz/iter/" << iter << ".gv";
      cur_sched->printGraphviz(ss.str().c_str());

      for(int i = 0; i < ssDFG->num_vec_input(); ++i) {
        cout << cur_sched->vecPortOf(ssDFG->vec_in(i)) << " ";
      }
      cout << "|";
      for(int i = 0; i < ssDFG->num_vec_output(); ++i) {
        cout << cur_sched->vecPortOf(ssDFG->vec_out(i)) << " ";
      }

      fprintf(stdout, "Iter: %4d, time:%0.2f, kRPS:%0.1f, left: %3d, " 
              "lat: %3d, vio %d, mis: %d, ovr: %d, agg_ovr: %d, util: %d, "
              "obj:%d, ins: %d/%d, outs: %d/%d,"
              " insts: %d/%d,%d, pts:%d, links:%d, edge-links:%d  %s%s", 
              iter, total_msec()/1000.f, _route_times/total_msec(),
              cur_sched->num_left(), lat, 
              cur_sched->violation(), latmis, ovr, agg_ovr, 
              max_util, -score.second,
              cur_sched->num_mapped<SSDfgVecInput>(),  
              (int) ssDFG->nodes<SSDfgVecInput*>().size(),
              cur_sched->num_mapped<SSDfgVecOutput>(), 
              (int) ssDFG->nodes<SSDfgVecOutput*>().size(),
              cur_sched->num_mapped<SSDfgInst>(),  presize, postsize,
              cur_sched->num_passthroughs(),
              cur_sched->num_links_mapped(),
              cur_sched->num_edge_links_mapped(),
              succeed_sched ? ", all mapped" : "",
              succeed_timing ? ", mismatch == 0" : "");
      if(score > best_score) {
        cout << "\n";
      } else {
        cout.flush();
        cout << "\r";
      }
    }


    if (score > best_score) {
      sched->printGraphviz("viz/cur-best.gv");

      best_score = score;
      *sched = *cur_sched; //shallow copy of sched should work?
      if (succeed_timing && !best_succeeded) {
        max_iters_no_improvement = std::max(1000, iter * 2);
      }

      best_mapped = succeed_sched;
      best_succeeded = succeed_timing;
      last_improvement_iter = iter;
    }

    if (((iter - last_improvement_iter) > max_iters_no_improvement) 
        && best_succeeded) {
      break;
    }

    if(best_succeeded) {
      break;
    }

  }

  if(verbose) {
    cout << "Breaking at Iter " << iter << "\n";
    std::cout << "Totally " << this->candidates_succ << "/" << this->candidates_tried << std::endl;
  }

  if(cur_sched) {
    delete cur_sched;
  }
  return best_mapped;
}

bool SchedulerSimulatedAnnealing::map_io_to_completion(SSDfg* ssDFG, Schedule* sched) {

  while (!(sched->is_complete<SSDfgVecInput>() && sched->is_complete<SSDfgVecOutput>())) {
    int r = rand_bt(0, 2); 
    switch (r) {
      case 0: {
        if (sched->is_complete<SSDfgVecInput>())
          break;
        bool success = map_one<SSDfgVecInput>(ssDFG, sched);
        if (!success) return false;
        break;
      }
      case 1: {
        if (sched->is_complete<SSDfgVecOutput>())
          break;
        bool success = map_one<SSDfgVecOutput>(ssDFG, sched);
        if (!success) return false;
        break;
      }
      default: {
        assert(0);
        break;
      }
    }
  }

  return true;
}


bool SchedulerSimulatedAnnealing::map_to_completion(SSDfg* ssDFG, Schedule* sched) {

  if(DEBUG_SCHED) cout << "Map to completion! " << sched->num_mapped<SSDfgNode>() << "\n";
  while(!sched->is_complete<SSDfgNode>()) {
    int r = rand() % 16; //upper limit defines ratio of input/output scheduling
    switch(r) {
      case 0: {
        if(sched->is_complete<SSDfgVecInput>())
          break;
        bool success = map_one<SSDfgVecInput>(ssDFG,sched);
        if(!success) {
          return false;
        }
        break;
      }
      case 1: {
        if(sched->is_complete<SSDfgVecOutput>())
          break;
        bool success = map_one<SSDfgVecOutput>(ssDFG,sched);
        if(!success) { 
          return false;
        }
        break;
      }      
      default: {
        if(sched->is_complete<SSDfgInst>())
          break;
        bool success = map_one<SSDfgInst>(ssDFG,sched);
        if(!success) { 
          return false;
        }
        break;
      }      
    }
  }
  
  return true;
}

void SchedulerSimulatedAnnealing::unmap_some(SSDfg* ssDFG, Schedule* sched) {

  int r = rand() % 1000; //upper limit defines ratio of input/output scheduling
  int num_to_unmap = (r < 5) ? 10 : (r < 250 ? 4 : 2);

  for (int i = 0; i < num_to_unmap && sched->num_mapped<SSDfgNode>(); ++i) {
    bool flag = sched->num_mapped<SSDfgNode>() != 0;
    while (flag) {
      r = rand_bt(0,10);
      if (r == 0 && sched->num_mapped<SSDfgVecInput>()) {
        unmap_one<SSDfgVecInput>(ssDFG, sched);
        i+=1;
        flag = false;
      } else if (r == 1 && sched->num_mapped<SSDfgVecOutput>()) {
        unmap_one<SSDfgVecOutput>(ssDFG, sched);
        i+=1;
        flag = false;
      } else if (sched->num_mapped<SSDfgInst>()) {
        unmap_one<SSDfgInst>(ssDFG, sched);
        flag = false;
      }
    }
  }

}

bool SchedulerSimulatedAnnealing::schedule_internal(SSDfg* ssDFG, Schedule*& sched) {
  int max_retries = 100;

  for(int t = 0; t < max_retries; ++t) {
    unmap_some(ssDFG,sched);

    if(_fake_it) {
      //Just map the i/o ports
      map_io_to_completion(ssDFG,sched);
      if(sched->is_complete<SSDfgVecInput>() && sched->is_complete<SSDfgVecOutput>()) {
        return true;
      }
    } else {
      //THE REAL THING
      map_to_completion(ssDFG,sched);
      if(sched->is_complete<SSDfgNode>()) {
        //cout << "remppaed after " << t << " tries" << "\n";
        return true;
      } 
    }
  } 
  //cout << "failed to remap\n";
  return false;
}

int SchedulerSimulatedAnnealing::routing_cost(SSDfgEdge* edge, int from_slot, int next_slot,
                                              sslink *link, Schedule* sched,
                                              const pair<int, ssnode*> &dest) {

  SSDfgNode *def_dfgnode = edge->def();
  SSDfgNode *use_dfgnode = edge->use();

  bool is_in = def_dfgnode->type() == SSDfgNode::V_INPUT;
  bool is_out = use_dfgnode->type() == SSDfgNode::V_OUTPUT;
  bool is_io = is_in || is_out;

  bool is_temporal = def_dfgnode->is_temporal();
  bool is_temporal_inst = is_temporal && !is_io;
  bool is_temporal_in = is_temporal && is_in;
  bool is_temporal_out = is_temporal && is_out;

  int internet_dis = abs(from_slot - next_slot);
  internet_dis = min(internet_dis, 8 - internet_dis);

  //For now, links only route on their own network
  //ie. temporal_io and non-temporal route on dedicated network
  if ((!is_temporal_inst && link->max_util() > 1) || (is_temporal_inst && link->max_util() <= 1)) {
    return -1;
  }

  ssnode *next = link->dest();

  //check if connection is closed..
  //0: free
  //1: empty
  //2: already there
  // FIXME(@were): Move these to a virtual method!
  int t_cost;
  if (is_temporal_in) {
    t_cost = sched->routing_cost_in(link, 
                       dynamic_cast<SSDfgVecInput *>(def_dfgnode));
  } else if (is_temporal_out) {
    t_cost = sched->routing_cost_out(make_pair(from_slot, link), def_dfgnode,
                       dynamic_cast<SSDfgVecOutput *>(use_dfgnode));
  } else { //NORMAL CASE!
    t_cost = sched->routing_cost(make_pair(from_slot, link), edge->val());
  }

  if (t_cost >= 2) { //square law avoidance of existing routes
    if (!is_temporal_inst) {
      return (t_cost) * (t_cost) * 10;
    }
  }

  bool is_dest = (next == dest.second && next_slot == dest.first);

  ssfu *fu = dynamic_cast<ssfu*>(next);
  if (fu && sched->dfgNodeOf(next_slot,fu) && !is_dest)
    return -1;  //stop if run into fu
  if (t_cost == 0) {
    return 0 + internet_dis; //free link because schedule or candidate routing already maps
  } else {
    if (fu && sched->isPassthrough(fu)) {
      return -1; //someone else's pass through
    }
    int passthrough = 10 * (fu && !is_dest);
    if(passthrough) 
      passthrough += 50 * sched->thingsAssigned(fu);

    return t_cost + passthrough + internet_dis;
  }

}

void insert_edge(std::pair<int,sslink*> link, Schedule* sched, 
      SSDfgEdge* edge, std::list<std::pair<int,sslink*>>::iterator it,
      std::pair<int,ssnode*> dest, std::pair<int,ssnode*> x) {
    sched->assign_edgelink(edge, link.first, link.second,it); 

    if (auto fu = dynamic_cast<ssfu*>(link.second->dest())) {
      if ((dest != x) && !sched->dfgNodeOf(link.first,link.second)) {
        sched->assign_edge_pt(edge, x);
      }
    }
}

int
SchedulerSimulatedAnnealing::route(Schedule *sched, 
                              SSDfgEdge *edge,
                              std::pair<int, SS_CONFIG::ssnode *> source,
                              std::pair<int, SS_CONFIG::ssnode *> dest,
      std::list<std::pair<int,sslink*>>::iterator* ins_it, int max_path_lengthen) {

  bool path_lengthen = ins_it!=nullptr;

  _ssModel->subModel()->clear_all_runtime_vals();
  int bitwidth = edge->bitwidth();

  if (edge->def()->bitwidth() > edge->bitwidth() && source.first != edge->l() / 8) {
    source.first = edge->l() / 8;
  }

  if(!path_lengthen) _route_times++;

  //FIXME: comment/delete this later
  //if(!path_lengthen) sched->edge_prop()[edge->id()].sched_index.clear();
  //sched->edge_prop()[edge->id()].sched_index.push_back(_route_times);

  if ((!path_lengthen) && (sched->link_count(edge) != 0)) {
    cerr << "Edge: " << edge->name() << " is already routed!\n";
    assert(0);
  }

  // Distance, random priority, slot, node
  set<std::tuple<int, int, int, ssnode*>> openset;

  int new_rand_prio = 0; //just pick zero
  source.second->set_done(source.first,new_rand_prio); //remeber for deleting
  openset.emplace(0, new_rand_prio, source.first, source.second);
  source.second->update_dist(source.first, 0, 0, nullptr);

  while (!openset.empty()) {
    int cur_dist = std::get<0>(*openset.begin());
    int slot = std::get<2>(*openset.begin());
    ssnode *node = std::get<3>(*openset.begin());

    openset.erase(openset.begin());

    if(!path_lengthen) {
      if (slot == dest.first && node == dest.second) break;
    } else {
      if ( (slot == dest.first && node == dest.second && cur_dist > 0) ||
         (cur_dist > max_path_lengthen) ) {
      break; 
      }
    }

    for (auto link : node->out_links()) {
      ssnode *next = link->dest();
      bool is_fu = dynamic_cast<ssfu*>(next) != nullptr;

      for (int delta = 0; delta <= (is_fu ? (64 / bitwidth - 1) : 1); ++delta) {
        int next_slot = slot + delta * bitwidth / 8;
        next_slot = (next_slot + 8) % 8;
        int route_cost;
        if(!path_lengthen) { // Normal thing
          route_cost = routing_cost(edge, slot, next_slot, link, sched, dest);
        } else {
          //For path lengthening, only route on free spaces
          route_cost = sched->routing_cost(make_pair(slot,link),edge->val());
          if(route_cost!=1 || sched->dfgNodeOf(next_slot,next)) route_cost =-1;
        }
 
        if (route_cost == -1) continue;

        int new_dist = cur_dist + route_cost;

        int next_dist = next->node_dist(next_slot);

        bool found_dest = (make_pair(next_slot,next) == dest);

        if (next_dist == -1 || next_dist > new_dist || found_dest) {
          if (next_dist != -1) {
            int next_rand_prio = next->done(next_slot);
            auto iter = openset.find(std::make_tuple(next_dist, next_rand_prio, 
                                                     next_slot, next));
              if(iter != openset.end())
                openset.erase(iter);
          }
          int new_rand_prio = rand_bt(0,20);
          next->set_done(next_slot,new_rand_prio); //remeber for later for deleting
          openset.emplace(new_dist, new_rand_prio,next_slot, next);
          next->update_dist(next_slot, new_dist, slot, link);
        } 
      }

    }
  }

  if (dest.second->node_dist(dest.first) == -1 || 
      (path_lengthen && dest.second->node_dist(dest.first)==0) ) {
    return false;  //routing failed, no routes exist!
  }

  auto it = ins_it ? *ins_it : sched->links_of(edge).begin();
  auto x = dest;

  int count = 0;
  SSDfgEdge* alt_edge=nullptr;
  pair<int, sslink*> link;

  while (x != source || (path_lengthen && count==0)) {
    count++;
    link = x.second->came_from(x.first);

    if((alt_edge=sched->alt_edge_for_link(link,edge->val()))) {
      break;
    }
    insert_edge(link,sched,edge,it,dest,x);
    it--; //dec iter to insert backwards

    x = std::make_pair(link.first, link.second->orig());
  }


  //Need to make sure we add back the other links in-order.
  //With path lengthening (which has cycles) the previous
  //code can't gaurantee that.
  if(alt_edge) {
    //cout << alt_edge->name() << " is alt_edge ---------- ";

    auto& alt_links = sched->links_of(alt_edge);
    for(auto alt_link : alt_links) {
      //cout << alt_link.second->name() << "\n";
      x = std::make_pair(alt_link.first, alt_link.second->orig());
      insert_edge(alt_link,sched,edge,it,dest,x);

      if(alt_link == link) break; // we added the final link
    }
    //cout << "Printing links for newly routed edge:\n ";
    //auto& links = sched->links_of(edge);
    //for(auto l : links) {
    //  std::cout << l.second->name() << "\n";
    //}
    //cout << "\n";
  }

  if(!path_lengthen){
    //Edges might not be consistent at this point for 
    sched->check_links_consistency(edge);
  }


  return count;
}


bool SchedulerSimulatedAnnealing::scheduleHere(Schedule* sched,
    SSDfgNode* node, pair<int, SS_CONFIG::ssnode*> here) {

  std::vector<SSDfgEdge*> to_revert;

#define process(edge_, node_, src, dest)                             \
  do {                                                               \
    auto &edges = node->edge_();                                     \
    int n = node->edge_().size();                                    \
    for (int i = 0; i < n; ++i) {                                    \
      auto edge = edges[i];                                          \
      if (sched->link_count(edge) != 0) {                            \
        cerr << "Edge: " << edge->name() << " is already routed!\n"; \
        assert(0);                                                   \
      }                                                              \
      auto node = edge->node_();                                     \
      if (sched->is_scheduled(node)) {                               \
        auto loc = sched->location_of(node);                         \
        if (!route(sched, edge, src, dest, nullptr,0))  {            \
          for (auto revert : to_revert)                              \
            sched->unassign_edge(revert);                            \
          for (int j = 0; j < i; ++j)                                \
            sched->unassign_edge(edges[j]);                          \
          return false;                                              \
        }                                                            \
      }                                                              \
    }                                                                \
  } while(false)

  process(in_edges, def, loc, here);
  to_revert = node->in_edges();
  process(uses, use, here, loc);

#undef process

  sched->assign_node(node, here);
  
  return true;
}
