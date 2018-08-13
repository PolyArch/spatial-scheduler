#ifndef __SB_SUB_MODEL_H__
#define __SB_SUB_MODEL_H__

#include "fu_model.h"
#include "direction.h"

#include <string>
#include <vector>
#include <sstream>
#include <unordered_map>
#include <map>
#include <utility>
#include <algorithm>

namespace SB_CONFIG {

class sbnode;
class sbinput;
class sboutput;

class sbio_interface {
    public:
    //interf_vec_port_num -> [cgra port_num -> vector_offset_elements]
    std::map<int, std::vector<std::pair<int, std::vector<int> > > > in_vports;
    std::map<int, std::vector<std::pair<int, std::vector<int> > > > out_vports;

    //intef_port_num -> possible_elements
    std::map<int, std::vector<int> > in_ports;
    std::map<int, std::vector<int> > out_ports;

      
    void sort_in_vports(std::vector<std::pair<int,int>>& portID2size) {
      sort(portID2size, in_vports);    
    }
    
    void sort_out_vports(std::vector<std::pair<int,int>>& portID2size) {
      sort(portID2size, out_vports);    
    }
    
    std::vector<std::pair<int, std::vector<int> > >& getDesc_I(int id) {
        assert(in_vports.count(id) != 0);
        return in_vports[id];
    }  
    std::vector<std::pair<int, std::vector<int> > >& getDesc_O(int id) {
        assert(out_vports.count(id) != 0);
        return out_vports[id];
    }  
    private:        
    void sort(std::vector<std::pair<int,int>>& portID2size, 
         std::map<int,std::vector<std::pair<int,std::vector<int>>>>& vports) {
      int index = 0;
      portID2size.resize(vports.size());
      for(auto i : vports) {
        int id = i.first;
        int size = i.second.size();
        portID2size[index++] = std::make_pair(id,size);
      }
      std::sort(portID2size.begin(), portID2size.end(), 
                [](std::pair<int,int>& left, std::pair<int,int>& right){
        return left.second < right.second;
      });
    }
  

};

class sblink {
    public:
    //enum linktype { input, output, inter, cross }
          
    sblink() {}
      
    sbnode* orig() const {return _orig;}
    sbnode* dest() const {return _dest;}
    SbDIR::DIR dir()  const {return _dir;}
    void setdir(SbDIR::DIR dir) { _dir=dir;}
    
    //Constructor
    sblink(sbnode* orig, sbnode* dest) {
        _orig=orig;
        _dest=dest;
        _ID=-1;
    }

    std::string name() const;
    std::string gams_name(int config) const;
    std::string gams_name(int , int) const;
    
    int id() {return _ID;}
    void set_id(int id) {_ID=id;}

    int max_util() {return _max_util;}
    int set_max_util(int m) {return _max_util = m;}

    protected:
    int _ID=-1;

    int _max_util=1; // by default, assume its a dedicated link

    sbnode* _orig;
    sbnode* _dest;
    SbDIR::DIR _dir;
    
    private:
    friend class SubModel;
};
    
    
class sbnode {   
    public:
    sbnode()  {}
      
    sblink* add_link(sbnode* node) { 
        sblink* link = new sblink(this, node);
        _out_links.push_back(link);
        node->add_back_link(link);
        return link;
    }

    void add_back_link(sblink* link) {
        _in_links.push_back(link);
    }
    
    virtual std::string name() const {
      return std::string("loadslice"); 
    }
    virtual std::string gams_name(int config=0) const {
      return std::string("loadslice");
    }
    
    typedef std::vector<sblink*>::const_iterator const_iterator;
    const_iterator ibegin() const {return _in_links.begin();}
    const_iterator iend() const {return _in_links.end();}
    const_iterator obegin() const {return _out_links.begin();}
    const_iterator oend() const {return _out_links.end();}
    
    sblink* getFirstOutLink() {
      if(_out_links.size()>0) {
          return _out_links[0];
      } else {
          return NULL;
      }
    }
    
    sblink* getFirstInLink() {
      if(_in_links.size()>0) {
          return _in_links[0];
      } else {
          return NULL;
      }
    }
    
    sblink* getInLink(SbDIR::DIR dir) {
      for(const_iterator I=ibegin(), E=iend();I!=E; ++I) {
          sblink* dlink= *I;
          if(dlink->dir() == dir) return dlink;
      }
      return NULL;
    }

