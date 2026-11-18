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

local function parse(bytes, rows)
  for line in bytes:gmatch("[^\n]+") do
    if not line:match("^%s*$") and not line:match("^%s*#") then
      local word, expression = line:match("^([^\t]+)\t([^\t]+)\r?$")
      if word and expression then
        local list = rows[word]
        if not list then
          list = {}
          rows[word] = list
        end
        local seen = false
        for _, existing in ipairs(list) do
          if existing == expression then seen = true; break end
        end
        if not seen then list[#list + 1] = expression end
      end
    end
  end
end

local function refresh(env, ctx)
  local kind = state[env.kind]
  local supplied, err = read_file(env.supplied_path)
  if not supplied then
    kind.rows = kind.rows or {}
    if ctx:get_property("_wetype_expressions_error") ~= err then
      ctx:set_property("_wetype_expressions_error", err)
    end
    return kind.rows
  end
  local personal = read_file(env.personal_path) or ""
  if supplied ~= kind.supplied or personal ~= kind.personal then
    local rows = {}
    parse(supplied, rows)
    parse(personal, rows)
    kind.supplied, kind.personal, kind.rows = supplied, personal, rows
  end
  if ctx:get_property("_wetype_expressions_error") ~= "" then
    ctx:set_property("_wetype_expressions_error", "")
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
    for candidate in translation:iter() do
      yield(candidate)
      local expressions = rows[candidate.text]
      if expressions then
        for _, expression in ipairs(expressions) do
          yield(ShadowCandidate(candidate, env.candidate_type, expression,
                                env.comment_word and candidate.text or ""))
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
