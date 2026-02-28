# 曲线模块架构说明（自 CANACDebugger 移植）

## CANACDebugger (WPF) → OFile1 (Qt) 对应关系

| CANACDebugger | OFile1 | 说明 |
|---------------|--------|------|
| **CurvesControl** | **CurvesDialog** | 曲线主容器：左侧配置+图例，中间画布，底部滑块+状态栏 |
| **CurvesBuffer** | **CurveDataBuffer** | 环形缓冲区：Items[row][col], SampleTime[], CurvesMap(curveKey→row), CurrentPos, AddCurve, RemoveCurve, AddSample |
| **CurvesPanelHostX** | **CurvesWidget** | 绘图控件：坐标轴、网格、曲线、竖线光标、缩放 |
| **Layer** | **Layer** (layer.h) | 层：name, minScale, maxScale, curveKeys[], 可多层不同Y刻度 |
| **_layerMap** | CurvesWidget::m_layers | 层名→Layer |
| **_curvesBuffer** | CurvesWidget::m_buffer | 曲线数据缓冲区 |
| **_usableCurveButtons** | 图例空槽位 (QListWidget 未用行) | 可用曲线槽位 |
| **_usedCurveButtons** | m_selectedLineEdits + m_selectedCurveKeyOrder | curveKey→lineEdit，已选曲线列表 |
| **_contentTextBlocks** | m_legendList 每行文本 | 左侧「名称 当前值 单位」 |
| **_curveXSlider** | m_slider | 时间轴滑块，对应 CurrentPos |
| **curveKey** | (Address<<16)+Index，单机时=Modbus地址 | 曲线唯一标识 |
| **CurveItemIndexInfo** | **CurveOption** | Address/curveKey, DispName/displayName, Unit/unit, LineEdit |
| **AddCurveEntry** | onConfigClicked 内 addCurve + 写入 m_selected* | 缓冲区→层→图例绑定 |
| **RemoveCurveEntry** | removeCurveFromLegend + CurvesWidget::removeCurve | 从层、缓冲区、图例移除 |
| **FillCurveData** | 无DB时：AddSelectedCurve；有数据时：AddSample | 从数据库或实时 lineEdit 灌数 |
| **SetValTextBlocksContent** | refreshLegendAndStatus | 按当前光标位置刷新左侧数值 |
| **CurveSelectionDlg** | 曲线配置对话框 (QListWidget 多选) | 选择要显示的曲线 |
| **ToggleButton + 显隐** | 图例项 + 可选显隐 | 每条曲线可勾选显示/隐藏 |
| **SetCurveCustomColor** | 图例项右键→颜色 | 单条曲线改色 |
| **Zoom** | CurvesWidget 鼠标拖拽 _xScale/_yScale | 缩放 |
| **Y轴拖动** | Layer.yScrollOffset（可选） | 层Y轴平移 |

## 数据流

1. **可选曲线来源**：主窗口 `getCurveOptions()` 从 `m_addressToRuleMap` + `m_curveDisplayNames` 生成 CurveOption 列表（curveKey, displayName, unit, lineEdit）。
2. **选曲线**：用户在「曲线配置」里多选 → 对每条调用 AddCurveEntry 等价：CurveDataBuffer.addCurve → Layer.addCurve → 图例增加一行，m_selectedLineEdits[curveKey]=lineEdit。
3. **实时采样**：定时器从 m_selectedLineEdits 各 lineEdit 取文本转 double，调用 addSample(buffer)；滑块/CurrentPos 随最新点移动。
4. **显示**：refreshLegendAndStatus 按 CurrentPos 或 lineEdit 当前值更新图例「名称 值 单位」；CurvesWidget 用 buffer 和 m_curveColors 绘制每条曲线，Y 轴范围由 computeDataYRange 或 Layer 刻度决定。

## 文件清单

- `curvedatabuffer.h/cpp`：缓冲区（对应 CurvesBuffer）
- `layer.h`：层结构（对应 Layer）
- `curveswidget.h/cpp`：绘图（对应 CurvesPanelHostX）
- `curvesdialog.h/cpp`：主界面与图例（对应 CurvesControl）
- 主窗口 `ofile1.cpp`：getCurveOptions、m_curveDisplayNames、打开 CurvesDialog 并 setCurveOptions