    sblink* getOutLink(SbDIR::DIR dir) {
      for(const_iterator I=obegin(), E=oend();I!=E; ++I) {
          sblink* dlink= *I;
          if(dlink->dir() == dir) return dlink;
      }
      return NULL;
    }

    sblink* getCycleLink() {
      for(const_iterator I=obegin(), E=oend();I!=E; ++I) {
        sblink* dlink= *I;
        if(dlink->dest() == this) {
          return dlink;
        }
      }
      return NULL;
    }


    int id() {return _ID;}
    void set_id(std::vector<sbnode*>& node_list, 
                std::vector<sblink*>& link_list) {
      _ID=node_list.size();
      node_list.push_back(this);  
      for(unsigned i = 0; i < _out_links.size(); ++i) {
        sblink* link = _out_links[i];
        assert(link->id()==-1);
        link->set_id(link_list.size());
        link_list.push_back(link);
      }
    }
    
    int node_dist() {return _node_dist;}
    void set_node_dist(int n) {_node_dist = n;}

    int done() {return _done;}
    void set_done(int n) {_done = n;}

    sblink* came_from() {return _came_from;}
    void set_came_from(sblink* n) {
      assert(n==0 || n->orig()->came_from()==0 || n->orig()->came_from()->orig() != this);
      _came_from = n;
    }

    void reset_runtime_vals() {
      set_node_dist(-1);
      set_came_from(0);
      set_done(0);
    }

    int max_util() {return _max_util;}
    int set_max_util(int m) {return _max_util = m;}

    protected:
      int _ID=-1;
      int _node_dist;
      int _done;
      int _max_util=1; // by default, assume its a dedicated link
      sblink* _came_from;
      std::vector<sblink*> _in_links; 
      std::vector<sblink*> _out_links; 
    
    private:
      friend class SubModel;
};
    
class sbswitch : public sbnode {
    public:

    sbswitch() : sbnode() {}
    
    void setXY(int x,int y) {_x=x;_y=y;}
    int x() const {return _x;}
    int y() const {return _y;}

    virtual std::string name() const {
        std::stringstream ss;
        ss << "SW" << "_" << _x << "_" << _y;
        return ss.str();
    }

    virtual std::string gams_name(int config) const {
        std::stringstream ss;
        if(config!=0) {
          ss << "Sw" << _x << _y << "c" << config;
        } else {
          ss << "Sw" << _x << _y;
        }
        return ss.str();
    }
    
    sbinput* getInput(int i);
    
    sboutput* getOutput(int i);
    
    protected:
    int _x, _y;
};
    
class sbfu : public sbnode {
    public:   

    sbfu() : sbnode() {}
      
    void setFUDef(func_unit_def* fu_def) {_fu_def = fu_def;}
    void setXY(int x, int y) {_x=x;_y=y;}

    int x() const {return _x;}
    int y() const {return _y;}
    
    virtual std::string name() const {
        std::stringstream ss;
        ss << "FU" << "_" << _x << "_" << _y;
        return ss.str();
    }

    virtual std::string gams_name(int config) const {
        std::stringstream ss;
        if(config!=0) {
          ss << "Fu" << _x << _y << "c" << config;
        } else {
          ss << "Fu" << _x << _y;
        }
        return ss.str();
    }
    
    func_unit_def* fu_def() {return _fu_def;}
    
    protected:      
    int _x, _y;
    func_unit_def* _fu_def;

    private:
    friend class SubModel;
};

class sbinput : public sbnode { 
    public:
    
    sbinput() : sbnode() {}
      
    void setPort(int port) {_port=port;}
    int port() const {return _port;}
    
    std::string name() const {
        std::stringstream ss;
        ss << "IP" << "_" << _port;
        return ss.str();
    }
    std::string gams_name(int config) const {
        std::stringstream ss;
        if(config!=0) {
          ss << "I" << _port << "c" << config;
        } else {
          ss << "I" << _port;
        }
        return ss.str();
    }
    
    protected:
    int _port;
};  

class sboutput : public sbnode {
    public:
    sboutput() : sbnode() {}
      
    void setPort(int port) {_port=port;}
    int port() const {return _port;}
    
    std::string name() const {
        std::stringstream ss;
        ss << "OP" << "_" << _port;
        return ss.str();
    }
    
