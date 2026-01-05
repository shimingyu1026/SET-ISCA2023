/* This file contains
 *  ChipletTrace: Data structures and utilities for generating
 *                chiplet simulation traces with deadlock-free
 *                data transfer sequences.
 */

#ifndef CHIPLET_TRACE_H
#define CHIPLET_TRACE_H

#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "util.h"

// Forward declarations
class SchNode;
class LNode;
class Cut;

namespace ChipletTrace {

// Operation types for chiplet
enum class OpType {
  RECV,    // Receive data from peer
  COMPUTE, // Perform computation
  SEND     // Send data to peer
};

// Single operation in a chiplet's operation list
struct Operation {
  OpType type;
  int peer_id;            // Peer chiplet ID (-1 for DRAM, -2 for COMPUTE)
  std::string layer_name; // Related layer
  vol_t data_size;        // Data size in bytes (0 for COMPUTE)
  int transfer_id;        // Unique transfer ID for matching SEND/RECV pairs

  // Check if this operation matches with another (SEND matches RECV)
  bool matches(const Operation &other) const {
    if (type == OpType::SEND && other.type == OpType::RECV) {
      return transfer_id == other.transfer_id;
    }
    if (type == OpType::RECV && other.type == OpType::SEND) {
      return transfer_id == other.transfer_id;
    }
    return false;
  }
};

// Computation task description
struct ComputationTask {
  std::string layer_name;
  std::string layer_type; // conv, pool, fc, etc.

  // Actual computation shape parameters
  len_t ifmap_h, ifmap_w;   // Input feature map H/W
  len_t filter_h, filter_w; // Filter H/W
  len_t channels;           // Input channels
  len_t num_filters;        // Output channels (number of filters)
  len_t stride_h, stride_w; // Strides

  // Extra layer-specific info (e.g., G=25 for groupconv, N=2 for eltwise)
  std::string extra_info;

  // Partition info
  len_t batch_from, batch_to; // Batch range
  len_t c_from, c_to;         // Channel range
  len_t h_from, h_to;         // Height range
  len_t w_from, w_to;         // Width range
};

// Single chiplet's trace
struct SingleChipletTrace {
  int chiplet_id;
  mlen_t pos_x, pos_y; // Position in mesh

  std::vector<ComputationTask> computations;
  std::deque<Operation> operations; // Ordered operation list
};

// Complete trace for all chiplets
struct FullTrace {
  mlen_t mesh_x, mesh_y; // Mesh dimensions
  std::string network_name;
  len_t total_batch;

  std::vector<SingleChipletTrace> chiplet_traces;

  // Verify deadlock-free property using matching elimination
  bool verify_deadlock_free() const;

  // Output trace to stream
  void print(std::ostream &os) const;
};

// Generator class
class TraceGenerator {
public:
  TraceGenerator(const SchNode *root, mlen_t xlen, mlen_t ylen);

  // Generate the full trace
  FullTrace generate();

private:
  const SchNode *root_;
  mlen_t xlen_, ylen_;
  int next_transfer_id_;

  // Collect all computation tasks recursively with xyid mapping
  void collect_computations(const SchNode *node,
                            std::vector<SingleChipletTrace> &traces,
                            len_t batch_offset,
                            const std::map<cidx_t, cidx_t> &xyid_to_idx);

  // Generate operations with actual chiplet-to-chiplet transfers
  void generate_operations(std::vector<SingleChipletTrace> &traces,
                           const std::map<cidx_t, cidx_t> &xyid_to_idx);

  // Collect data transfers between chiplets
  void collect_transfers(const SchNode *node,
                         std::vector<SingleChipletTrace> &traces,
                         len_t batch_offset,
                         const std::map<cidx_t, cidx_t> &xyid_to_idx);
};

} // namespace ChipletTrace

#endif // CHIPLET_TRACE_H
