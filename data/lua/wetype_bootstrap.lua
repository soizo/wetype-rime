-- Load the native module through Lua's existing library loader. Candidate and
-- input handling are implemented using librime's official C++ component API.
local M = {}
function M.init(env)
  local path = env.engine.schema.config:get_string("wetype/module")
  assert(path and path ~= "", "WeType module is not installed")
  if path:sub(1, 1) ~= "/" then
    path = rime_api.get_user_data_dir() .. "/" .. path
  end
  local load, err = package.loadlib(path, "rime_wetype_load")
  assert(load, err)
  load()
end
function M.func() return 2 end
return M