    std::string gams_name(int config) const {
        std::stringstream ss;
        if(config!=0) {
          ss << "O" << _port << "i" << config;
        } else {
          ss << "O" << _port;
        }
        return ss.str();
    }
    
    protected:
    int _port;
};


class SubModel {
    public:
    
    //Port type of the substrate nodes
    //opensp -- dyser opensplyser N + N -1 ips
    //three ins -- Softbrain 3 x N
    //everywitch -- all switches has ops and ips
    enum class PortType {opensp, everysw, threein, threetwo};
    
    typedef std::vector<sbinput>::const_iterator  const_input_iterator;
    typedef std::vector<sboutput>::const_iterator const_output_iterator;
    
    SubModel(std::istream& istream, FuModel*, bool multi_config=true);
    SubModel(int x, int y, PortType pt=PortType::opensp, int ips=2, int ops=2, bool multi_config=true);
    
    void PrintGraphviz(std::ostream& ofs);
    void PrintGamsModel(std::ostream& ofs, 
                        std::unordered_map<std::string, std::pair<sbnode*,int> >&,
                        std::unordered_map<std::string, std::pair<sblink*,int> >&, 
                        std::unordered_map<std::string, std::pair<sbswitch*,int> >&, 
                        std::unordered_map<std::string, std::pair<bool,int>>&,  /*isInput, port*/
                        int n_configs=1);
    
    int sizex() {return _sizex;}
    int sizey() {return _sizey;}
    
    sbfu* fuAt(int x, int y) {return &(_fus[x][y]);}
    sbswitch* switchAt(int x, int y) {return &(_switches[x][y]);}

    sbinput*  get_input(int i)  {return &(_inputs[i]); }
    sboutput* get_output(int i) {return &(_outputs[i]);}

    const_input_iterator input_begin() { return _inputs.begin();}
    const_input_iterator input_end()  { return _inputs.end();}
    
    const_output_iterator output_begin()  { return _outputs.begin();}
    const_output_iterator output_end()  { return _outputs.end();}
    
    //const_output_iterator output_begin()  { return _outputs.begin();}
    //const_output_iterator output_end()  { return _outputs.end();}
    
    std::vector<std::vector<sbfu> >& fus() {return _fus;}
    std::vector<std::vector<sbswitch> >& switches() {return _switches;}
    
    bool multi_config() { return _multi_config;}
    
    sbswitch* cross_switch() {return &_cross_switch;}
    sbnode* load_slice() {return &_load_slice;}
    
    int num_fu() {return _fus.size();}
    
    void parse_io(std::istream& istream);
    sbio_interface& io_interf() {return _sbio_interf;}

    void clear_all_runtime_vals();
    void clear_fu_runtime_vals();

    sbnode* get_node_by_id(int i) {return _node_list[i];}
    sblink* get_link_by_id(int i) {return _link_list[i];}

    int num_node_ids() {return _node_list.size();}
    int num_link_ids() {return _link_list.size();}

    std::vector<sbinput>&   inputs()      {return _inputs;}
    std::vector<sboutput>&  outputs()     {return _outputs;}
    std::vector<sbfu*>&     fu_list()     {return _fu_list;}
    std::vector<sbswitch*>& switch_list() {return _switch_list;}
    std::vector<sbnode*>&   node_list()   {return _node_list;}

    private:

    void regroup_vecs(); //fills in the linear lists 

    //void CreateFUArray(int,int);
    
    //void SetTotalFUByRatio();
    //void RandDistributeFUs();
    void build_substrate(int x, int y);
    void connect_substrate(int x, int y, PortType pt, int ips, int ops,bool multi_config,int temp_x, int temp_y, int temp_width, int temp_height);

    int _sizex, _sizey;  //size of SB cgra
    bool _multi_config;
    std::vector<sbinput> _inputs;
    std::vector<sboutput> _outputs;
    std::vector<std::vector<sbfu> > _fus;
    std::vector<std::vector<sbswitch> > _switches;

    //These are only valid after regroup_vecs()
    std::vector<sbnode*> _io_list;
    std::vector<sbfu*> _fu_list;
    std::vector<sbswitch*> _switch_list;
    std::vector<sbnode*> _node_list;
    std::vector<sblink*> _link_list;

    sbswitch _cross_switch;
    sbnode _load_slice;
    sbio_interface _sbio_interf;
};

}

#endif
