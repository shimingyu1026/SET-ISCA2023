# Chiplet Trace Parser

这是一个用于解析 chiplet 仿真 trace 文件的 Python 工具。

## 文件说明

- **`parse_chiplet_trace.py`**: 主要的解析器脚本
- **`example_usage.py`**: 使用示例，展示如何分析解析后的数据
- **`bash_exp_SET_chiplet_trace.txt`**: 输入的 trace 文件

## 数据结构

解析器使用以下数据类来组织数据：

### 1. `TraceHeader` - 全局配置
```python
- mesh_size: str          # 网格大小，如 "4x4"
- network: str            # 网络类型，如 "DNN"
- total_batch: int        # 批次数
- total_chiplets: int     # Chiplet 总数
```

### 2. `Computation` - 层计算信息
```python
- layer: str              # 层名称
- type: str               # 层类型 (conv, eltwise, pool)
- ifmap_h, ifmap_w: int   # 输入特征图尺寸
- filter_h, filter_w: int # 卷积核尺寸
- channels: int           # 输入通道数
- num_filters: int        # 输出滤波器数
- stride_h, stride_w: int # 步长
- batch: str              # 批次范围
- output_range: str       # 输出范围
```

### 3. `Operation` - 操作序列
```python
- seq: int                # 序列号
- op_type: str            # 操作类型 (SEND, RECV, COMPUTE)
- peer: str               # 对等节点 ID 或 "DRAM" 或 "-"
- layer: str              # 相关层名称
- size: int               # 数据大小（字节）
- transfer_id: str        # 传输 ID
```

### 4. `Chiplet` - Chiplet 信息
```python
- chiplet_id: int         # Chiplet ID
- position: tuple         # 位置 (row, col)
- computations: List[Computation]  # 计算列表
- operations: List[Operation]      # 操作列表
```

### 5. `ChipletTrace` - 完整 trace 数据
```python
- header: TraceHeader              # 头部信息
- chiplets: Dict[int, Chiplet]     # Chiplet 字典
```

## 快速使用

### 基本解析

```python
from parse_chiplet_trace import ChipletTraceParser

# 创建解析器并解析文件
parser = ChipletTraceParser("bash_exp_SET_chiplet_trace.txt")
trace = parser.parse()

# 打印摘要
parser.print_summary()

# 访问特定 chiplet
chiplet_0 = parser.get_chiplet(0)
print(f"Chiplet 0 has {len(chiplet_0.computations)} computations")
print(f"Chiplet 0 has {len(chiplet_0.operations)} operations")
```

### 运行示例

```bash
# 运行基本解析器
python3 parse_chiplet_trace.py

# 运行使用示例（包含多种分析）
python3 example_usage.py
```

## 使用场景示例

### 1. 统计操作类型
```python
op_counts = {"SEND": 0, "RECV": 0, "COMPUTE": 0}
for chiplet in trace.chiplets.values():
    for op in chiplet.operations:
        if op.op_type in op_counts:
            op_counts[op.op_type] += 1
```

### 2. 分析 Chiplet 间通信
```python
for chiplet_id, chiplet in trace.chiplets.items():
    for op in chiplet.operations:
        if op.op_type == "SEND" and op.peer not in ["DRAM", "-"]:
            peer_id = int(op.peer)
            print(f"Chiplet {chiplet_id} -> Chiplet {peer_id}: {op.size} bytes")
```

### 3. 计算 DRAM 访问量
```python
for chiplet_id, chiplet in trace.chiplets.items():
    dram_bytes = sum(op.size for op in chiplet.operations 
                     if op.op_type == "RECV" and op.peer == "DRAM")
    print(f"Chiplet {chiplet_id}: {dram_bytes / (1024*1024):.2f} MB from DRAM")
```

### 4. 查找特定层
```python
chiplet_0 = parser.get_chiplet(0)
conv_layers = [c for c in chiplet_0.computations if c.type == "conv"]
for comp in conv_layers:
    print(f"{comp.layer}: {comp.channels} -> {comp.num_filters} filters")
```

### 5. 导出为 JSON
```python
import json
from dataclasses import asdict

chiplet_dict = asdict(chiplet_0)
with open("chiplet_export.json", "w") as f:
    json.dump(chiplet_dict, f, indent=2)
```

## 解析的数据统计

从示例文件中解析出：
- **16 个 Chiplets** (4x4 网格)
- **679 个计算操作**
- **6,190 个 SEND 操作**
- **6,369 个 RECV 操作**
- **3 种层类型**: conv, eltwise, pool

## 扩展建议

你可以基于这个解析器实现更多功能：

1. **性能分析**: 计算关键路径、瓶颈识别
2. **可视化**: 生成通信图、数据流图
3. **优化建议**: 基于通信模式提出优化方案
4. **模拟器**: 使用解析的数据进行更详细的仿真
5. **比较分析**: 对比不同配置的 trace 文件

## 依赖

- Python 3.7+ (使用了 dataclasses)
- 标准库：`re`, `json`, `dataclasses`, `typing`

## 许可

此代码可自由使用和修改。
