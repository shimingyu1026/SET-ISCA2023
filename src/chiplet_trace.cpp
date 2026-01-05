#include "chiplet_trace.h"
#include "schnode.h"

#include <algorithm>
#include <cassert>
#include <iomanip>
#include <iostream>
#include <map>

#include "cluster.h"
#include "layer.h"
#include "network.h"

namespace ChipletTrace {

// ============== FullTrace Implementation ==============

bool FullTrace::verify_deadlock_free() const {
  std::vector<std::deque<Operation>> chip_ops;
  chip_ops.reserve(chiplet_traces.size());
  for (const auto &trace : chiplet_traces) {
    chip_ops.push_back(trace.operations);
  }

  while (true) {
    bool all_empty = true;
    for (const auto &ops : chip_ops) {
      if (!ops.empty()) {
        all_empty = false;
        break;
      }
    }
    if (all_empty)
      return true;

    bool matched = false;
    for (size_t i = 0; i < chip_ops.size(); i++) {
      if (chip_ops[i].empty())
        continue;

      const Operation &op = chip_ops[i].front();
      if (op.type == OpType::COMPUTE) {
        chip_ops[i].pop_front();
        matched = true;
        break;
      }

      int peer = op.peer_id;
      if (peer < 0) {
        chip_ops[i].pop_front();
        matched = true;
        break;
      }

      if (peer >= 0 && peer < static_cast<int>(chip_ops.size()) &&
          !chip_ops[peer].empty()) {
        const Operation &peer_op = chip_ops[peer].front();
        if (op.matches(peer_op)) {
          chip_ops[i].pop_front();
          chip_ops[peer].pop_front();
          matched = true;
          break;
        }
      }
    }

    if (!matched) {
      std::cerr << "[Deadlock] Unable to match any operation at head:"
                << std::endl;
      for (size_t i = 0; i < chip_ops.size(); i++) {
        if (!chip_ops[i].empty()) {
          const auto &op = chip_ops[i].front();
          std::cerr << "  Chip " << i << ": "
                    << (op.type == OpType::SEND ? "SEND" : "RECV")
                    << " peer=" << op.peer_id << " tid=" << op.transfer_id
                    << std::endl;
        }
      }
      return false;
    }
  }
}

void FullTrace::print(std::ostream &os) const {
  os << "# Chiplet Simulation Trace\n";
  os << "# Mesh: " << static_cast<int>(mesh_x) << "x"
     << static_cast<int>(mesh_y) << "\n";
  os << "# Network: " << network_name << "\n";
  os << "# Total Batch: " << total_batch << "\n";
  os << "# Total Chiplets: " << chiplet_traces.size() << "\n\n";

  for (const auto &trace : chiplet_traces) {
    os << "===== CHIPLET " << trace.chiplet_id << " ("
       << static_cast<int>(trace.pos_x) << "," << static_cast<int>(trace.pos_y)
       << ") =====\n\n";

    os << "[COMPUTATIONS]\n";
    os << "# Layer | Type | IFMAP_H | IFMAP_W | Filter_H | Filter_W | Channels "
          "| NumFilters | Stride_H | Stride_W | Extra | Batch | OutputRange\n";
    for (const auto &comp : trace.computations) {
      os << comp.layer_name << " | " << comp.layer_type << " | " << comp.ifmap_h
         << " | " << comp.ifmap_w << " | " << comp.filter_h << " | "
         << comp.filter_w << " | " << comp.channels << " | " << comp.num_filters
         << " | " << comp.stride_h << " | " << comp.stride_w << " | "
         << (comp.extra_info.empty() ? "-" : comp.extra_info) << " | "
         << "[" << comp.batch_from << "," << comp.batch_to << ") | "
         << "C[" << comp.c_from << "," << comp.c_to << ")"
         << "H[" << comp.h_from << "," << comp.h_to << ")"
         << "W[" << comp.w_from << "," << comp.w_to << ")\n";
    }

    os << "\n[ORDERED_OPERATIONS]\n";
    os << "# Seq | Type    | Peer | Layer           | Size     | TransferID\n";
    int seq = 0;
    for (const auto &op : trace.operations) {
      os << std::setw(5) << seq++ << " | ";
      switch (op.type) {
      case OpType::RECV:
        os << "RECV    | ";
        break;
      case OpType::COMPUTE:
        os << "COMPUTE | ";
        break;
      case OpType::SEND:
        os << "SEND    | ";
        break;
      }

      if (op.peer_id == -1) {
        os << "DRAM";
      } else if (op.peer_id == -2) {
        os << "-   ";
      } else {
        os << std::setw(4) << op.peer_id;
      }
      os << " | " << std::setw(15) << op.layer_name;
      os << " | " << std::setw(8) << op.data_size;
      os << " | T" << op.transfer_id << "\n";
    }
    os << "\n";
  }
}

// ============== TraceGenerator Implementation ==============

TraceGenerator::TraceGenerator(const SchNode *root, mlen_t xlen, mlen_t ylen)
    : root_(root), xlen_(xlen), ylen_(ylen), next_transfer_id_(0) {}

FullTrace TraceGenerator::generate() {
  FullTrace trace;
  trace.mesh_x = xlen_;
  trace.mesh_y = ylen_;
  trace.network_name = "DNN";
  trace.total_batch = SchNode::tot_batch;

  cidx_t num_real_chips = xlen_ * ylen_;
  trace.chiplet_traces.resize(num_real_chips);

  // Create xyid to idx mapping
  std::map<cidx_t, cidx_t> xyid_to_idx;
  for (mlen_t y = 0; y < ylen_; y++) {
    for (mlen_t x = 0; x < xlen_; x++) {
      cidx_t real_chip_idx = y * xlen_ + x;
      pos_t pos = {static_cast<mlen_t>(x), y};
      cidx_t xyid = Cluster::get_xyid(pos);
      xyid_to_idx[xyid] = real_chip_idx;

      auto &ct = trace.chiplet_traces[real_chip_idx];
      ct.chiplet_id = real_chip_idx;
      ct.pos_x = x;
      ct.pos_y = y;
    }
  }

  // Collect computations and transfers
  collect_computations(root_, trace.chiplet_traces, 0, xyid_to_idx);

  // Generate operations with actual chiplet-to-chiplet transfers
  generate_operations(trace.chiplet_traces, xyid_to_idx);

  return trace;
}

void TraceGenerator::collect_computations(
    const SchNode *node, std::vector<SingleChipletTrace> &traces,
    len_t batch_offset, const std::map<cidx_t, cidx_t> &xyid_to_idx) {
  if (node == nullptr)
    return;

  if (node->get_type() == SchNode::NodeType::L) {
    const LNode *lnode = static_cast<const LNode *>(node);
    const Node &layer_node = lnode->getLayer();
    const Layer &l = layer_node.layer();
    const PlaceSch &place = lnode->get_place_sch();

    ComputationTask task;
    task.layer_name = layer_node.name();

    if (REF_IS_INSTANCE(l, ConvLayer)) {
      // Check for GroupConvLayer first (it's a subclass of ConvLayer)
      if (REF_IS_INSTANCE(l, GroupConvLayer)) {
        const GroupConvLayer &gconv = static_cast<const GroupConvLayer &>(l);
        auto wl = gconv.get_workload();
        task.layer_type = "groupconv";
        task.ifmap_h = wl.H;
        task.ifmap_w = wl.W;
        task.filter_h = wl.R;
        task.filter_w = wl.S;
        task.channels = wl.C;
        task.num_filters = wl.K;
        task.stride_h = wl.sH;
        task.stride_w = wl.sW;
        // Extra info: G (group count), GC (channels per group), GK (filters per
        // group)
        task.extra_info = "G=" + std::to_string(wl.G) +
                          ",GC=" + std::to_string(wl.GC) +
                          ",GK=" + std::to_string(wl.GK);
      } else if (REF_IS_INSTANCE(l, FCLayer)) {
        // FCLayer is also a subclass of ConvLayer
        const ConvLayer &conv = static_cast<const ConvLayer &>(l);
        auto wl = conv.get_workload();
        task.layer_type = "fc";
        task.ifmap_h = l.tot_ifmap_shape().h;
        task.ifmap_w = l.tot_ifmap_shape().w;
        task.filter_h = task.ifmap_h;
        task.filter_w = task.ifmap_w;
        task.channels = l.tot_ifmap_shape().c;
        task.num_filters = l.ofmap_shape().c;
        task.stride_h = 1;
        task.stride_w = 1;
        // Extra info: actual filter dimensions
        task.extra_info =
            "R=" + std::to_string(wl.R) + ",S=" + std::to_string(wl.S);
      } else {
        const ConvLayer &conv = static_cast<const ConvLayer &>(l);
        auto wl = conv.get_workload();
        task.layer_type = "conv";
        task.ifmap_h = wl.H;
        task.ifmap_w = wl.W;
        task.filter_h = wl.R;
        task.filter_w = wl.S;
        task.channels = wl.C;
        task.num_filters = wl.K;
        task.stride_h = wl.sH;
        task.stride_w = wl.sW;
        task.extra_info = ""; // Standard conv, no extra info needed
      }
    } else if (REF_IS_INSTANCE(l, PoolingLayer)) {
      const PoolingLayer &pool = static_cast<const PoolingLayer &>(l);
      auto wl = pool.get_workload();
      task.layer_type = "pool";
      task.ifmap_h = wl.H;
      task.ifmap_w = wl.W;
      task.filter_h = wl.R;
      task.filter_w = wl.S;
      task.channels = wl.K;
      task.num_filters = wl.K;
      task.stride_h = wl.sH;
      task.stride_w = wl.sW;
      // Extra info: pool window size
      task.extra_info =
          "pool=" + std::to_string(wl.R) + "x" + std::to_string(wl.S);
    } else if (REF_IS_INSTANCE(l, EltwiseLayer)) {
      const LRLayer &lr = static_cast<const LRLayer &>(l);
      auto wl = lr.get_workload();
      task.layer_type = "eltwise";
      task.ifmap_h = l.tot_ifmap_shape().h;
      task.ifmap_w = l.tot_ifmap_shape().w;
      task.filter_h = 0;
      task.filter_w = 0;
      task.channels = l.tot_ifmap_shape().c;
      task.num_filters = l.ofmap_shape().c;
      task.stride_h = 1;
      task.stride_w = 1;
      // Extra info: N (number of inputs to combine)
      task.extra_info = "N=" + std::to_string(wl.N);
    } else if (REF_IS_INSTANCE(l, PTPLayer)) {
      task.layer_type = "ptp";
      task.ifmap_h = l.tot_ifmap_shape().h;
      task.ifmap_w = l.tot_ifmap_shape().w;
      task.filter_h = 0;
      task.filter_w = 0;
      task.channels = l.tot_ifmap_shape().c;
      task.num_filters = l.ofmap_shape().c;
      task.stride_h = 1;
      task.stride_w = 1;
      task.extra_info = ""; // Point-to-point, no extra params
    } else if (REF_IS_INSTANCE(l, TransposeLayer)) {
      task.layer_type = "transpose";
      task.ifmap_h = l.tot_ifmap_shape().h;
      task.ifmap_w = l.tot_ifmap_shape().w;
      task.filter_h = 0;
      task.filter_w = 0;
      task.channels = l.tot_ifmap_shape().c;
      task.num_filters = l.ofmap_shape().c;
      task.stride_h = 1;
      task.stride_w = 1;
      task.extra_info = ""; // Transpose, dimensions already shown
    } else {
      task.layer_type = "other";
      task.ifmap_h = l.tot_ifmap_shape().h;
      task.ifmap_w = l.tot_ifmap_shape().w;
      task.filter_h = 0;
      task.filter_w = 0;
      task.channels = l.tot_ifmap_shape().c;
      task.num_filters = l.ofmap_shape().c;
      task.stride_h = 1;
      task.stride_w = 1;
      task.extra_info = "";
    }

    const auto &ofm_layout = place.getOfmL();
    for (auto it = ofm_layout.begin(); it != ofm_layout.end(); ++it) {
      auto [range, pos] = *it;
      if (range.is_empty())
        continue;

      cidx_t xyid = Cluster::get_xyid(pos);
      auto map_it = xyid_to_idx.find(xyid);
      if (map_it == xyid_to_idx.end())
        continue;

      cidx_t trace_idx = map_it->second;

      ComputationTask part_task = task;
      part_task.batch_from = range.b.from + batch_offset;
      part_task.batch_to = range.b.to + batch_offset;
      part_task.c_from = range.c.from;
      part_task.c_to = range.c.to;
      part_task.h_from = range.h.from;
      part_task.h_to = range.h.to;
      part_task.w_from = range.w.from;
      part_task.w_to = range.w.to;

      traces[trace_idx].computations.push_back(part_task);
    }
  } else {
    const Cut *cut = static_cast<const Cut *>(node);
    len_t bgrp = cut->get_num_bgrp();
    len_t batch_per_grp = SchNode::tot_batch / bgrp;

    for (len_t b = 0; b < bgrp; b++) {
      for (auto child : cut->getChildren()) {
        collect_computations(child, traces, batch_offset + b * batch_per_grp,
                             xyid_to_idx);
      }
    }
  }
}

void TraceGenerator::generate_operations(
    std::vector<SingleChipletTrace> &traces,
    const std::map<cidx_t, cidx_t> &xyid_to_idx) {

  // Collect all transfers by analyzing data dependencies
  collect_transfers(root_, traces, 0, xyid_to_idx);
}

void TraceGenerator::collect_transfers(
    const SchNode *node, std::vector<SingleChipletTrace> &traces,
    len_t batch_offset, const std::map<cidx_t, cidx_t> &xyid_to_idx) {
  if (node == nullptr)
    return;

  if (node->get_type() == SchNode::NodeType::L) {
    const LNode *lnode = static_cast<const LNode *>(node);
    const Node &layer_node = lnode->getLayer();
    const PlaceSch &place = lnode->get_place_sch();
    const Bitset &dirp_set = lnode->get_dirp_set();

    // Get previous layers
    Bitset prev_layers = layer_node.getPrevs();
    bool has_prev_layer = (prev_layers.count() != 0);

    // For each output partition of this layer
    const auto &ofm_layout = place.getOfmL();
    for (auto it = ofm_layout.begin(); it != ofm_layout.end(); ++it) {
      auto [ofmap_range, pos] = *it;
      if (ofmap_range.is_empty())
        continue;

      cidx_t to_xyid = Cluster::get_xyid(pos);
      auto to_map_it = xyid_to_idx.find(to_xyid);
      if (to_map_it == xyid_to_idx.end())
        continue;
      cidx_t to_idx = to_map_it->second;

      // Check if data comes from previous layer chiplets or DRAM
      if (has_prev_layer) {
        // Analyze where input data comes from
        FOR_BITSET(prev_layerno, prev_layers) {
          const LNode *prev_lnode = root_->get_lnode_by_id(prev_layerno);
          if (prev_lnode == nullptr)
            continue;

          bool is_direct_prev = dirp_set.contains(prev_layerno);

          if (is_direct_prev) {
            // Data comes from another chiplet directly
            const auto &prev_ofm_layout = prev_lnode->get_place_sch().getOfmL();

            for (auto prev_it = prev_ofm_layout.begin();
                 prev_it != prev_ofm_layout.end(); ++prev_it) {
              auto [prev_range, prev_pos] = *prev_it;
              if (prev_range.is_empty())
                continue;

              cidx_t from_xyid = Cluster::get_xyid(prev_pos);
              auto from_map_it = xyid_to_idx.find(from_xyid);
              if (from_map_it == xyid_to_idx.end())
                continue;
              cidx_t from_idx = from_map_it->second;

              // Calculate intersection to determine if transfer needed
              vol_t transfer_size =
                  prev_range.size() * 8; // 8 bytes per element

              if (from_idx != to_idx && transfer_size > 0) {
                int tid = next_transfer_id_++;

                // Add RECV at destination
                Operation recv_op;
                recv_op.type = OpType::RECV;
                recv_op.peer_id = from_idx;
                recv_op.layer_name = layer_node.name() + "_from_" +
                                     network->getNode(prev_layerno).name();
                recv_op.data_size = transfer_size;
                recv_op.transfer_id = tid;
                traces[to_idx].operations.push_back(recv_op);

                // Add SEND at source
                Operation send_op;
                send_op.type = OpType::SEND;
                send_op.peer_id = to_idx;
                send_op.layer_name = network->getNode(prev_layerno).name() +
                                     "_to_" + layer_node.name();
                send_op.data_size = transfer_size;
                send_op.transfer_id = tid;
                traces[from_idx].operations.push_back(send_op);
              }
            }
          } else {
            // Data comes from DRAM (not direct predecessor)
            vol_t ifmap_size = ofmap_range.size() * 8;

            Operation recv_op;
            recv_op.type = OpType::RECV;
            recv_op.peer_id = -1; // DRAM
            recv_op.layer_name = layer_node.name() + "_ifmap";
            recv_op.data_size = ifmap_size;
            recv_op.transfer_id = next_transfer_id_++;
            traces[to_idx].operations.push_back(recv_op);
          }
        }
      } else {
        // First layer - data from DRAM
        vol_t ifmap_size = ofmap_range.size() * 8;

        Operation recv_op;
        recv_op.type = OpType::RECV;
        recv_op.peer_id = -1; // DRAM
        recv_op.layer_name = layer_node.name() + "_ifmap";
        recv_op.data_size = ifmap_size;
        recv_op.transfer_id = next_transfer_id_++;
        traces[to_idx].operations.push_back(recv_op);
      }

      // Add COMPUTE operation
      Operation comp_op;
      comp_op.type = OpType::COMPUTE;
      comp_op.peer_id = -2;
      comp_op.layer_name = layer_node.name();
      comp_op.data_size = 0;
      comp_op.transfer_id = -1;
      traces[to_idx].operations.push_back(comp_op);
    }
  } else {
    const Cut *cut = static_cast<const Cut *>(node);
    len_t bgrp = cut->get_num_bgrp();
    len_t batch_per_grp = SchNode::tot_batch / bgrp;

    for (len_t b = 0; b < bgrp; b++) {
      for (auto child : cut->getChildren()) {
        collect_transfers(child, traces, batch_offset + b * batch_per_grp,
                          xyid_to_idx);
      }
    }
  }
}

} // namespace ChipletTrace

#ifndef NOT_GEN_IR
void SchNode::gen_chiplet_trace(std::ostream &os) const {
  ChipletTrace::TraceGenerator generator(this, Cluster::xlen, Cluster::ylen);
  auto trace = generator.generate();

  if (!trace.verify_deadlock_free()) {
    std::cerr << "[Warning] Trace may contain deadlocks!" << std::endl;
  }

  trace.print(os);
}
#endif
