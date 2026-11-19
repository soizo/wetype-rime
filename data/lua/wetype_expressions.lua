-- External, editable expression tables: Unicode Emoji, kaomoji and symbols.
-- These replace the engine's built-in recommendations with plain TSV data in
-- the Rime user directory, matched against the simplified candidate text
-- before any OpenCC conversion runs.
--
-- Data files (word<TAB>expression, one per line, '#' comments):
--   wetype_emoji.tsv / wetype_kaomoji.tsv / wetype_symbols.tsv    supplied
--   wetype_*.custom.tsv                                           personal
-- Personal rows are merged over the supplied tables; edits apply after the
-- next deploy (files are re-read when their bytes change).
--
-- Matching is hybrid: a plain key is an exact literal match (hash lookup).
-- A key wrapped in /.../ is a Lua pattern matched with string.find, e.g.
--   /.*心$	❤️   matches any candidate ending in 心
--   /开心/	😄   matches any candidate containing 开心
-- Keep pattern keys few; they are scanned per candidate, literal keys are not.
--
-- Each filter is gated by its Rime option (emoji / kaomoji / symbol_emoji)
-- and steps aside automatically while the matching native engine category is
-- active (_wetype_expression_flags), so built-in and external recommendations
-- never duplicate each other.
local file_limit = 4 * 1024 * 1024
local state = {}

local function read_file(path)
  local file, err, errno = io.open(path, "rb")
  if not file then
    if errno == 2 then return "" end
    return nil, err
  end
  local bytes, read_error = file:read(file_limit + 1)
  file:close()
  if read_error then return nil, read_error end
  if bytes and #bytes > file_limit then return nil, path .. " exceeds 4 MiB" end
  return bytes or ""
end

