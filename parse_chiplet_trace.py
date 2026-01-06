#!/usr/bin/env python3
"""
Chiplet Trace Parser - 简洁版本

只负责解析，使用统一的类结构表示数据
"""

from dataclasses import dataclass, field
from typing import List, Dict
import re


@dataclass
class Computation:
    """层计算信息"""

    layer: str
    type: str
    ifmap_h: int
    ifmap_w: int
    filter_h: int
    filter_w: int
    channels: int
    num_filters: int
    stride_h: int
    stride_w: int
    extra: str
    batch: str
    output_range: str


@dataclass
class Operation:
    """操作信息 (SEND/RECV/COMPUTE)"""

    seq: int
    op_type: str  # SEND, RECV, COMPUTE
    peer: str  # Chiplet ID, DRAM, 或 -
    layer: str
    size: int
    transfer_id: str


@dataclass
class Chiplet:
    """Chiplet 类 - 统一表示每个chiplet的所有信息"""

    chiplet_id: int
    row: int
    col: int
    computations: List[Computation] = field(default_factory=list)
    operations: List[Operation] = field(default_factory=list)


@dataclass
class ChipletTrace:
    """Complete trace data"""

    mesh_size: str
    network: str
    total_batch: int
    total_chiplets: int
    chiplets: Dict[int, Chiplet] = field(default_factory=dict)

    def remove_compute_operations(self):
        """删除所有 chiplets 中的 COMPUTE 操作"""
        for chiplet in self.chiplets.values():
            chiplet.operations = [
                op for op in chiplet.operations if op.op_type != "COMPUTE"
            ]

    def remove_dram_operations(self):
        """删除所有 chiplets 中 peer='DRAM' 的操作"""
        for chiplet in self.chiplets.values():
            chiplet.operations = [op for op in chiplet.operations if op.peer != "DRAM"]

    def check_pair(self):
        """
        删除 COMPUTE 和 DRAM 操作后，迭代消除配对的 SEND/RECV 操作
        配对规则：chiplet A 的第一个操作 SEND to B，chiplet B 的第一个操作 RECV from A
        """
        self.remove_compute_operations()
        self.remove_dram_operations()

        eliminated_count = 0
        while True:
            # 尝试找到一对可以消除的操作
            pair_found = False

            # 遍历所有 chiplet
            for chiplet_id, chiplet in self.chiplets.items():
                # 如果当前 chiplet 没有操作，跳过
                if not chiplet.operations:
                    continue

                first_op = chiplet.operations[0]

                # 只处理 SEND 操作
                if first_op.op_type == "SEND":
                    peer_id_str = first_op.peer

                    # 检查 peer 是否是有效的 chiplet ID
                    try:
                        peer_id = int(peer_id_str)
                    except ValueError:
                        continue

                    # 检查 peer chiplet 是否存在且有操作
                    if peer_id not in self.chiplets:
                        continue

                    peer_chiplet = self.chiplets[peer_id]
                    if not peer_chiplet.operations:
                        continue

                    peer_first_op = peer_chiplet.operations[0]

                    # 检查是否配对：peer 的第一个操作是 RECV from 当前 chiplet
                    if (
                        peer_first_op.op_type == "RECV"
                        and peer_first_op.peer == str(chiplet_id)
                        and peer_first_op.transfer_id == first_op.transfer_id
                    ):

                        # 找到配对，删除这两个操作
                        chiplet.operations.pop(0)
                        peer_chiplet.operations.pop(0)
                        eliminated_count += 1
                        pair_found = True
                        break  # 找到一对后重新开始

            # 如果没有找到任何可以配对的操作，退出循环
            if not pair_found:
                break

        return eliminated_count


