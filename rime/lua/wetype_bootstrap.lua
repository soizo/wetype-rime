-- Load the native module through Lua's existing library loader. Candidate and
-- input handling are implemented using librime's official C++ component API.
local M = {}
function M.init(env)
  local path = env.engine.schema.config:get_string("wetype/module")
  local load, err = package.loadlib(path, "rime_wetype_load")
  assert(load, err)
  load()
end
function M.func() return 2 end
return M
