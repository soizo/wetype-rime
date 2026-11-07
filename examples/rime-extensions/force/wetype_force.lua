-- 在此表中添加“完整输入 -> 固定输出”。只处理完整组合，不做模糊匹配。
local replacements = {
  myname = "自定义姓名",
}

return function(input, env)
  local code = env.engine.context.input
  local text = replacements[code]
  if text then
    yield(Candidate("wetype_forced", 0, #code, text, ""))
    return
  end
  for candidate in input:iter() do
    yield(candidate)
  end
end
