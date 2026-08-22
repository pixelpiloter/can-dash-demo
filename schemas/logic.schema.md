# logic.yaml 结构约定 (轻量 schema 文档)
#
# 每条 logics[] 必填:
#   id: string           规则唯一 id
#   expr: string         ExprTk 表达式 (可用 setter / can 信号名)
#   can_signals: [str]   本规则关注的信号 (可多个); 每项必须在 can_ids.yaml fields 声明
#
# 可选:
#   desc: string
#
# ConfigLoader 启动期校验:
#   can_signals             → 必须是 scalar sequence，每项必须属于 can_ids.yaml fields
#   setwarnon/off("id")     → id 必须在 warn.yaml warns[].name
#   setlighton/shine/off("id") → id 必须在 lights.yaml lights[].name
#   不静态分析 expr 中 setdisplayvalue / isovertime 参数或任意变量
#
# 多信号示例:
#   can_signals: ["bat_volt", "bat_curr"]
#   expr 内直接写 bat_volt * bat_curr
