// Copyright 2019 libsbn project contributors.
// libsbn is free software under the GPLv3; see LICENSE file for details.

#include "sbn_maps.hpp"
#include <algorithm>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>

SizeBitsetMap SBNMaps::IdIdSetMapOf(Node::NodePtr topology) {
  SizeBitsetMap map;
  auto id_count = topology->Id() + 1;
  topology->PostOrder([&map, id_count](const Node* node) {
    Bitset bitset(static_cast<size_t>(id_count));
    Assert(node->Id() < id_count, "Malformed ids in IdIdSetMapOf.");
    // Set the bit for the id of the current edge.
    bitset.set(node->Id());
    // Take the union of the children below.
    for (const auto& child : node->Children()) {
      bitset |= map.at(child->Id());
    }
    SafeInsert(map, node->Id(), std::move(bitset));
  });
  return map;
}

BitsetSizeDict SBNMaps::RootsplitCounterOf(const Node::TopologyCounter& topologies) {
  BitsetSizeDict rootsplit_counter(0);
  for (const auto& iter : topologies) {
    auto topology = iter.first;
    auto count = iter.second;
    auto Aux = [&rootsplit_counter, &count](const Node* n) {
      auto split = n->Leaves();
      split.Minorize();
      rootsplit_counter.increment(std::move(split), count);
    };
    for (const auto& child : topology->Children()) {
      child->PreOrder(Aux);
    }
  }
  return rootsplit_counter;
}

PCSSDict SBNMaps::PCSSCounterOf(const Node::TopologyCounter& topologies) {
  PCSSDict pcss_dict;
  for (const auto& iter : topologies) {
    auto topology = iter.first;
    auto count = iter.second;
    auto leaf_count = topology->LeafCount();
    Assert(topology->Children().size() == 3,
           "PCSSCounterOf was expecting a tree with a trifurcation at the root!");
    topology->PCSSPreOrder([&pcss_dict, &count, &leaf_count](
                               const Node* sister_node, bool sister_direction,
                               const Node* focal_node, bool focal_direction,  //
                               const Node* child0_node,
                               bool child0_direction,  //
                               const Node* child1_node, bool child1_direction,
                               const Node*  // ignore virtual root clade
                           ) {
      Bitset parent(2 * leaf_count, false);
      // The first chunk is for the sister node.
      parent.CopyFrom(sister_node->Leaves(), 0, sister_direction);
      // The second chunk is for the focal node.
      parent.CopyFrom(focal_node->Leaves(), leaf_count, focal_direction);
      // Now we build the child bitset.
      auto child0 = child0_node->Leaves();
      if (child0_direction) {
        child0.flip();
      }
      auto child1 = child1_node->Leaves();
      if (child1_direction) {
        child1.flip();
      }
      auto child = std::min(child0, child1);
      // Insert the parent-child pair into the map.
      auto search = pcss_dict.find(parent);
      if (search == pcss_dict.end()) {
        // The first time we have seen this parent.
        BitsetSizeDict child_singleton(0);
        child_singleton.increment(std::move(child), count);
        SafeInsert(pcss_dict, std::move(parent), child_singleton);
      } else {
        search->second.increment(std::move(child), count);
      }
    });
  }
  return pcss_dict;
}

SizeVector SBNMaps::SplitIndicesOf(const BitsetSizeMap& indexer,
                                   const Node::NodePtr& topology) {
  SizeVector split_result(topology->Id());
  topology->PreOrder([&topology, &split_result, &indexer](const Node* node) {
    // Skip the root.
    if (node != topology.get()) {
      Bitset rootsplit = node->Leaves();
      rootsplit.Minorize();
      split_result[node->Id()] = indexer.at(rootsplit);
    }
  });
  return split_result;
}

