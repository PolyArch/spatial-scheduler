#ifndef __INST_MODEL_H__
#define __INST_MODEL_H__

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace dsa {

// Instruction Class
// Stores attributes like it's name, latency, etc...
class ConfigInst {
 public:
  std::string name() { return _name; }
  void setName(std::string& name) { _name = name; }

  int bitwidth() { return _bitwidth; }
  void setBitwidth(int b) { _bitwidth = b; }

  int numOps() { return _num_ops; }
  void setNumOperands(int n_ops) { _num_ops = n_ops; }

  int numValues() { return _num_values; }
  void setNumValues(int n_values) { _num_values = n_values; }

  int latency() { return _latency; }
  void setLatency(int lat) { _latency = lat; }

  int throughput() { return _throughput; }
  void setThroughput(int thr) { _throughput = thr; }

  double area() { return _area; }
  void setArea(double area) { _area = area; }

  double power() { return _power; }
  void setPower(double power) { _power = power; }

 private:
  std::string _name;
  int _latency;
  int _throughput;  // technically 1/throughput in cycles
  int _num_values;
  int _num_ops;
  int _bitwidth;
  double _area = -1.0;
  double _power = -1.0;
};

class FuType {
 public:
  std::string name() { return _name; }
  void setName(std::string name) { _name = name; }

  double area() { return _area; }
  void setArea(double area) { _area = area; }

  double power() { return _power; }
  void setPower(double power) { _power = power; }

 private:
  std::string _name;
  double _area = -1.0;
  double _power = -1.0;
};

class InstModel {
 public:
  InstModel(char* filename);  // read the file and populate the instructions
  void PowerAreaModel(char* filename);

  void printCFiles(char* header, char* cpp);

 private:
  std::vector<ConfigInst*> _instList;
  std::vector<FuType*> _fuTypeList;
  std::string filename_;
  std::string base_folder;
};

}  // namespace dsa

#endif