def parse_trace(filename: str) -> ChipletTrace:
    """解析 trace 文件并返回数据结构"""
    with open(filename, "r") as f:
        lines = f.readlines()

    # 解析头部信息
    trace = ChipletTrace(mesh_size="", network="", total_batch=0, total_chiplets=0)

    for line in lines[:10]:
        line = line.strip()
        if line.startswith("# Mesh:"):
            trace.mesh_size = line.split(":")[1].strip()
        elif line.startswith("# Network:"):
            trace.network = line.split(":")[1].strip()
        elif line.startswith("# Total Batch:"):
            trace.total_batch = int(line.split(":")[1].strip())
        elif line.startswith("# Total Chiplets:"):
            trace.total_chiplets = int(line.split(":")[1].strip())

    # 解析 chiplets
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        if line.startswith("===== CHIPLET"):
            chiplet, next_i = _parse_chiplet(lines, i)
            trace.chiplets[chiplet.chiplet_id] = chiplet
            i = next_i
        else:
            i += 1

    return trace


def _parse_chiplet(lines: List[str], start_idx: int) -> tuple:
    """解析单个 chiplet"""
    # 解析 chiplet 头部 "===== CHIPLET 0 (0,0) ====="
    header_line = lines[start_idx].strip()
    match = re.search(r"CHIPLET (\d+) \((\d+),(\d+)\)", header_line)
    chiplet_id = int(match.group(1))
    row = int(match.group(2))
    col = int(match.group(3))

    chiplet = Chiplet(chiplet_id=chiplet_id, row=row, col=col)

    i = start_idx + 1
    section = None

    while i < len(lines):
        line = lines[i].strip()

        # 检查是否到达下一个 chiplet
        if line.startswith("===== CHIPLET"):
            break

        # 识别section
        if line == "[COMPUTATIONS]":
            section = "COMPUTATIONS"
            i += 1
            continue
        elif line == "[ORDERED_OPERATIONS]":
            section = "OPERATIONS"
            i += 1
            continue

        # 跳过注释和空行
        if line.startswith("#") or line == "":
            i += 1
            continue

        # 解析内容
        if section == "COMPUTATIONS":
            comp = _parse_computation(line)
            if comp:
                chiplet.computations.append(comp)
        elif section == "OPERATIONS":
            op = _parse_operation(line)
            if op:
                chiplet.operations.append(op)

        i += 1

    return chiplet, i


def _parse_computation(line: str) -> Computation:
    """解析计算行"""
    parts = [p.strip() for p in line.split("|")]
    if len(parts) < 13:
        return None

    try:
        return Computation(
            layer=parts[0],
            type=parts[1],
            ifmap_h=int(parts[2]),
            ifmap_w=int(parts[3]),
            filter_h=int(parts[4]),
            filter_w=int(parts[5]),
            channels=int(parts[6]),
            num_filters=int(parts[7]),
            stride_h=int(parts[8]),
            stride_w=int(parts[9]),
            extra=parts[10],
            batch=parts[11],
            output_range=parts[12],
        )
    except (ValueError, IndexError):
        return None


def _parse_operation(line: str) -> Operation:
    """解析操作行"""
    parts = [p.strip() for p in line.split("|")]
    if len(parts) < 6:
        return None

    try:
        return Operation(
            seq=int(parts[0]),
            op_type=parts[1],
            peer=parts[2],
            layer=parts[3],
            size=int(parts[4]),
            transfer_id=parts[5],
        )
    except (ValueError, IndexError):
        return None


# 简单的使用示例
if __name__ == "__main__":
    import sys

    # 默认文件路径
    trace_file = "bash_exp_SET_chiplet_trace.txt"
    if len(sys.argv) > 1:
        trace_file = sys.argv[1]

    # 解析
    trace = parse_trace(trace_file)

    # 打印基本信息
    print(f"Mesh: {trace.mesh_size}")
    print(f"Network: {trace.network}")
    print(f"Total Chiplets: {trace.total_chiplets}")
    print(f"Parsed {len(trace.chiplets)} chiplets")
    print()

    # 示例：访问 chiplet 0
    chiplet_0 = trace.chiplets.get(0)
    if chiplet_0:
        print(f"Chiplet 0 位置: ({chiplet_0.row}, {chiplet_0.col})")
        print(f"  计算层数: {len(chiplet_0.computations)}")
        print(f"  操作数: {len(chiplet_0.operations)}")
