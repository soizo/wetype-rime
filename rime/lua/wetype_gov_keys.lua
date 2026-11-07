-- 与个人“小鹤双拼大陆”一致：区分右 Shift 标点，Shift+空格输入全角空格。
return function(key, env)
  if key.keycode == 65506 then
    env.right_shift_down = not key:release() and key:shift()
    return 2
  end
  if not key:shift() then env.right_shift_down = false end
  if key:release() or key:ctrl() or key:alt() or key:super() then return 2 end
  local context = env.engine.context
  if context:get_option("ascii_mode") then return 2 end
  if key.keycode == 32 and key:shift() then
    if context:is_composing() then context:commit() end
    env.engine:commit_text("　")
    return 1
  end
  if not env.right_shift_down then return 2 end
  local text = ({
    [91] = "『", [123] = "『", [93] = "』", [125] = "』",
    [92] = "\\", [124] = "\\", [59] = ";", [58] = ";",
    [45] = "_", [95] = "_", [54] = "^", [94] = "^",
    [57] = "（", [40] = "（", [48] = "）", [41] = "）",
    [51] = "※", [35] = "※", [56] = "☆", [42] = "☆",
    [53] = "○", [37] = "○",
  })[key.keycode]
  if not text then return 2 end
  if context:is_composing() then context:commit() end
  env.engine:commit_text(text)
  return 1
end
