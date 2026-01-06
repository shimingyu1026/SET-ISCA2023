#!/usr/bin/env python3
"""测试 remove_dram_operations 功能"""

from parse_chiplet_trace import parse_trace

# 解析文件
trace = parse_trace("bash_exp_SET_chiplet_trace.txt")

# 检查 chiplet 0 删除前的操作数
chiplet_0 = trace.chiplets[0]
print(f"删除前 Chiplet 0 的操作数: {len(chiplet_0.operations)}")

# 统计 DRAM 操作
dram_count = sum(1 for op in chiplet_0.operations if op.peer == "DRAM")
other_count = len(chiplet_0.operations) - dram_count

print(f"  - peer='DRAM': {dram_count}")
print(f"  - 其他 peer: {other_count}")

# 删除所有 DRAM 操作
trace.remove_dram_operations()

# 检查删除后的操作数
print(f"\n删除后 Chiplet 0 的操作数: {len(chiplet_0.operations)}")

# 验证没有 DRAM 操作了
dram_count_after = sum(1 for op in chiplet_0.operations if op.peer == "DRAM")
print(f"  - peer='DRAM': {dram_count_after}")

# 检查所有 chiplets
total_dram_ops = sum(1 for c in trace.chiplets.values() 
                     for op in c.operations if op.peer == "DRAM")
print(f"\n所有 chiplets 中剩余 DRAM 操作数: {total_dram_ops}")
print("✓ 成功删除所有 DRAM 操作！" if total_dram_ops == 0 else "✗ 仍有 DRAM 操作")