local function append_unique(list, value)
  for _, existing in ipairs(list) do
    if existing == value then return end
  end
  list[#list + 1] = value
end

-- Extended pattern syntax compiled to native Lua patterns at load time, so
-- matching stays a fast string.find. Adds | (alternation) and {n,m} (bounded
-- repetition) on top of Lua patterns; no lookahead. Alternation and repetition
-- are expanded into separate native patterns (cartesian product), capped to
-- keep the per-candidate scan small. Escape literal | { } as %| %{ %}.
local EXPAND_CAP = 256

local function compile_extended(src)
  local pos = 1
  local function peek() return src:sub(pos, pos) end
  -- True when s is exactly one multibyte UTF-8 character (not %x, not [...]).
  local function is_multibyte(s)
    if #s <= 1 or s:sub(1, 1) == "%" or s:sub(1, 1) == "[" then return false end
    local tail = s:match("([\194-\244][\128-\191]*)$")
    return tail ~= nil and #tail == #s
  end
  local parse_atom
  local function parse_expr()
    local branches = {}
    while true do
      local seq = {}
      while pos <= #src and peek() ~= "|" and peek() ~= ")" do
        local atom = parse_atom()
        local c = peek()
        if c == "*" or c == "+" or c == "-" or c == "?" then
          pos = pos + 1
          if atom.kind == "lit" and is_multibyte(atom.s) then
            -- Native */+/-/? repeat a single byte only, which would corrupt a
            -- multibyte character. ? is bounded: expand as {0,1}. Unbounded
            -- repetition of a multibyte sequence cannot be expressed as a
            -- native Lua pattern, so reject it and point at {n,m}.
            if c == "?" then
              seq[#seq + 1] = { kind = "range", atom = atom, n = 0, m = 1 }
            else
              return nil, "unbounded repetition of a multibyte character; use {n,m}"
            end
          else
            seq[#seq + 1] = { kind = "rep", atom = atom, q = c }
          end
        elseif c == "{" then
          local close = src:find("}", pos + 1, true)
          if not close then return nil, "unclosed {n,m}" end
          local body = src:sub(pos + 1, close - 1)
          local a, b = body:match("^(%d+)%s*,%s*(%d+)$")
          local n, m
          if a then
            n, m = tonumber(a), tonumber(b)
          elseif body:match("^%d+$") then
            n = tonumber(body); m = n
          else
            return nil, "bad {n,m}: " .. body
          end
          if n > m or m > 64 then return nil, "bad {n,m} bounds" end
          if atom.kind == "anchor" or atom.kind == "group" then
            return nil, "{n,m} applies to a single character, class or . only"
          end
          pos = close + 1
          seq[#seq + 1] = { kind = "range", atom = atom, n = n, m = m }
        else
          seq[#seq + 1] = atom
        end
      end
      branches[#branches + 1] = { kind = "seq", items = seq }
      if peek() == "|" then pos = pos + 1 else break end
    end
    if #branches == 1 then return branches[1] end
    return { kind = "alt", branches = branches }
  end
  parse_atom = function()
    local c = peek()
    if c == "%" then
      local nx = src:sub(pos + 1, pos + 1)
      if nx == "" then error("trailing %") end
      pos = pos + 2
      return { kind = "lit", s = "%" .. nx }
    elseif c == "[" then
      local j = pos + 1
      if src:sub(j, j) == "^" then j = j + 1 end
      if src:sub(j, j) == "]" then j = j + 1 end
      while j <= #src and src:sub(j, j) ~= "]" do
        if src:sub(j, j) == "%" then j = j + 1 end
        j = j + 1
      end
      if j > #src then error("unclosed [") end
      local s = src:sub(pos, j)
      pos = j + 1
      return { kind = "lit", s = s }
    elseif c == "(" then
      pos = pos + 1
      local inner = parse_expr()
      if peek() ~= ")" then error("unclosed (") end
      pos = pos + 1
      return { kind = "group", node = inner }
    elseif c == "^" or c == "$" then
      pos = pos + 1
      return { kind = "anchor", s = c }
    elseif c == "." then
      pos = pos + 1
      return { kind = "lit", s = "." }
    elseif c == "" or c == "|" or c == ")" or c == "{" or c == "}" then
      error("unexpected " .. c)
    else
      -- Consume a whole UTF-8 character as one atom so quantifiers (and
      -- {n,m}) apply to the character, not to its individual bytes.
      local j = pos + 1
      local b = c:byte()
      if b >= 194 and b <= 244 then
        while j <= #src do
          local nb = src:byte(j)
          if nb >= 128 and nb <= 191 then j = j + 1 else break end
        end
      end
      local s = src:sub(pos, j - 1)
      pos = j
      return { kind = "lit", s = s }
    end
  end
  local function expand(node)
    if node.kind == "lit" or node.kind == "anchor" then
      return { node.s }
    elseif node.kind == "group" then
      return expand(node.node)
    elseif node.kind == "rep" then
      local out = {}
      for _, s in ipairs(expand(node.atom)) do out[#out + 1] = s .. node.q end
      return out
    elseif node.kind == "range" then
      local base = expand(node.atom)
      local out = {}
      for _, s in ipairs(base) do
        -- Lua's ? quantifier binds to one byte only, so a multibyte UTF-8
        -- literal cannot take s? per optional copy. Expand n mandatory copies
        -- plus one alternative per optional count instead (still exact).
        local unit, opt = s, s .. "?"
        if #s > 1 and s:sub(1, 1) ~= "%" and s:sub(1, 1) ~= "[" then
          local tail = s:match("([\194-\244][\128-\191]*)$")
          if tail and #tail == #s then
            opt = nil
          end
        end
        if opt then
          out[#out + 1] = unit:rep(node.n) .. opt:rep(node.m - node.n)
        else
          for k = node.n, node.m do
            out[#out + 1] = unit:rep(k)
            if #out > EXPAND_CAP then return nil, "pattern expands too large" end
          end
        end
      end
      return out
    elseif node.kind == "seq" then
      local acc = { "" }
      for _, item in ipairs(node.items) do
        local part = expand(item)
        if not part then return nil, "pattern expands too large" end
        local next_acc = {}
        for _, a in ipairs(acc) do
          for _, b in ipairs(part) do
            next_acc[#next_acc + 1] = a .. b
            if #next_acc > EXPAND_CAP then return nil, "pattern expands too large" end
          end
        end
        acc = next_acc
      end
      return acc
    elseif node.kind == "alt" then
      local out = {}
      for _, branch in ipairs(node.branches) do
        local part = expand(branch)
        if not part then return nil, "pattern expands too large" end
        for _, s in ipairs(part) do
          out[#out + 1] = s
          if #out > EXPAND_CAP then return nil, "pattern expands too large" end
        end
      end
      return out
    end
    return nil, "bad node"
  end
  local ok, node, perr = pcall(parse_expr)
  if not ok then return nil, tostring(node) end
  if not node then return nil, perr end
  if pos <= #src then return nil, "unexpected " .. src:sub(pos, pos) end
  if src:sub(-1) == "|" then return nil, "empty alternation branch" end
  local pats, err = expand(node)
  if not pats then return nil, err end
  for _, p in ipairs(pats) do
    if not pcall(string.find, "x", p) then return nil, "invalid generated pattern" end
  end
  return pats
end

local function parse(bytes, rows, patterns, pat_index, errors)
  for line in bytes:gmatch("[^\n]+") do
    if not line:match("^%s*$") and not line:match("^%s*#") then
      local word, expression = line:match("^([^\t]+)\t([^\t]+)\r?$")
      if word and expression then
        if #word >= 2 and word:sub(1, 1) == "/" and word:sub(-1) == "/" then
          local pat = word:sub(2, -2)
          if pat ~= "" then
            local pats, perr = compile_extended(pat)
            if pats then
              local list = pat_index[pat]
              if not list then
                list = {}
                pat_index[pat] = list
                for _, p in ipairs(pats) do
                  patterns[#patterns + 1] = { pat = p, exprs = list }
                end
              end
              append_unique(list, expression)
            else
              errors[#errors + 1] = word .. ": " .. perr
            end
          end
        else
          local list = rows[word]
          if not list then
            list = {}
            rows[word] = list
          end
          append_unique(list, expression)
        end
      end
    end
  end
end

-- All three filters share one error property, so publish the aggregate of
-- every kind's error; a per-kind write would let an error-free filter that
-- runs later overwrite another kind's reported error with "".
local KIND_ORDER = { "emoji", "kaomoji", "symbols" }

local function publish_error(ctx)
  local parts = {}
  for _, name in ipairs(KIND_ORDER) do
    local kind = state[name]
    local err = kind and (kind.read_error or kind.pattern_error)
    if err then parts[#parts + 1] = err end
  end
  local message = table.concat(parts, "; ")
  if ctx:get_property("_wetype_expressions_error") ~= message then
    ctx:set_property("_wetype_expressions_error", message)
  end
end

local function refresh(env, ctx)
  local kind = state[env.kind]
  local supplied, err = read_file(env.supplied_path)
  if not supplied then
    kind.rows = kind.rows or {}
    kind.patterns = kind.patterns or {}
    kind.read_error = err
    publish_error(ctx)
    return kind.rows
  end
  kind.read_error = nil
  local personal = read_file(env.personal_path) or ""
  if supplied ~= kind.supplied or personal ~= kind.personal then
    local rows, patterns, pat_index, errors = {}, {}, {}, {}
    parse(supplied, rows, patterns, pat_index, errors)
    parse(personal, rows, patterns, pat_index, errors)
    kind.supplied, kind.personal = supplied, personal
    kind.rows, kind.patterns = rows, patterns
    kind.pattern_error = errors[1]
  end
  publish_error(ctx)
  return kind.rows
end

local function native_active(ctx, bit)
  -- The adapter publishes the active native mask: 1 emoji, 2 kaomoji, 4 symbols.
  local flags = tonumber(ctx:get_property("_wetype_expression_flags")) or 0
  return math.floor(flags / bit) % 2 == 1
end

local function make(kind, file, option, native_bit, candidate_type, comment_word)
  local filter = {}
  function filter.init(env)
    env.kind = kind
    env.option = option
    env.native_bit = native_bit
    env.candidate_type = candidate_type
    env.comment_word = comment_word
    local directory = rime_api.get_user_data_dir()
    env.supplied_path = directory .. "/" .. file .. ".tsv"
    env.personal_path = directory .. "/" .. file .. ".custom.tsv"
    state[kind] = state[kind] or {}
    refresh(env, env.engine.context)
  end
  function filter.func(translation, env)
    local ctx = env.engine.context
    if not ctx:get_option(env.option) or native_active(ctx, env.native_bit) then
      for candidate in translation:iter() do yield(candidate) end
      return
    end
    local rows = refresh(env, ctx)
    local patterns = state[env.kind].patterns
    local has_patterns = #patterns > 0
    for candidate in translation:iter() do
      yield(candidate)
      local text = candidate.text
      local list = rows[text]
      if list then
        for _, expression in ipairs(list) do
          yield(ShadowCandidate(candidate, env.candidate_type, expression,
                                env.comment_word and text or ""))
        end
      end
      if has_patterns then
        local seen
        if list then
          seen = {}
          for _, expression in ipairs(list) do seen[expression] = true end
        end
        for _, p in ipairs(patterns) do
          if string.find(text, p.pat) then
            if not seen then seen = {} end
            for _, expression in ipairs(p.exprs) do
              if not seen[expression] then
                seen[expression] = true
                yield(ShadowCandidate(candidate, env.candidate_type, expression,
                                      env.comment_word and text or ""))
              end
            end
          end
        end
      end
    end
  end
  return filter
end

return {
  emoji = make("emoji", "wetype_emoji", "emoji", 1, "lua_emoji", false),
  kaomoji = make("kaomoji", "wetype_kaomoji", "kaomoji", 2, "lua_kaomoji", true),
  symbols = make("symbols", "wetype_symbols", "symbol_emoji", 4, "lua_symbols", false),
}
