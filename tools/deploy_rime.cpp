// Deploy our schema and menu, or restore only the menu after disabling wetype.
#include <rime_api.h>
#include <cstdio>
#include <cstring>
#include <string>
int main(int argc,char **argv){
    if(argc<4){
        fprintf(stderr,"usage: wetype-deploy USER SHARED LOG [--default-only | SCHEMA ...]\n");
        return 2;
    }
    const bool default_only=argc==5&&!std::strcmp(argv[4],"--default-only");
    RimeApi *api=rime_get_api();
    RIME_STRUCT(RimeTraits,traits);
    traits.user_data_dir=argv[1];traits.shared_data_dir=argv[2];traits.log_dir=argv[3];
    traits.app_name="rime.wetype-deploy";traits.min_log_level=2;
    api->setup(&traits);api->deployer_initialize(&traits);
    bool ok=api->deploy_config_file("default.yaml","config_version");
    if(!default_only){
        for(int i=4;i<argc||i==4;++i){
            const char *name=argc==4?"wetype":argv[i];
            if(std::strchr(name,'/')||std::strncmp(name,"wetype",6)){ok=false;break;}
            std::string schema=std::string(argv[1])+"/"+name+".schema.yaml";
            if(!api->deploy_schema(schema.c_str()))ok=false;
        }
    }
    api->finalize();
    if(!ok){fprintf(stderr,"Rime deployment failed; inspect local deployment logs\n");return 1;}
    puts(default_only?"Updated scheme list only":"Deployed wetype schema and updated scheme list");
}
