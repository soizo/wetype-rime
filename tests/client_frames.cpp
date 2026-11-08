#include "../native/engine_client.h"
#include <cstdio>
int main(int argc,char **argv){
    if(argc!=2)return 2;
    try{
        wetype::Client client(argv[1]);
        for(unsigned flags=8;flags<=16;++flags){
            bool blocked=false;
            try{client.open("quanpin",flags);}catch(const std::runtime_error &){blocked=true;}
            if(!blocked)throw std::runtime_error("unsupported expression mask reached the worker");
        }
        auto session=client.open();
        bool rejected=false;
        try{client.query(session,"abc");}
        catch(const std::exception &e){rejected=std::string(e.what())=="invalid candidate coverage";}
        if(!rejected)throw std::runtime_error("malformed row was not rejected");
        auto result=client.query(session,"nihao");
        if(result.rows.size()!=1||result.rows[0].text!="你好"||result.rows[0].cover!=5||result.rows[0].preedit!="ni'hao")
            throw std::runtime_error("malformed frame corrupted the next response");
        client.command("RESET "+std::to_string(session));
        puts("PASS: unsupported expression masks rejected; malformed frame consumed; next query remains usable");
    }catch(const std::exception &e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
