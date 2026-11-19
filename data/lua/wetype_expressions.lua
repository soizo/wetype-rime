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

local function parse(bytes, rows, patterns, pat_index, errors)
  for line in bytes:gmatch("[^\n]+") do
    if not line:match("^%s*$") and not line:match("^%s*#") then
      local word, expression = line:match("^([^\t]+)\t([^\t]+)\r?$")
      if word and expression then
        if #word >= 2 and word:sub(1, 1) == "/" and word:sub(-1) == "/" then
          local pat = word:sub(2, -2)
          if pat ~= "" then
            if pcall(string.find, "x", pat) then
              local list = pat_index[pat]
              if not list then
                list = {}
                pat_index[pat] = list
                patterns[#patterns + 1] = { pat = pat, exprs = list }
              end
              append_unique(list, expression)
            else
              errors[#errors + 1] = "invalid Lua pattern: " .. word
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

local function refresh(env, ctx)
  local kind = state[env.kind]
  local supplied, err = read_file(env.supplied_path)
  if not supplied then
    kind.rows = kind.rows or {}
    kind.patterns = kind.patterns or {}
    if ctx:get_property("_wetype_expressions_error") ~= err then
      ctx:set_property("_wetype_expressions_error", err)
    end
    return kind.rows
  end
  local personal = read_file(env.personal_path) or ""
  if supplied ~= kind.supplied or personal ~= kind.personal then
    local rows, patterns, pat_index, errors = {}, {}, {}, {}
    parse(supplied, rows, patterns, pat_index, errors)
    parse(personal, rows, patterns, pat_index, errors)
    kind.supplied, kind.personal = supplied, personal
    kind.rows, kind.patterns = rows, patterns
    kind.pattern_error = errors[1]
  end
  local message = kind.pattern_error or ""
  if ctx:get_property("_wetype_expressions_error") ~= message then
    ctx:set_property("_wetype_expressions_error", message)
  end
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
