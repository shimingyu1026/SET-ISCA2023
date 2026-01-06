#!/usr/bin/env python3
"""测试 check_pair 配对消除功能"""

from parse_chiplet_trace import parse_trace

# 解析文件
trace = parse_trace("bash_exp_SET_chiplet_trace.txt")

print("=" * 60)
print("配对消除测试")
print("=" * 60)

# 统计初始操作数
total_ops_before = sum(len(c.operations) for c in trace.chiplets.values())
print(f"初始总操作数: {total_ops_before}")

# 查看几个 chiplet 的初始状态
for i in [0, 1, 2]:
    chiplet = trace.chiplets[i]
    print(f"  Chiplet {i}: {len(chiplet.operations)} 个操作")

print()

# 执行配对消除
eliminated_count = trace.check_pair()

print(f"消除了 {eliminated_count} 对配对操作")
print()

# 统计消除后操作数
total_ops_after = sum(len(c.operations) for c in trace.chiplets.values())
print(f"消除后总操作数: {total_ops_after}")
print(f"减少了: {total_ops_before - total_ops_after} 个操作")

# 查看几个 chiplet 的剩余操作
for i in [0, 1, 2]:
    chiplet = trace.chiplets[i]
    print(f"  Chiplet {i}: {len(chiplet.operations)} 个操作")
    if chiplet.operations:
        print(f"    第一个操作: {chiplet.operations[0].op_type} peer={chiplet.operations[0].peer}")

print()

# 检查是否还有可配对的操作（应该没有）
has_matching_pair = False
for chiplet_id, chiplet in trace.chiplets.items():
    if not chiplet.operations:
        continue
    first_op = chiplet.operations[0]
    if first_op.op_type == "SEND":
        try:
            peer_id = int(first_op.peer)
            if peer_id in trace.chiplets:
                peer_chiplet = trace.chiplets[peer_id]
                if peer_chiplet.operations:
                    peer_first = peer_chiplet.operations[0]
                    if (peer_first.op_type == "RECV" and 
                        peer_first.peer == str(chiplet_id) and
                        peer_first.transfer_id == first_op.transfer_id):
                        has_matching_pair = True
                        print(f"⚠️  发现未消除的配对: Chiplet {chiplet_id} <-> {peer_id}")
        except ValueError:
            pass

if not has_matching_pair:
    print("✓ 所有可配对的操作都已成功消除！")

print("=" * 60)
