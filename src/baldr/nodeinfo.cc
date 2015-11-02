#include "baldr/nodeinfo.h"
#include <boost/functional/hash.hpp>
#include <cmath>

#include <baldr/datetime.h>
#include <baldr/graphtile.h>

using namespace valhalla::baldr;

namespace {

const uint32_t ContinuityLookup[] = {0, 7, 13, 18, 22, 25, 27};

json::MapPtr access_json(uint16_t access) {
  return json::map({
    {"bicycle", static_cast<bool>(access && kBicycleAccess)},
    {"bus", static_cast<bool>(access && kBusAccess)},
    {"car", static_cast<bool>(access && kAutoAccess)},
    {"emergency", static_cast<bool>(access && kEmergencyAccess)},
    {"HOV", static_cast<bool>(access && kHOVAccess)},
    {"pedestrian", static_cast<bool>(access && kPedestrianAccess)},
    {"taxi", static_cast<bool>(access && kTaxiAccess)},
    {"truck", static_cast<bool>(access && kTruckAccess)},
  });
}

json::MapPtr admin_json(const AdminInfo& admin, uint16_t tz_index) {
  //admin
  auto m = json::map({
    {"iso_3166-1", admin.country_iso()},
    {"country", admin.country_text()},
    {"iso_3166-2", admin.state_iso()},
    {"state", admin.state_text()},
  });

  //timezone
  const auto& tz_db = DateTime::get_tz_db();
  auto tz = tz_db.time_zone_from_region(tz_db.regions[tz_index]);
  if(tz) {
    //TODO: so much to do but posix tz has pretty much all the info
    m->emplace("time_zone_posix", tz->to_posix_string());
    m->emplace("standard_time_zone_name", tz->std_zone_name());
    if(tz->has_dst())
      m->emplace("daylight_savings_time_zone_name", tz->dst_zone_name());
  }

  return m;
}

}

namespace valhalla {
namespace baldr {

// Default constructor. Initialize to all 0's.
NodeInfo::NodeInfo() {
  memset(this, 0, sizeof(NodeInfo));
}

// Get the latitude, longitude
const PointLL& NodeInfo::latlng() const {
  return static_cast<const PointLL&>(latlng_);
}

// Get the index in this tile of the first outbound directed edge
uint32_t NodeInfo::edge_index() const {
  return edge_index_;
}

// Get the number of outbound edges from this node.
uint32_t NodeInfo::edge_count() const {
  return edge_count_;
}

// Get the best road class of any outbound edges.
RoadClass NodeInfo::bestrc() const {
  return static_cast<RoadClass>(bestrc_);
}

// Get the access modes (bit mask) allowed to pass through the node.
// See graphconstants.h
uint16_t NodeInfo::access() const {
  return access_;
}

// Get the intersection type.
IntersectionType NodeInfo::intersection() const {
  return static_cast<IntersectionType>(intersection_);
}

// Get the index of the administrative information within this tile.
uint32_t NodeInfo::admin_index() const {
  return admin_index_;
}

// Returns the timezone index.
uint32_t NodeInfo::timezone() const {
  return timezone_;
}

// Get the driveability of the local directed edge given a local
// edge index.
Traversability NodeInfo::local_driveability(const uint32_t localidx) const {
  uint32_t s = localidx * 2;     // 2 bits per index
  return static_cast<Traversability>((local_driveability_ & (3 << s)) >> s);
}

// Get the relative density at the node.
uint32_t NodeInfo::density() const {
  return density_;
}

// Gets the node type. See graphconstants.h for the list of types.
NodeType NodeInfo::type() const {
  return static_cast<NodeType>(type_);
}

// Checks if this node is a transit node.
bool NodeInfo::is_transit() const {
  NodeType nt = type();
  return (nt == NodeType::kRailStop || nt == NodeType::kBusStop ||
          nt == NodeType::kMultiUseTransitStop);
}

// Get the number of edges on the local level. We add 1 to allow up to
// up to kMaxLocalEdgeIndex + 1.
uint32_t NodeInfo::local_edge_count() const {
  return local_edge_count_ + 1;
}

// Is this a parent node (e.g. a parent transit stop).
bool NodeInfo::parent() const {
  return parent_;
}

// Is this a child node (e.g. a child transit stop).
bool NodeInfo::child() const {
  return child_;
}

// Is a mode change allowed at this node? The access data tells which
// modes are allowed at the node. Examples include transit stops, bike
// share locations, and parking locations.
bool NodeInfo::mode_change() const {
  return mode_change_;
}

// Is there a traffic signal at this node?
bool NodeInfo::traffic_signal() const {
  return traffic_signal_;
}

// Gets the transit stop index. This is used for schedule lookups
// and possibly queries to a transit service.
uint32_t NodeInfo::stop_index() const {
  return stop_.stop_index;
}

// Get the name consistency between a pair of local edges. This is limited
// to the first kMaxLocalEdgeIndex local edge indexes.
bool NodeInfo::name_consistency(const uint32_t from, const uint32_t to) const {
  if (from == to) {
    return true;
  } else if (from < to) {
    return (to > kMaxLocalEdgeIndex) ? false :
        (stop_.name_consistency & 1 << (ContinuityLookup[from] + (to-from-1)));
  } else {
    return (from > kMaxLocalEdgeIndex) ? false :
        (stop_.name_consistency & 1 << (ContinuityLookup[to] + (from-to-1)));
  }
}

// Get the heading of the local edge given its local index. Supports
// up to 8 local edges. Headings are expanded from 8 bits.
uint32_t NodeInfo::heading(const uint32_t localidx) const {
  // Make sure everything is 64 bit!
  uint64_t shift = localidx * 8;     // 8 bits per index
  return static_cast<uint32_t>(std::round(
      ((headings_ & (static_cast<uint64_t>(255) << shift)) >> shift)
          * kHeadingExpandFactor));
}


json::MapPtr NodeInfo::json(const GraphTile* tile) const {
  auto m = json::map({
    {"lon", json::fp_t{latlng_.first, 6}},
    {"lat", json::fp_t{latlng_.second, 6}},
    {"best_road_class", to_string(static_cast<RoadClass>(bestrc_))},
    {"edge_count", static_cast<uint64_t>(edge_count_)},
    {"access", access_json(access_)},
    {"intersection_type", to_string(static_cast<IntersectionType>(intersection_))},
    {"administrative", admin_json(tile->admininfo(admin_index_), timezone_)},
    {"child", static_cast<uint64_t>(child_)},
    {"density", static_cast<uint64_t>(density_)},
    {"local_edge_count", static_cast<uint64_t>(local_edge_count_ + 1)},
    {"mode_change", static_cast<bool>(mode_change_)},
    {"parent", static_cast<bool>(parent_)},
    {"traffic_signal", static_cast<bool>(traffic_signal_)},
    {"type", to_string(static_cast<NodeType>(type_))},
  });
  if(is_transit())
    m->emplace("stop_index", static_cast<uint64_t>(stop_.stop_index));
  return m;
}


}
}
