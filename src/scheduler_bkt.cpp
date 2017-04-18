#include "scheduler_bkt.h"

using namespace SB_CONFIG;
using namespace std;

#include <unordered_map>
#include <fstream>
#include <list>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>


bool SchedulerBacktracking::schedule(SbPDG* sbPDG, Schedule*& sched) {

  sched = new Schedule(Scheduler::getSBModel(),sbPDG);
  sched->setNConfigs(1);

  bool vec_in_assigned = assignVectorInputs(sbPDG,sched);
  if(!vec_in_assigned) {
    return false;
  }

  map<SbPDG_Inst*,bool> seen;
  bool schedule_okay=true; 
 
  list<SbPDG_Inst* > openset;
  SbPDG::const_input_iterator I,E;

  //pdg input nodes
  for(I=sbPDG->input_begin(),E=sbPDG->input_end();I!=E;++I) {
    SbPDG_Input* n = *I;
    
    SbPDG_Node::const_edge_iterator I,E;
    for(I=n->uses_begin(), E=n->uses_end();I!=E;++I) {
      SbPDG_Inst* use_pdginst = dynamic_cast<SbPDG_Inst*>((*I)->use());
      if(use_pdginst) {
        openset.push_back(use_pdginst);
      }
    }
  }
 
  //populate the schedule object
  while(!openset.empty()) {
    SbPDG_Inst* n = openset.front(); 
    openset.pop_front();
    
    if(!seen[n]) {
      schedule_okay &= scheduleNode(sched,n);
    }
    seen[n]=true;
    
    SbPDG_Node::const_edge_iterator I,E;
    for(I=n->uses_begin(), E=n->uses_end();I!=E;++I) {
      SbPDG_Inst* use_pdginst = dynamic_cast<SbPDG_Inst*>((*I)->use());
      if(use_pdginst) {
        openset.push_back(use_pdginst);
      }
    }

  }

  bool vec_out_assigned = assignVectorOutputs(sbPDG,sched);
  if(!vec_out_assigned) {
    return false;
  }

  return schedule_okay;
}

bool SchedulerBacktracking::scheduleNode(Schedule* sched, SbPDG_Node* pdgnode) {

  std::pair<int,int> bestScore = make_pair(0,MAX_ROUTE);
	std::pair<int,int> fscore = make_pair(0,MAX_ROUTE);
  CandidateRouting* bestRouting = new CandidateRouting();
  sbnode* bestspot;
  int bestconfig;
  
  CandidateRouting* curRouting = new CandidateRouting();  

  std::vector<sbnode*> spots;
  
  //for each configuration
  for(int config = 0; config < sched->nConfigs(); ++config) {
    if(SbPDG_Inst* pdginst= dynamic_cast<SbPDG_Inst*>(pdgnode))  { 
      fillInstSpots(sched, pdginst, config, spots);             //all possible candidates based on FU capability 
    } else if(SbPDG_Input* pdg_in = dynamic_cast<SbPDG_Input*>(pdgnode)) {
      fillInputSpots(sched,pdg_in,config,spots); 
    } else if(SbPDG_Output* pdg_out = dynamic_cast<SbPDG_Output*>(pdgnode)) {
      fillOutputSpots(sched,pdg_out,config,spots); 
    }
   
    //populate a scheduling score for each of canidate sbspot
    for(unsigned i=0; i < spots.size(); i++) {
      sbnode* cand_spot = spots[i];
      
      curRouting->routing.clear();
      curRouting->forwarding.clear();
      
      pair<int,int> curScore = scheduleHere(sched, pdgnode, cand_spot, config,*curRouting,bestScore);
                  
      if(curScore < bestScore) {
        bestScore=curScore;
        bestspot=cand_spot;
        bestconfig=config;
        std::swap(bestRouting,curRouting);
      }
      
      if(bestScore<=make_pair(0,1))  {
        applyRouting(sched,pdgnode,bestspot,bestconfig,bestRouting);
        return true;
      }//apply routing step
    
    }//for loop -- check for all sbnode spots
  }
  
  
  //TODO: If not scheduled, then increase the numConfigs, and try again
  
  if(bestScore < fscore) {
    applyRouting(sched,pdgnode,bestspot,bestconfig,bestRouting);
  } else {
    cout << "no route found for pdgnode: " << pdgnode->name() << "\n";
    return false; 
  }  
  return true;
}



pair<int,int> SchedulerBacktracking::scheduleHere(Schedule* sched, SbPDG_Node* n,
                                sbnode* here, int config,
                                CandidateRouting& candRouting,
                                pair<int,int> bestScore) {
  pair<int,int> score=make_pair(0,0);
  pair<int,int> fscore = make_pair(0,MAX_ROUTE);

  SbPDG_Node::const_edge_iterator I,E;

  for(I=n->ops_begin(), E=n->ops_end();I!=E;++I) {
    if(*I == NULL) { continue; } //could be immediate
    SbPDG_Edge* source_pdgegde = (*I);
    SbPDG_Node* source_pdgnode = source_pdgegde->def();     //could be input node also

    //route edge if source pdgnode is scheduled
    if(sched->isScheduled(source_pdgnode)) {
      pair<sbnode*,int> source_loc = sched->locationOf(source_pdgnode); //scheduled location

      //route using source node, sbnode
      pair<int,int> tempScore = route(sched, source_pdgegde, source_loc.first, here,config,candRouting,bestScore-score);
      score = score + tempScore;
      //cout << n->name() << " " << here->name() << " " << score << "\n";
      if(score>bestScore) return fscore;
    }
  }

  SbPDG_Node::const_edge_iterator Iu,Eu;
  for(Iu=n->uses_begin(), Eu=n->uses_end();Iu!=Eu;++Iu) {
    SbPDG_Edge* use_pdgedge = (*Iu);
    SbPDG_Node* use_pdgnode = use_pdgedge->use();

     //route edge if source pdgnode is scheduled
     if(sched->isScheduled(use_pdgnode)) {
       pair<sbnode*,int> use_loc = sched->locationOf(use_pdgnode);

       pair<int,int> tempScore = route(sched, use_pdgedge, here, use_loc.first,config,candRouting,bestScore-score);
       score = score + tempScore;
       //cout << n->name() << " " << here->name() << " " << score << "\n";
       if(score>bestScore) return score;
     }
  }

  return score;
}

pair<int,int> SchedulerBacktracking::route(Schedule* sched, SbPDG_Edge* pdgedge, sbnode* source, sbnode* dest, int config, CandidateRouting& candRouting, pair<int,int> scoreLeft) {
	pair<int,int> score = route_minimizeDistance(sched, pdgedge, source, dest, config, candRouting, scoreLeft);
	return score;
}





