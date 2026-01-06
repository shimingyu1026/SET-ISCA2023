from parse_chiplet_trace import *





# 解析文件
trace = parse_trace("bash_exp_SET_chiplet_trace.txt")

# 访问 chiplet
# chiplet_0 = trace.chiplets[0]
# print(chiplet_0.row, chiplet_0.col,chiplet_0.chiplet_id)  # 位置
# print(chiplet_0.computations[0])  # 计算列表
# trace.check_pair()

for chiplet_id, chiplet in trace.chiplets.items():
    print(f"chiplet {chiplet_id} has {len(chiplet.operations)} operations left")
    
