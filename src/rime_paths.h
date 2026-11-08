#pragma once
#include <rime/service.h>
namespace wetype {
// Relative extension paths use the frontend's user directory, never its cwd.
inline string user_path(const string &value){
    if(value.empty())return {};
    rime::path path(value);
    return (path.is_absolute()?path:rime::Service::instance().deployer().user_data_dir/path).u8string();
}
}
