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
  -- A UTF-8 length prefix is 194-244; the continuation bytes are 128-191.
  local function utf8_len(b) return b >= 240 and 4 or (b >= 224 and 3 or 2) end
  -- True when s repeats safely as a native unit (single byte, [class], %x, .):
  -- appending * + - ? to it stays exact. Multibyte chars and multi-char
  -- sequences are rejected because Lua quantifiers bind to one byte.
  local function is_byte_unit(s)
    if s:sub(1, 1) == "[" and s:sub(-1) == "]" then return true end
    if #s == 1 then return true end
    if #s == 2 and s:sub(1, 1) == "%" then return true end
    return false
  end
  -- Lua character classes are byte-based: [蛋卵] would match any single byte
  -- of those characters, so 说 (E8 AF B4) matches 蛋 (E8 9B 8B) via E8. A
  -- class holding multibyte characters must expand to per-character
  -- alternation; pure-byte classes stay native.
  local function class_chars(s)
    local inner = s:sub(2, -2)
    local negated = inner:sub(1, 1) == "^"
    if negated then inner = inner:sub(2) end
    local items, i, multi = {}, 1, false
    if inner:sub(1, 1) == "]" then
      items[#items + 1] = "[]]"
      inner = inner:sub(2)
    end
    -- Read one class member at `at`: returns the member's pattern, whether it
    -- is a multibyte character, whether it was %-escaped, and the next index.
    local function member(at)
      local c = inner:sub(at, at)
      if c == "%" then
        local nx = inner:sub(at + 1, at + 1)
        if nx == "" then return nil, "trailing % in class" end
        local nb = nx:byte()
        if nb >= 194 and nb <= 244 then
          local want = utf8_len(nb)
          local j = at + 2
          while j <= #inner and j - (at + 1) < want do
            local cb = inner:byte(j)
            if cb >= 128 and cb <= 191 then j = j + 1 else break end
          end
          if j - (at + 1) < want then return nil, "truncated UTF-8 in class" end
          -- % escapes only matter for magic bytes; a multibyte character is
          -- already literal, so drop the escape and expand per character.
          return inner:sub(at + 1, j - 1), true, true, j
        end
        return "%" .. nx, false, true, at + 2
      end
      local b = inner:byte(at)
      if b >= 194 and b <= 244 then
        local want = utf8_len(b)
        local j = at + 1
        while j <= #inner and j - at < want do
          local cb = inner:byte(j)
          if cb >= 128 and cb <= 191 then j = j + 1 else break end
        end
        if j - at < want then return nil, "truncated UTF-8 in class" end
        return inner:sub(at, j - 1), true, false, j
      end
      -- Wrap single-byte members as one-char classes so a later ? suffix binds
      -- to the whole member and class metacharacters stay literal.
      local ch = c
      if ch:find("[^%w]") then ch = "%" .. ch end
      return "[" .. ch .. "]", false, false, at + 1
    end
    while i <= #inner do
      local item, extra, escaped, nxt = member(i)
      if not item then return nil, extra end
      -- A dash between two members is a range. The upper bound cannot be
      -- multibyte: [蛋-卵] has no byte-wise equivalent.
      if inner:sub(nxt, nxt) == "-" and inner:sub(nxt + 1, nxt + 1) ~= "" then
        if extra then return nil, "multibyte range in class" end
        if escaped then return nil, "escaped range in class" end
        local hi = inner:byte(nxt + 1)
        if hi >= 128 then return nil, "multibyte range in class" end
        if inner:byte(i) > hi then return nil, "reversed range in class" end
        items[#items + 1] = "[" .. inner:sub(i, i) .. "-" .. inner:sub(nxt + 1, nxt + 1) .. "]"
        i = nxt + 2
      else
        if extra then multi = true end
        items[#items + 1] = item
        i = nxt
      end
    end
    return items, negated, multi
  end
  local function parse_expr()
    local branches = {}
    while true do
      local seq = {}
      while pos <= #src and peek() ~= "|" and peek() ~= ")" do
        local atom = parse_atom()
        local c = peek()
        if c == "*" or c == "+" or c == "-" or c == "?" then
          pos = pos + 1
          if atom.kind == "class" then
            if c ~= "?" then
              return nil, "unbounded repetition of a multibyte class; use {n,m}"
            end
            seq[#seq + 1] = { kind = "range", atom = atom, n = 0, m = 1 }
          elseif atom.kind == "lit" and is_multibyte(atom.s) then
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
      if s:find("[\194-\244]") then
        local items, negated, multi = class_chars(s)
        if not items then error(negated) end
        if negated and multi then
          error("negated multibyte class cannot expand exactly; list characters positively")
        end
        if multi then return { kind = "class", items = items } end
        -- No multibyte member (e.g. a class of lone continuation bytes): the
        -- byte-based semantics are exactly what was written, keep it native.
      end
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
    elseif node.kind == "class" then
      -- Each member becomes its own alternative so matching is per character,
      -- never per byte. Quantifiers on the class multiply via the cartesian
      -- product in seq, bounded by EXPAND_CAP.
      local out = {}
      for _, item in ipairs(node.items) do
        out[#out + 1] = item
        if #out > EXPAND_CAP then return nil, "pattern expands too large" end
      end
      return out
    elseif node.kind == "rep" then
      local base, berr = expand(node.atom)
      if not base then return nil, berr or "bad node" end
      local out = {}
      if node.q == "?" then
        local exact_native = true
        for _, s in ipairs(base) do
          if not is_byte_unit(s) then exact_native = false end
        end
        if exact_native then
          for _, s in ipairs(base) do out[#out + 1] = s .. "?" end
        else
          -- Native ? binds to the last byte, so (大利)? would mean 大 + 利?.
          -- Expand to the empty string plus each whole alternative instead.
          out[1] = ""
          for _, s in ipairs(base) do out[#out + 1] = s end
        end
      else
        for _, s in ipairs(base) do
          if not is_byte_unit(s) then
            return nil, "unbounded repetition needs a single byte, class or .; use {n,m}"
          end
          out[#out + 1] = s .. node.q
        end
      end
      return out
    elseif node.kind == "range" then
      local base, berr = expand(node.atom)
      if not base then return nil, berr or "bad node" end
      local out = {}
      -- A single unit that repeats natively (single byte, class, %x, .) can be
      -- written compactly, e.g. [0-9]{2,3} -> [0-9][0-9][0-9]?.
      if #base == 1 and is_byte_unit(base[1]) then
        local unit = base[1]
        out[1] = unit:rep(node.n) .. string.rep(unit .. "?", node.m - node.n)
        return out
      end
      -- Otherwise every count and every member combination is its own
      -- alternative: [蛋卵]{2} -> 蛋蛋 | 蛋卵 | 卵蛋 | 卵卵. Each member is a
      -- whole character, so matching never falls back to byte semantics.
      local acc = { "" }
      if node.n == 0 then out[1] = "" end
      for k = 1, node.m do
        local next_acc = {}
        for _, a in ipairs(acc) do
          for _, b in ipairs(base) do
            next_acc[#next_acc + 1] = a .. b
            if #next_acc > EXPAND_CAP then return nil, "pattern expands too large" end
          end
        end
        acc = next_acc
        if k >= node.n then
          for _, s in ipairs(acc) do
            out[#out + 1] = s
            if #out > EXPAND_CAP then return nil, "pattern expands too large" end
          end
        end
      end
      return out
    elseif node.kind == "seq" then
      local acc = { "" }
      for _, item in ipairs(node.items) do
        local part, perr = expand(item)
        if not part then return nil, perr or "pattern expands too large" end
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
        local part, perr = expand(branch)
        if not part then return nil, perr or "pattern expands too large" end
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
