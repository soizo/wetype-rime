-- Rime-side display preferences. The original candidate still owns selection.
local file_name = "/wetype-candidate-rules.custom.yaml"
local file_limit = 256 * 1024
local state = { rules = {} }
local filter, processor = {}, {}
local actions = { "pin", "block", "unpin", "restore" }
local fields = { layout = true, code = true, pin = true, block = true, unpin = true, restore = true }

local function read_file(path)
  local file, err, errno = io.open(path, "rb")
  if not file then
    if errno == 2 then return "" end
    return nil, err
  end
  local bytes, read_error = file:read(file_limit + 1)
  file:close()
  if read_error then return nil, read_error end
  return bytes or ""
end

local function parse(path, bytes)
  assert(#bytes <= file_limit, "candidate rules exceed 256 KiB")
  local has_content = false
  for line in bytes:gmatch("[^\n]+") do
    if not line:match("^%s*$") and not line:match("^%s*#") then
      assert(not line:match("^%-%-%-") and not line:match("^%.%.%."), "use one YAML document without document markers")
      has_content = true
    end
  end
  if not has_content then return {} end

  local config = Config()
  assert(config:load_from_file(path), "invalid candidate-rules YAML")
  local entries = config:get_list("")
  assert(entries, "expected a list of candidate rules")
  local rules = {}
  for i = 0, entries.size - 1 do
    local prefix = "@" .. i .. "/"
    local row = config:get_map("@" .. i)
    local layout = config:get_string(prefix .. "layout")
    local code = config:get_string(prefix .. "code")
    assert(row and layout and layout:match("^[a-z0-9_]+$") and code and code ~= "", "rule requires a layout and code")
    for _, field in ipairs(row:keys()) do
      assert(fields[field], "unknown candidate-rule field: " .. field)
    end
    local by_code = rules[layout] or {}
    rules[layout] = by_code
    local group = by_code[code] or { pins = {}, blocked = {} }
    by_code[code] = group
    local function words(action, apply)
      if not row:has_key(action) then return end
      local list = config:get_list(prefix .. action)
      assert(list, action .. " must be a list")
      for j = 0, list.size - 1 do
        local value = list:get_value_at(j)
        assert(value and value.value ~= "", action .. " requires non-empty words")
        apply(value.value)
      end
    end
    words("pin", function(word)
      for _, existing in ipairs(group.pins) do
        if existing == word then return end
      end
      group.pins[#group.pins + 1] = word
    end)
    words("unpin", function(word)
      for j, existing in ipairs(group.pins) do
        if existing == word then table.remove(group.pins, j); return end
      end
    end)
    if row:has_key("restore") then
      local restore = config:get_bool(prefix .. "restore")
      assert(restore ~= nil, "restore must be a boolean")
      if restore then group.blocked = {} end
    end
    words("block", function(word) group.blocked[word] = true end)
  end
  for _, by_code in pairs(rules) do
    for _, group in pairs(by_code) do
      group.rank = {}
      for index, word in ipairs(group.pins) do group.rank[word] = index end
    end
  end
  return rules
end

local function report(ctx, message)
  message = message or ""
  if ctx:get_property("_wetype_rules_error") ~= message then
    ctx:set_property("_wetype_rules_error", message)
  end
end

local function refresh(env, ctx)
  local bytes, err = read_file(env.rules_path)
  if not bytes then report(ctx, err); return end
  if bytes ~= state.bytes then
    local ok, rules = pcall(parse, env.rules_path, bytes)
    if read_file(env.rules_path) ~= bytes then
      report(ctx, "candidate rules changed during read")
      return
    end
    state.bytes = bytes
    if ok then
      state.rules, state.error = rules, nil
    else
      state.error = rules
    end
  end
  report(ctx, state.error)
  return bytes
end

local function candidate_key(candidate, input)
  local original = candidate and candidate:get_genuine()
  if original and (original.type == "wetype" or original.type == "wetype_fuzzy") then
    return input:sub(original.start + 1, original._end), original.text
  end
end

local function init(env)
  env.rules_path = rime_api.get_user_data_dir() .. file_name
end

filter.init = init
function filter.func(translation, env)
  local ctx = env.engine.context
  refresh(env, ctx)
  local groups = state.rules[ctx:get_property("_wetype_mode")]
  local input = ctx.input
  local has_pins = false
  if groups then
    for code, group in pairs(groups) do
      if #group.pins > 0 and input:find(code, 1, true) then has_pins = true; break end
    end
  end

  local output, buckets = {}, {}
  for candidate in translation:iter() do
    local code, word
    if groups then code, word = candidate_key(candidate, input) end
    local group = groups and groups[code]
    if not group or not group.blocked[word] then
      if not has_pins then
        yield(candidate)
      else
        output[#output + 1] = candidate
        if group and #group.pins > 0 then
          local bucket = buckets[code] or { slots = {}, pinned = {}, ordinary = {} }
          buckets[code] = bucket
          bucket.slots[#bucket.slots + 1] = #output
          local rank = group.rank[word]
          if rank then
            local same_rank = bucket.pinned[rank] or {}
            bucket.pinned[rank] = same_rank
            local comment = candidate.comment
            same_rank[#same_rank + 1] = ShadowCandidate(candidate, candidate.type, "",
              comment == "" and "置顶" or "置顶 · " .. comment)
          else
            bucket.ordinary[#bucket.ordinary + 1] = candidate
          end
        end
      end
    end
  end
  for code, bucket in pairs(buckets) do
    local i = 1
    for rank = 1, #groups[code].pins do
      for _, candidate in ipairs(bucket.pinned[rank] or {}) do
        output[bucket.slots[i]] = candidate
        i = i + 1
      end
    end
    for _, candidate in ipairs(bucket.ordinary) do
      output[bucket.slots[i]] = candidate
      i = i + 1
    end
  end
  for _, candidate in ipairs(output) do yield(candidate) end
end

local function quote(text)
  return '"' .. text:gsub('[%z\1-\31\127\\"]', function(char)
    if char == '\\' or char == '"' then return '\\' .. char end
    return string.format('\\x%02X', char:byte())
  end) .. '"'
end

local function append_rule(env, ctx, layout, code, action, word)
  local bytes = refresh(env, ctx)
  if not bytes or state.error or state.bytes ~= bytes then return false end
  local header
  for line in bytes:gmatch("[^\r\n]+") do
    if not line:match("^%s*$") and not line:match("^%s*#") then header = line; break end
  end
  local indent = header and header:match("^( *)%-[ \t]") or ""
  if header and not header:match("^ *%-[ \t]") then
    report(ctx, "shortcut writes require a block-style rules list")
    return false
  end
  local ending = bytes:find("\r\n", 1, true) and "\r\n" or "\n"
  local value = action == "restore" and "true" or ("[" .. quote(word) .. "]")
  local entry = indent .. "- {layout: " .. quote(layout) .. ", code: " .. quote(code)
    .. ", " .. action .. ": " .. value .. "}" .. ending
  local addition = (bytes == "" or bytes:sub(-1) == "\n") and "" or ending
  addition = addition .. entry
  if #bytes + #addition > file_limit then report(ctx, "candidate rules exceed 256 KiB"); return false end
  if read_file(env.rules_path) ~= bytes then
    report(ctx, "candidate rules changed before shortcut write; retry")
    return false
  end
  local file, err = io.open(env.rules_path, "ab")
  if not file then report(ctx, err); return false end
  -- One append retains the user's formatting and avoids replacing concurrent edits.
  local written, write_error = file:setvbuf("no")
  if written then written, write_error = file:write(addition) end
  local closed, close_error = file:close()
  if not written or not closed then report(ctx, write_error or close_error); return false end
  local saved = refresh(env, ctx)
  if state.error then return false end
  if not saved or not saved:find(entry, #bytes + 1, true) then
    report(ctx, "candidate rules changed during shortcut write; retry")
    return false
  end
  return true
end

local function shortcut_key(key)
  local repr = key:repr()
  -- Squirrel sends uppercase keysyms for shifted letters.
  if key:shift() then return (repr:gsub("%+[A-Z]$", string.lower)) end
  return repr
end

function processor.init(env)
  init(env)
  env.shortcuts = {}
  local config = env.engine.schema.config
  for _, action in ipairs(actions) do
    local binding = config:get_string("wetype/candidate_keys/" .. action)
    if binding and binding ~= "" then env.shortcuts[shortcut_key(KeyEvent(binding))] = action end
  end
end

function processor.func(key, env)
  if next(env.shortcuts) == nil or key:release() then return 2 end
  local action = env.shortcuts[shortcut_key(key)]
  if not action then return 2 end
  local ctx = env.engine.context
  if ctx.input == "" or ctx:get_option("ascii_mode") then return 2 end
  local layout = ctx:get_property("_wetype_mode")
  if layout == "" then return 2 end
  local code, word
  if action == "restore" then
    code = ctx.input
  else
    code, word = candidate_key(ctx:get_selected_candidate(), ctx.input)
    if not code or code == "" then return 2 end
  end
  if append_rule(env, ctx, layout, code, action, word) then
    ctx:refresh_non_confirmed_composition()
  end
  return 1
end

return { filter = filter, processor = processor }
