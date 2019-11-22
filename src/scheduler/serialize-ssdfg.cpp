#include "serialization.h"
#include "ssdfg.h"

// Boost Includes
#include <boost/archive/text_iarchive.hpp>
#include <boost/archive/text_oarchive.hpp>

#include <boost/serialization/base_object.hpp>
#include <boost/serialization/bitset.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/map.hpp>
#include <boost/serialization/set.hpp>
#include <boost/serialization/vector.hpp>

template <class Archive>
void SSDfgValue::serialize(Archive& ar, const unsigned version) {
  ar& BOOST_SERIALIZATION_NVP(_node);
  ar& BOOST_SERIALIZATION_NVP(_index);
  ar& BOOST_SERIALIZATION_NVP(_bitwidth);
  ar& BOOST_SERIALIZATION_NVP(_uses);
}

template <class Archive>
void SSDfgEdge::serialize(Archive& ar, const unsigned version) {
  ar& BOOST_SERIALIZATION_NVP(_ID);
  ar& BOOST_SERIALIZATION_NVP(_ssdfg);
  ar& BOOST_SERIALIZATION_NVP(_value);
  ar& BOOST_SERIALIZATION_NVP(_use);
  ar& BOOST_SERIALIZATION_NVP(_etype);
  ar& BOOST_SERIALIZATION_NVP(_l);
  ar& BOOST_SERIALIZATION_NVP(_r);
}

template <class Archive>
void SSDfgOperand::serialize(Archive& ar, const unsigned version) {
  ar& BOOST_SERIALIZATION_NVP(edges);
  ar& BOOST_SERIALIZATION_NVP(imm);
}

template <class Archive>
void SSDfgNode::serialize(Archive& ar, const unsigned version) {
  ar& BOOST_SERIALIZATION_NVP(_ssdfg);
  ar& BOOST_SERIALIZATION_NVP(_ID);
  ar& BOOST_SERIALIZATION_NVP(_name);
  ar& BOOST_SERIALIZATION_NVP(_ops);
  ar& BOOST_SERIALIZATION_NVP(_inc_edge_list);
  ar& BOOST_SERIALIZATION_NVP(_values);
  ar& BOOST_SERIALIZATION_NVP(_uses);
  ar& BOOST_SERIALIZATION_NVP(_needs_ctrl_dep);
  ar& BOOST_SERIALIZATION_NVP(_min_lat);
  ar& BOOST_SERIALIZATION_NVP(_sched_lat);
  ar& BOOST_SERIALIZATION_NVP(_max_thr);
  ar& BOOST_SERIALIZATION_NVP(_group_id);
  ar& BOOST_SERIALIZATION_NVP(_vtype);
}

template <class Archive>
void CtrlBits::serialize(Archive& ar, const unsigned version) {
  ar& BOOST_SERIALIZATION_NVP(mask);
}

template <class Archive>
void SSDfgInst::serialize(Archive& ar, const unsigned version) {
  ar& BOOST_SERIALIZATION_BASE_OBJECT_NVP(SSDfgNode);
  ar& BOOST_SERIALIZATION_NVP(_predInv);
  ar& BOOST_SERIALIZATION_NVP(_isDummy);
  ar& BOOST_SERIALIZATION_NVP(_imm_slot);
  ar& BOOST_SERIALIZATION_NVP(_subFunc);
  ar& BOOST_SERIALIZATION_NVP(_ctrl_bits);
  ar& BOOST_SERIALIZATION_NVP(_self_bits);
  ar& BOOST_SERIALIZATION_NVP(_reg);
  ar& BOOST_SERIALIZATION_NVP(_imm);
  ar& BOOST_SERIALIZATION_NVP(_ssinst);
}

template <class Archive>
void SSDfgVec::serialize(Archive& ar, const unsigned version) {
  ar& BOOST_SERIALIZATION_BASE_OBJECT_NVP(SSDfgNode);
  ar& BOOST_SERIALIZATION_NVP(_bitwidth);
  ar& BOOST_SERIALIZATION_NVP(_port_width);
  ar& BOOST_SERIALIZATION_NVP(_vp_len);
}

template <class Archive>
void SSDfgVecInput::serialize(Archive& ar, const unsigned version) {
  ar& BOOST_SERIALIZATION_BASE_OBJECT_NVP(SSDfgVec);
}

template <class Archive>
void SSDfgVecOutput::serialize(Archive& ar, const unsigned version) {
  ar& BOOST_SERIALIZATION_BASE_OBJECT_NVP(SSDfgVec);
}

template <class Archive>
void GroupProp::serialize(Archive& ar, const unsigned version) {
  ar& BOOST_SERIALIZATION_NVP(is_temporal);
  ar& BOOST_SERIALIZATION_NVP(frequency);
  ar& BOOST_SERIALIZATION_NVP(unroll);
}

template <class Archive>
void SSDfg::serialize(Archive& ar, const unsigned version) {
  ar& BOOST_SERIALIZATION_NVP(_nodes);
  ar& BOOST_SERIALIZATION_NVP(_insts);
  ar& BOOST_SERIALIZATION_NVP(_orderedNodes);
  ar& BOOST_SERIALIZATION_NVP(_vecInputs);
  ar& BOOST_SERIALIZATION_NVP(_vecOutputs);
  ar& BOOST_SERIALIZATION_NVP(_edges);
  ar& BOOST_SERIALIZATION_NVP(_vecInputGroups);
  ar& BOOST_SERIALIZATION_NVP(_vecOutputGroups);
  ar& BOOST_SERIALIZATION_NVP(_groupProps);
}

// Boost Stuff
BOOST_CLASS_EXPORT_GUID(SSDfgEdge, "SSDfgEdge");
BOOST_CLASS_EXPORT_GUID(SSDfgNode, "SSDfgNode")
BOOST_CLASS_EXPORT_GUID(SSDfgInst, "SSDfgInst");
BOOST_CLASS_EXPORT_GUID(SSDfgVec, "SSDfgVec")
BOOST_CLASS_EXPORT_GUID(SSDfgVecInput, "SSDfgVecInput")
BOOST_CLASS_EXPORT_GUID(SSDfgVecOutput, "SSDfgVecOutput")
BOOST_CLASS_EXPORT_GUID(SSDfg, "SSDfg")

SERIALIZABLE(SSDfgEdge);
SERIALIZABLE(SSDfgNode);
SERIALIZABLE(SSDfgInst);
SERIALIZABLE(SSDfgVec);
SERIALIZABLE(SSDfgVecInput);
SERIALIZABLE(SSDfgVecOutput);
SERIALIZABLE(SSDfg);