IndexerRepresentation SBNMaps::IndexerRepresentationOf(const BitsetSizeMap& indexer,
                                                       const Node::NodePtr& topology,
                                                       const size_t default_index) {
  const auto leaf_count = topology->LeafCount();
  // First, the rootsplits.
  SizeVector rootsplit_result = SBNMaps::SplitIndicesOf(indexer, topology);
  // We initialize each vector with the rootsplit index.
  SizeVectorVector result(topology->Id());
  std::transform(rootsplit_result.begin(), rootsplit_result.end(), result.begin(),
                 [&topology](const auto rootsplit) {
                   SizeVector v = {rootsplit};
                   // The number of PCSSs is less than number of internal nodes/2.
                   v.reserve(topology->Id() / 2);
                   return v;
                 });
  // Now we append the PCSSs.
  topology->PCSSPreOrder([&indexer, &default_index, &leaf_count, &result, &topology](
                             const Node* sister_node, bool sister_direction,
                             const Node* focal_node,
                             bool focal_direction,  //
                             const Node* child0_node,
                             bool child0_direction,  //
                             const Node* child1_node, bool child1_direction,
                             const Node* virtual_root_clade) {
    // Start by making the bitset representation of this PCSS.
    Bitset bitset(3 * leaf_count, false);
    bitset.CopyFrom(sister_node->Leaves(), 0, sister_direction);
    bitset.CopyFrom(focal_node->Leaves(), leaf_count, focal_direction);
    auto child0_bitset = child0_node->Leaves();
    if (child0_direction) {
      child0_bitset.flip();
    }
    auto child1_bitset = child1_node->Leaves();
    if (child1_direction) {
      child1_bitset.flip();
    }
    bitset.CopyFrom(std::min(child0_bitset, child1_bitset), 2 * leaf_count, false);
    const auto indexer_position = AtWithDefault(indexer, bitset, default_index);
    const auto& focal_index = focal_node->Id();
    if (sister_node == focal_node) {
      // We are in the bidirectional edge situation.
      Assert(focal_index < result.size(), "focal_index out of range.");
      // Rooting at the present edge will indeed lead to the given PCSS.
      result[focal_index].push_back(indexer_position);
    } else {
      // The only time the virtual root clade should be nullptr should be when
      // sister_node == focal_node, but we check anyhow.
      Assert(virtual_root_clade != nullptr, "virtual_root_clade is null.");
      // Virtual-rooting on every edge in the virtual rooting clade will also
      // lead to this PCSS, because then the root will be "above" the PCSS.
      virtual_root_clade->ConditionalPreOrder([&result, &indexer_position, &sister_node,
                                               &focal_node,
                                               &topology](const Node* node) {
        if (node == sister_node || node == focal_node) {
          // Don't enter the sister or focal clades. This is only
          // activated in the second case on the bottom row of pcss.svg:
          // we want to add everything in the clade above the focal node,
          // but nothing else.
          return false;
        }  // else
        // Add all of the edges of the virtual rooting clade, except for the
        // root of the topology.
        if (node != topology.get()) {
          Assert(node->Id() < result.size(), "node's root Id is out of range.");
          result[node->Id()].push_back(indexer_position);
        }
        return true;
      });
    }
  });
  return result;
}

IndexerRepresentationCounter SBNMaps::IndexerRepresentationCounterOf(
    const BitsetSizeMap& indexer, const Node::TopologyCounter& topology_counter,
    const size_t default_index) {
  IndexerRepresentationCounter counter;
  counter.reserve(topology_counter.size());
  for (const auto& [topology, topology_count] : topology_counter) {
    counter.push_back(
        {SBNMaps::IndexerRepresentationOf(indexer, topology, default_index),
         topology_count});
  }
  return counter;
}

StringPCSSMap SBNMaps::StringPCSSMapOf(PCSSDict d) {
  StringPCSSMap d_str;
  for (const auto& iter : d) {
    d_str[iter.first.ToString()] = StringifyMap(iter.second.Map());
  }
  return d_str;
}
