/*
    Smithproxy- transparent proxy with SSL inspection capabilities.
    Copyright (c) 2014, Ales Stibal <astib@mag0.net>, All rights reserved.

    Smithproxy is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Smithproxy is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Smithproxy.  If not, see <http://www.gnu.org/licenses/>.

    Linking Smithproxy statically or dynamically with other modules is
    making a combined work based on Smithproxy. Thus, the terms and
    conditions of the GNU General Public License cover the whole combination.

    In addition, as a special exception, the copyright holders of Smithproxy
    give you permission to combine Smithproxy with free software programs
    or libraries that are released under the GNU LGPL and with code
    included in the standard release of OpenSSL under the OpenSSL's license
    (or modified versions of such code, with unchanged license).
    You may copy and distribute such a system following the terms
    of the GNU GPL for Smithproxy and the licenses of the other code
    concerned, provided that you include the source code of that other code
    when and as the GNU GPL requires distribution of source code.

    Note that people who make modified versions of Smithproxy are not
    obligated to grant this special exception for their modified versions;
    it is their choice whether to do so. The GNU General Public License
    gives permission to release a modified version without this exception;
    this exception also makes it possible to release a modified version
    which carries forward this exception.
*/

#include <vector>

#include <cfgapi.hpp>
#include <logger.hpp>
#include <policy.hpp>
#include <mitmproxy.hpp>
#include <mitmhost.hpp>
#include <cmdserver.hpp>

#include <socle.hpp>
#include <smithproxy.hpp>

#include <shmtable.hpp>


CfgFactory::CfgFactory(): args_debug_flag(NON), syslog_level(INF)  {

    listen_tcp_port = "50080";
    listen_tls_port = "50443";
    listen_dtls_port = "50443";
    listen_udp_port = "50080";
    listen_socks_port = "1080";

    listen_tcp_port_base = "50080";
    listen_tls_port_base = "50443";
    listen_dtls_port_base = "50443";
    listen_udp_port_base = "50080";
    listen_socks_port_base = "1080";

    config_file_check_only = false;

    dir_msg_templates = "/etc/smithproxy/msg/en/";


    num_workers_tcp = 0;
    num_workers_tls = 0;
    num_workers_dtls = 0;
    num_workers_udp = 0;
    num_workers_socks = 0;

    syslog_server = "";
    syslog_port = 514;
    syslog_facility = 23; //local7
    syslog_family = 4;


    // multi-tenancy support
    tenant_name = "default";
    tenant_index = 0;


    traflog_dir = "/var/local/smithproxy/data";
    //traflog_file_prefix = "";
    traflog_file_suffix = "smcap";

    log_console = false;
    ts_sys_started = std::time(nullptr);

}

struct cfgapi_table_ cfgapi_table;

bool CfgFactory::cfgapi_init(const char* fnm) {
    
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    DIAS_("Reading config file");
    
    // Read the file. If there is an error, report it and exit.
    try {
        cfgapi.readFile(fnm);
    }
    catch(const FileIOException &fioex)
    {
        ERR_("I/O error while reading config file: %s",fnm);
        return false;   
    }
    catch(const ParseException &pex)
    {
        ERR_("Parse error in %s at %s:%d - %s", fnm, pex.getFile(), pex.getLine(), pex.getError());
        return false;
    }
    
    ts_sys_started = ::time(nullptr);
    
    return true;
}

AddressObject* CfgFactory::lookup_address (const char *name) {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    if(db_address.find(name) != db_address.end()) {
        return db_address[name];
    }
    
    return nullptr;
}

range CfgFactory::lookup_port (const char *name) {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    if(db_port.find(name) != db_port.end()) {
        return db_port[name];
    }    
    
    return NULLRANGE;
}

int CfgFactory::lookup_proto (const char *name) {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    if(db_proto.find(name) != db_proto.end()) {
        return db_proto[name];
    }    
    
    return 0;
}

ProfileContent* CfgFactory::lookup_prof_content (const char *name) {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    if(db_prof_content.find(name) != db_prof_content.end()) {
        return db_prof_content[name];
    }    
    
    return nullptr;
}

ProfileDetection* CfgFactory::lookup_prof_detection (const char *name) {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    if(db_prof_detection.find(name) != db_prof_detection.end()) {
        return db_prof_detection[name];
    }    
    
    return nullptr;
}

ProfileTls* CfgFactory::lookup_prof_tls (const char *name) {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    if(db_prof_tls.find(name) != db_prof_tls.end()) {
        return db_prof_tls[name];
    }    
    
    return nullptr;
}

ProfileAlgDns* CfgFactory::lookup_prof_alg_dns (const char *name) {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    if(db_prof_alg_dns.find(name) != db_prof_alg_dns.end()) {
        return db_prof_alg_dns[name];
    }    
    
    return nullptr;

}


ProfileAuth* CfgFactory::lookup_prof_auth (const char *name) {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    if(db_prof_auth.find(name) != db_prof_auth.end()) {
        return db_prof_auth[name];
    }    
    
    return nullptr;
}

bool CfgFactory::load_settings () {

    std::lock_guard<std::recursive_mutex> l(lock_);

    if(! cfgapi.getRoot().exists("settings"))
        return false;

    if(cfgapi.getRoot()["settings"].exists("nameservers")) {

        if(!db_nameservers.empty()) {
            DEBS_("load_settings: clearing existing entries in: nameservers");
            db_nameservers.clear();
        }

        const int num = cfgapi.getRoot()["settings"]["nameservers"].getLength();
        for(int i = 0; i < num; i++) {
            std::string ns = cfgapi.getRoot()["settings"]["nameservers"][i];
            db_nameservers.push_back(ns);
        }
    }

    cfgapi.getRoot()["settings"].lookupValue("certs_path",SSLFactory::default_cert_path());
    cfgapi.getRoot()["settings"].lookupValue("certs_ca_key_password",SSLFactory::default_cert_password());
    cfgapi.getRoot()["settings"].lookupValue("certs_ca_path",SSLFactory::default_client_ca_path());

    cfgapi.getRoot()["settings"].lookupValue("plaintext_port",listen_tcp_port_base); listen_tcp_port = listen_tcp_port_base;
    cfgapi.getRoot()["settings"].lookupValue("plaintext_workers",num_workers_tcp);
    cfgapi.getRoot()["settings"].lookupValue("ssl_port",listen_tls_port_base); listen_tls_port = listen_tls_port_base;
    cfgapi.getRoot()["settings"].lookupValue("ssl_workers",num_workers_tls);
    cfgapi.getRoot()["settings"].lookupValue("ssl_autodetect",MitmMasterProxy::ssl_autodetect);
    cfgapi.getRoot()["settings"].lookupValue("ssl_autodetect_harder",MitmMasterProxy::ssl_autodetect_harder);
    cfgapi.getRoot()["settings"].lookupValue("ssl_ocsp_status_ttl",SSLFactory::ssl_ocsp_status_ttl);
    cfgapi.getRoot()["settings"].lookupValue("ssl_crl_status_ttl",SSLFactory::ssl_crl_status_ttl);

    cfgapi.getRoot()["settings"].lookupValue("udp_port",listen_udp_port_base); listen_udp_port = listen_udp_port_base;
    cfgapi.getRoot()["settings"].lookupValue("udp_workers",num_workers_udp);

    cfgapi.getRoot()["settings"].lookupValue("dtls_port",listen_dtls_port_base);  listen_dtls_port = listen_dtls_port_base;
    cfgapi.getRoot()["settings"].lookupValue("dtls_workers",num_workers_dtls);

    if(cfgapi.getRoot()["settings"].exists("udp_quick_ports")) {

        if(!db_udp_quick_ports.empty()) {
            DEBS_("load_settings: clearing existing entries in: udp_quick_ports");
            db_udp_quick_ports.clear();
        }

        int num = cfgapi.getRoot()["settings"]["udp_quick_ports"].getLength();
        for(int i = 0; i < num; ++i) {
            int port = cfgapi.getRoot()["settings"]["udp_quick_ports"][i];
            db_udp_quick_ports.push_back(port);
        }
    }

    cfgapi.getRoot()["settings"].lookupValue("socks_port",listen_socks_port_base); listen_socks_port = listen_socks_port_base;
    cfgapi.getRoot()["settings"].lookupValue("socks_workers",num_workers_socks);

    if(cfgapi.getRoot().exists("settings")) {
        if(cfgapi.getRoot()["settings"].exists("socks")) {
            cfgapi.getRoot()["settings"]["socks"].lookupValue("async_dns", socksServerCX::global_async_dns);
        }
    }

    cfgapi.getRoot()["settings"].lookupValue("log_level",cfgapi_table.logging.level.level_);

    cfgapi.getRoot()["settings"].lookupValue("syslog_server",syslog_server);
    cfgapi.getRoot()["settings"].lookupValue("syslog_port",syslog_port);
    cfgapi.getRoot()["settings"].lookupValue("syslog_facility",syslog_facility);
    cfgapi.getRoot()["settings"].lookupValue("syslog_level",syslog_level.level_);
    cfgapi.getRoot()["settings"].lookupValue("syslog_family",syslog_family);

    cfgapi.getRoot()["settings"].lookupValue("messages_dir",dir_msg_templates);

    cfgapi.getRoot()["settings"]["cli"].lookupValue("port",cli_port_base); cli_port = cli_port_base;
    cfgapi.getRoot()["settings"]["cli"].lookupValue("enable_password",cli_enable_password);


    if(cfgapi.getRoot().exists("settings")) {
        if(cfgapi.getRoot()["settings"].exists("auth_portal")) {
            cfgapi.getRoot()["settings"]["auth_portal"].lookupValue("address", auth_address);
            cfgapi.getRoot()["settings"]["auth_portal"].lookupValue("http_port", auth_http);
            cfgapi.getRoot()["settings"]["auth_portal"].lookupValue("https_port", auth_https);
            cfgapi.getRoot()["settings"]["auth_portal"].lookupValue("ssl_key", auth_sslkey);
            cfgapi.getRoot()["settings"]["auth_portal"].lookupValue("ssl_cert", auth_sslcert);
            cfgapi.getRoot()["settings"]["auth_portal"].lookupValue("magic_ip", tenant_magic_ip);
        }
    }

    return true;
}


int CfgFactory::load_db_address () {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int num = 0;
    
    DIAS_("cfgapi_load_addresses: start");
    
    if(cfgapi.getRoot().exists("address_objects")) {

        num = cfgapi.getRoot()["address_objects"].getLength();
        DIA_("cfgapi_load_addresses: found %d objects",num);
        
        Setting& curr_set = cfgapi.getRoot()["address_objects"];

        for( int i = 0; i < num; i++) {
            std::string name;
            std::string address;
            int type;
            
            Setting& cur_object = curr_set[i];

            if (  ! cur_object.getName() ) {
                DIA_("cfgapi_load_address: unnamed object index %d: not ok", i);
                continue;
            }
            
            name = cur_object.getName();

            DEB_("cfgapi_load_addresses: processing '%s'",name.c_str());
            
            if( cur_object.lookupValue("type",type)) {
                switch(type) {
                    case 0: // CIDR notation
                        if (cur_object.lookupValue("cidr",address)) {
                            CIDR* c = cidr_from_str(address.c_str());
                            db_address[name] = new CidrAddress(c);
                            db_address[name]->prof_name = name;
                            DIA_("cfgapi_load_addresses: cidr '%s': ok",name.c_str());
                        }
                    break;
                    case 1: // FQDN notation
                        if (cur_object.lookupValue("fqdn",address))  {
                            FqdnAddress* f = new FqdnAddress(address);
                            db_address[name] = f;
                            db_address[name]->prof_name = name;
                            DIA_("cfgapi_load_addresses: fqdn '%s': ok",name.c_str());
                        }
                    break;
                    default:
                        DIA_("cfgapi_load_addresses: fqdn '%s': unknown type value(ignoring)",name.c_str());
                }
            } else {
                DIA_("cfgapi_load_addresses: '%s': not ok",name.c_str());
            }
        }
    }
    
    return num;
}

int CfgFactory::load_db_port () {
    
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int num = 0;
    
    DIAS_("cfgapi_load_ports: start");
    
    if(cfgapi.getRoot().exists("port_objects")) {

        num = cfgapi.getRoot()["port_objects"].getLength();
        DIA_("cfgapi_load_ports: found %d objects",num);
        
        Setting& curr_set = cfgapi.getRoot()["port_objects"];

        for( int i = 0; i < num; i++) {
            std::string name;
            int a;
            int b;
            
            Setting& cur_object = curr_set[i];

            if (  ! cur_object.getName() ) {
                DIA_("cfgapi_load_ports: unnamed object index %d: not ok", i);
                continue;
            }

            name = cur_object.getName();

            DEB_("cfgapi_load_ports: processing '%s'",name.c_str());
            
            if( cur_object.lookupValue("start",a) &&
                cur_object.lookupValue("end",b)   ) {
                
                if(a <= b) {
                    db_port[name] = range(a,b);
                } else {
                    db_port[name] = range(b,a);
                }
                
                DIA_("cfgapi_load_ports: '%s': ok",name.c_str());
            } else {
                DIA_("cfgapi_load_ports: '%s': not ok",name.c_str());
            }
        }
    }
    
    return num;
}

int CfgFactory::load_db_proto () {
    
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int num = 0;
    
    DIAS_("cfgapi_load_proto: start");
    
    if(cfgapi.getRoot().exists("proto_objects")) {

        num = cfgapi.getRoot()["proto_objects"].getLength();
        DIA_("cfgapi_load_proto: found %d objects",num);
        
        Setting& curr_set = cfgapi.getRoot()["proto_objects"];

        for( int i = 0; i < num; i++) {
            std::string name;
            int a;
            
            Setting& cur_object = curr_set[i];

            if (  ! cur_object.getName() ) {
                DIA_("cfgapi_load_proto: unnamed object index %d: not ok", i);
                continue;
            }
            
            name = cur_object.getName();

            DEB_("cfgapi_load_proto: processing '%s'",name.c_str());
            
            if( cur_object.lookupValue("id",a) ) {
                
                db_proto[name] = a;
                
                DIA_("cfgapi_load_proto: '%s': ok",name.c_str());
            } else {
                DIA_("cfgapi_load_proto: '%s': not ok",name.c_str());
            }
        }
    }
    
    return num;
}


int CfgFactory::load_db_policy () {
    
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int num = 0;
    
    DIAS_("cfgapi_load_policy: start");
    
    if(cfgapi.getRoot().exists("policy")) {

        num = cfgapi.getRoot()["policy"].getLength();
        DIA_("cfgapi_load_policy: found %d objects",num);
        
        Setting& curr_set = cfgapi.getRoot()["policy"];

        for( int i = 0; i < num; i++) {
            Setting& cur_object = curr_set[i];
            
            std::string proto;
            std::string dst;
            std::string dport;
            std::string src;
            std::string sport;
            std::string profile_detection;
            std::string profile_content;
            std::string action;
            std::string nat;
            
            bool error = false;
            
            DEB_("cfgapi_load_policy: processing #%d",i);
            
            auto* rule = new PolicyRule();

            if(cur_object.lookupValue("proto",proto)) {
                int r = lookup_proto(proto.c_str());
                if(r != 0) {
                    rule->proto_name = proto;
                    rule->proto = r;
                    rule->proto_default = false;
                    DIA_("cfgapi_load_policy[#%d]: proto object: %s",i,proto.c_str());
                } else {
                    DIA_("cfgapi_load_policy[#%d]: proto object not found: %s",i,proto.c_str());
                    error = true;
                }
            }
            
            const Setting& sett_src = cur_object["src"];
            if(sett_src.isScalar()) {
                DIA_("cfgapi_load_policy[#%d]: scalar src address object",i);
                if(cur_object.lookupValue("src",src)) {
                    
                    AddressObject* r = lookup_address(src.c_str());
                    if(r != nullptr) {
                        rule->src.push_back(r);
                        rule->src_default = false;
                        DIA_("cfgapi_load_policy[#%d]: src address object: %s",i,src.c_str());
                    } else {
                        DIA_("cfgapi_load_policy[#%d]: src address object not found: %s",i,src.c_str());
                        error = true;
                    }
                }
            } else {
                int sett_src_count = sett_src.getLength();
                DIA_("cfgapi_load_policy[#%d]: src address list",i);
                for(int y = 0; y < sett_src_count; y++) {
                    const char* obj_name = sett_src[y];
                    
                    AddressObject* r = lookup_address(obj_name);
                    if(r != nullptr) {
                        rule->src.push_back(r);
                        rule->src_default = false;
                        DIA_("cfgapi_load_policy[#%d]: src address object: %s",i,obj_name);
                    } else {
                        DIA_("cfgapi_load_policy[#%d]: src address object not found: %s",i,obj_name);
                        error = true;
                    }

                }
            }
            
            const Setting& sett_sport = cur_object["sport"];
            if(sett_sport.isScalar()) {
                if(cur_object.lookupValue("sport",sport)) {
                    range r = lookup_port(sport.c_str());
                    if(r != NULLRANGE) {
                        rule->src_ports.push_back(r);
                        rule->src_ports_names.push_back(sport);
                        rule->src_ports_default = false;
                        DIA_("cfgapi_load_policy[#%d]: src_port object: %s",i,sport.c_str());
                    } else {
                        DIA_("cfgapi_load_policy[#%d]: src_port object not found: %s",i,sport.c_str());
                        error = true;
                    }
                }
            } else {
                int sett_sport_count = sett_sport.getLength();
                DIA_("cfgapi_load_policy[#%d]: sport list",i);
                for(int y = 0; y < sett_sport_count; y++) {
                    const char* obj_name = sett_sport[y];
                    
                    range r = lookup_port(obj_name);
                    if(r != NULLRANGE) {
                        rule->src_ports.push_back(r);
                        rule->src_ports_names.emplace_back(obj_name);
                        rule->src_ports_default = false;
                        DIA_("cfgapi_load_policy[#%d]: src_port object: %s",i,obj_name);
                    } else {
                        DIA_("cfgapi_load_policy[#%d]: src_port object not found: %s",i,obj_name);
                        error = true;
                    }
                }
            }

            const Setting& sett_dst = cur_object["dst"];
            if(sett_dst.isScalar()) {
                if(cur_object.lookupValue("dst",dst)) {
                    AddressObject* r = lookup_address(dst.c_str());
                    if(r != nullptr) {
                        rule->dst.push_back(r);
                        rule->dst_default = false;
                        DIA_("cfgapi_load_policy[#%d]: dst address object: %s",i,dst.c_str());
                    } else {
                        DIA_("cfgapi_load_policy[#%d]: dst address object not found: %s",i,dst.c_str());
                        error = true;
                    }                
                }
            } else {
                int sett_dst_count = sett_dst.getLength();
                DIA_("cfgapi_load_policy[#%d]: dst list",i);
                for(int y = 0; y < sett_dst_count; y++) {
                    const char* obj_name = sett_dst[y];

                    AddressObject* r = lookup_address(obj_name);
                    if(r != nullptr) {
                        rule->dst.push_back(r);
                        rule->dst_default = false;
                        DIA_("cfgapi_load_policy[#%d]: dst address object: %s",i,obj_name);
                    } else {
                        DIA_("cfgapi_load_policy[#%d]: dst address object not found: %s",i,obj_name);
                        error = true;
                    }                
                }
            }
            
            
            const Setting& sett_dport = cur_object["dport"];
            if(sett_dport.isScalar()) { 
                if(cur_object.lookupValue("dport",dport)) {
                    range r = lookup_port(dport.c_str());
                    if(r != NULLRANGE) {
                        rule->dst_ports.push_back(r);
                        rule->dst_ports_names.push_back(dport);
                        rule->dst_ports_default = false;
                        DIA_("cfgapi_load_policy[#%d]: dst_port object: %s",i,dport.c_str());
                    } else {
                        DIA_("cfgapi_load_policy[#%d]: dst_port object not found: %s",i,dport.c_str());
                        error = true;
                    }
                }
            } else {
                int sett_dport_count = sett_dport.getLength();
                DIA_("cfgapi_load_policy[#%d]: dst_port object list",i);
                for(int y = 0; y < sett_dport_count; y++) {
                    const char* obj_name = sett_dport[y];
                    
                    range r = lookup_port(obj_name);
                    if(r != NULLRANGE) {
                        rule->dst_ports.push_back(r);
                        rule->dst_ports_names.emplace_back(obj_name);
                        rule->dst_ports_default = false;
                        DIA_("cfgapi_load_policy[#%d]: dst_port object: %s",i,obj_name);
                    } else {
                        DIA_("cfgapi_load_policy[#%d]: dst_port object not found: %s",i,obj_name);
                        error = true;
                    }                    
                }
            }
            
            if(cur_object.lookupValue("action",action)) {
                int r_a = POLICY_ACTION_PASS;
                if(action == "deny") {
                    DIA_("cfgapi_load_policy[#%d]: action: deny",i);
                    r_a = POLICY_ACTION_DENY;
                    rule->action_name = action;

                } else if (action == "accept"){
                    DIA_("cfgapi_load_policy[#%d]: action: accept",i);
                    r_a = POLICY_ACTION_PASS;
                    rule->action_name = action;
                } else {
                    DIA_("cfgapi_load_policy[#%d]: action: unknown action '%s'",i,action.c_str());
                    r_a  = POLICY_ACTION_DENY;
                    error = true;
                }
                
                rule->action = r_a;
            } else {
                rule->action = POLICY_ACTION_DENY;
                rule->action_name = "deny";
            }

            if(cur_object.lookupValue("nat",nat)) {
                int nat_a = POLICY_NAT_NONE;
                
                if(nat == "none") {
                    DIA_("cfgapi_load_policy[#%d]: nat: none",i);
                    nat_a = POLICY_NAT_NONE;
                    rule->nat_name = nat;

                } else if (nat == "auto"){
                    DIA_("cfgapi_load_policy[#%d]: nat: auto",i);
                    nat_a = POLICY_NAT_AUTO;
                    rule->nat_name = nat;
                } else {
                    DIA_("cfgapi_load_policy[#%d]: nat: unknown nat method '%s'",i,nat.c_str());
                    nat_a  = POLICY_NAT_NONE;
                    rule->nat_name = "none";
                    error = true;
                }
                
                rule->nat = nat_a;
            } else {
                rule->nat = POLICY_NAT_NONE;
            }            
            
            
            /* try to load policy profiles */
            
            if(rule->action == 1) {
                // makes sense to load profiles only when action is accept! 
                std::string name_content;
                std::string name_detection;
                std::string name_tls;
                std::string name_auth;
                std::string name_alg_dns;
                
                if(cur_object.lookupValue("detection_profile",name_detection)) {
                    ProfileDetection* prf  = lookup_prof_detection(name_detection.c_str());
                    if(prf != nullptr) {
                        DIA_("cfgapi_load_policy[#%d]: detect profile %s",i,name_detection.c_str());
                        rule->profile_detection = prf;
                    } else {
                        ERR_("cfgapi_load_policy[#%d]: detect profile %s cannot be loaded",i,name_detection.c_str());
                        error = true;
                    }
                }
                
                if(cur_object.lookupValue("content_profile",name_content)) {
                    ProfileContent* prf  = lookup_prof_content(name_content.c_str());
                    if(prf != nullptr) {
                        DIA_("cfgapi_load_policy[#%d]: content profile %s",i,name_content.c_str());
                        rule->profile_content = prf;
                    } else {
                        ERR_("cfgapi_load_policy[#%d]: content profile %s cannot be loaded",i,name_content.c_str());
                        error = true;
                    }
                }                
                if(cur_object.lookupValue("tls_profile",name_tls)) {
                    ProfileTls* tls  = lookup_prof_tls(name_tls.c_str());
                    if(tls != nullptr) {
                        DIA_("cfgapi_load_policy[#%d]: tls profile %s",i,name_tls.c_str());
                        rule->profile_tls= tls;
                    } else {
                        ERR_("cfgapi_load_policy[#%d]: tls profile %s cannot be loaded",i,name_tls.c_str());
                        error = true;
                    }
                }         
                if(cur_object.lookupValue("auth_profile",name_auth)) {
                    ProfileAuth* auth  = lookup_prof_auth(name_auth.c_str());
                    if(auth != nullptr) {
                        DIA_("cfgapi_load_policy[#%d]: auth profile %s",i,name_auth.c_str());
                        rule->profile_auth= auth;
                    } else {
                        ERR_("cfgapi_load_policy[#%d]: auth profile %s cannot be loaded",i,name_auth.c_str());
                        error = true;
                    }
                }
                if(cur_object.lookupValue("alg_dns_profile",name_alg_dns)) {
                    ProfileAlgDns* dns  = lookup_prof_alg_dns(name_alg_dns.c_str());
                    if(dns != nullptr) {
                        DIA_("cfgapi_load_policy[#%d]: DNS alg profile %s",i,name_alg_dns.c_str());
                        rule->profile_alg_dns = dns;
                    } else {
                        ERR_("cfgapi_load_policy[#%d]: DNS alg %s cannot be loaded",i,name_alg_dns.c_str());
                        error = true;
                    }
                }                    
            }
            
            if(!error){
                DIA_("cfgapi_load_policy[#%d]: ok",i);
                db_policy.push_back(rule);
            } else {
                ERR_("cfgapi_load_policy[#%d]: not ok (will not process traffic)",i);
            }
        }
    }
    
    return num;
}

int CfgFactory::policy_match (baseProxy *proxy) {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int x = 0;
    for( auto rule: db_policy) {

        bool r = rule->match(proxy);
        
        if(r) {
            DIA_("policy_match: matched #%d",x);
            return x;
        }
        
        x++;
    }
    
    DIAS_("policy_match: implicit deny");
    return -1;
}

int CfgFactory::policy_match (std::vector<baseHostCX *> &left, std::vector<baseHostCX *> &right) {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int x = 0;
    for( auto rule: db_policy) {

        bool r = rule->match(left,right);
        
        if(r) {
            DIA_("cfgapi_obj_policy_match_lr: matched #%d",x);
            return x;
        }
        
        x++;
    }
    
    DIAS_("cfgapi_obj_policy_match_lr: implicit deny");
    return -1;
}    

int CfgFactory::policy_action (int index) {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    if(index < 0) {
        return -1;
    }
    
    if(index < (signed int)db_policy.size()) {
        return db_policy.at(index)->action;
    } else {
        DIA_("cfg_obj_policy_action[#%d]: out of bounds, deny",index);
        return POLICY_ACTION_DENY;
    }
}

ProfileContent* CfgFactory::policy_prof_content (int index) {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    if(index < 0) {
        return nullptr;
    }
    
    if(index < (signed int)db_policy.size()) {
        return db_policy.at(index)->profile_content;
    } else {
        DIA_("policy_prof_content[#%d]: out of bounds, nullptr",index);
        return nullptr;
    }
}

ProfileDetection* CfgFactory::policy_prof_detection (int index) {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    if(index < 0) {
        return nullptr;
    }
    
    if(index < (signed int)db_policy.size()) {
        return db_policy.at(index)->profile_detection;
    } else {
        DIA_("policy_prof_detection[#%d]: out of bounds, nullptr",index);
        return nullptr;
    }
}

ProfileTls* CfgFactory::policy_prof_tls (int index) {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    if(index < 0) {
        return nullptr;
    }
    
    if(index < (signed int)db_policy.size()) {
        return db_policy.at(index)->profile_tls;
    } else {
        DIA_("policy_prof_tls[#%d]: out of bounds, nullptr",index);
        return nullptr;
    }
}


ProfileAlgDns* CfgFactory::policy_prof_alg_dns (int index) {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    if(index < 0) {
        return nullptr;
    }
    
    if(index < (signed int)db_policy.size()) {
        return db_policy.at(index)->profile_alg_dns;
    } else {
        DIA_("policy_prof_alg_dns[#%d]: out of bounds, nullptr",index);
        return nullptr;
    }
}


ProfileAuth* CfgFactory::policy_prof_auth (int index) {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    if(index < 0) {
        return nullptr;
    }
    
    if(index < (signed int)db_policy.size()) {
        return db_policy.at(index)->profile_auth;
    } else {
        DIA_("policy_prof_auth[#%d]: out of bounds, nullptr",index);
        return nullptr;
    }
}



int CfgFactory::load_db_prof_detection () {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int num = 0;
    
    DIAS_("cfgapi_load_obj_profile_detect: start");
    
    if(cfgapi.getRoot().exists("detection_profiles")) {

        num = cfgapi.getRoot()["detection_profiles"].getLength();
        DIA_("cfgapi_load_obj_profile_detect: found %d objects",num);
        
        Setting& curr_set = cfgapi.getRoot()["detection_profiles"];
        
        for( int i = 0; i < num; i++) {
            std::string name;
            auto* a = new ProfileDetection;
            
            Setting& cur_object = curr_set[i];

            if (  ! cur_object.getName() ) {
                DIA_("cfgapi_load_obj_profile_detect: unnamed object index %d: not ok", i);
                continue;
            }

            name = cur_object.getName();
           
            DIA_("cfgapi_load_obj_profile_detect: processing '%s'",name.c_str());
            
            if( cur_object.lookupValue("mode",a->mode) ) {
                
                a->prof_name = name;
                db_prof_detection[name] = a;
                
                DIA_("cfgapi_load_obj_profile_detect: '%s': ok",name.c_str());
            } else {
                DIA_("cfgapi_load_obj_profile_detect: '%s': not ok",name.c_str());
            }
        }
    }
    
    return num;
}


int CfgFactory::load_db_prof_content () {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int num = 0;
    
    DIAS_("load_db_prof_content: start");
    
    if(cfgapi.getRoot().exists("content_profiles")) {

        num = cfgapi.getRoot()["content_profiles"].getLength();
        DIA_("load_db_prof_content: found %d objects",num);
        
        Setting& curr_set = cfgapi.getRoot()["content_profiles"];

        for( int i = 0; i < num; i++) {
            std::string name;
            auto* a = new ProfileContent;
            
            Setting& cur_object = curr_set[i];

            if (  ! cur_object.getName() ) {
                DIA_("load_db_prof_content: unnamed object index %d: not ok", i);
                continue;
            }

            name = cur_object.getName();

            DEB_("load_db_prof_content: processing '%s'",name.c_str());
            
            if( cur_object.lookupValue("write_payload",a->write_payload) ) {
                
                a->prof_name = name;
                db_prof_content[name] = a;
                
                if(cur_object.exists("content_rules")) {
                    int jnum = cur_object["content_rules"].getLength();
                    DIA_("replace rules in profile '%s', size %d",name.c_str(),jnum);
                    for (int j = 0; j < jnum; j++) {
                        Setting& cur_replace_rule = cur_object["content_rules"][j];

                        std::string m;
                        std::string r;
                        bool action_defined = false;
                        
                        bool fill_length = false;
                        int replace_each_nth = 0;
                        
                        cur_replace_rule.lookupValue("match",m);
                        
                        if(cur_replace_rule.lookupValue("replace",r)) {
                            action_defined = true;
                        }
                        
                        //optional
                        cur_replace_rule.lookupValue("fill_length",fill_length);
                        cur_replace_rule.lookupValue("replace_each_nth",replace_each_nth);
                        
                        if( (! m.empty() ) && action_defined) {
                            DIA_("    [%d] match '%s' and replace with '%s'",j,m.c_str(),r.c_str());
                            ProfileContentRule p;
                            p.match = m;
                            p.replace = r;
                            p.fill_length = fill_length;
                            p.replace_each_nth = replace_each_nth;

                            a->content_rules.push_back(p);
                            
                        } else {
                            ERR_("    [%d] unfinished replace policy",j);
                        }
                    }
                }
                
                
                DIA_("load_db_prof_content: '%s': ok",name.c_str());
            } else {
                DIA_("load_db_prof_content: '%s': not ok",name.c_str());
            }
        }
    }
    
    return num;
}

int CfgFactory::load_db_prof_tls () {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int num = 0;
    
    DIAS_("load_db_prof_tls: start");
    
    if(cfgapi.getRoot().exists("tls_profiles")) {

        num = cfgapi.getRoot()["tls_profiles"].getLength();
        DIA_("load_db_prof_tls: found %d objects",num);
        
        Setting& curr_set = cfgapi.getRoot()["tls_profiles"];

        for( int i = 0; i < num; i++) {
            std::string name;
            auto* a = new ProfileTls;
            
            Setting& cur_object = curr_set[i];

            if (  ! cur_object.getName() ) {
                DIA_("load_db_prof_tls: unnamed object index %d: not ok", i);
                continue;
            }

            name = cur_object.getName();

            DEB_("load_db_prof_tls: processing '%s'",name.c_str());
            
            if( cur_object.lookupValue("inspect",a->inspect) ) {
                
                a->prof_name = name;
                cur_object.lookupValue("allow_untrusted_issuers",a->allow_untrusted_issuers);
                cur_object.lookupValue("allow_invalid_certs",a->allow_invalid_certs);
                cur_object.lookupValue("allow_self_signed",a->allow_self_signed);
                cur_object.lookupValue("use_pfs",a->use_pfs);
                cur_object.lookupValue("left_use_pfs",a->left_use_pfs);
                cur_object.lookupValue("right_use_pfs",a->right_use_pfs);
                cur_object.lookupValue("left_disable_reuse",a->left_disable_reuse);
                cur_object.lookupValue("right_disable_reuse",a->right_disable_reuse);
                
                cur_object.lookupValue("ocsp_mode",a->ocsp_mode);
                cur_object.lookupValue("ocsp_stapling",a->ocsp_stapling);
                cur_object.lookupValue("ocsp_stapling_mode",a->ocsp_stapling_mode);
                cur_object.lookupValue("failed_certcheck_replacement",a->failed_certcheck_replacement);
                cur_object.lookupValue("failed_certcheck_override",a->failed_certcheck_override);
                cur_object.lookupValue("failed_certcheck_override_timeout",a->failed_certcheck_override_timeout);
                cur_object.lookupValue("failed_certcheck_override_timeout_type",a->failed_certcheck_override_timeout_type);
                
                if(cur_object.exists("sni_filter_bypass")) {
                        Setting& sni_filter = cur_object["sni_filter_bypass"];
                        
                        //init only when there is something
                        int sni_filter_len = sni_filter.getLength();
                        if(sni_filter_len > 0) {
                                a->sni_filter_bypass.ptr(new std::vector<std::string>);
                                for(int j = 0; j < sni_filter_len; ++j) {
                                    const char* elem = sni_filter[j];
                                    a->sni_filter_bypass.ptr()->push_back(elem);
                                }
                        }
                }
                

                if(cur_object.exists("redirect_warning_ports")) {
                        Setting& rwp = cur_object["redirect_warning_ports"];
                        
                        //init only when there is something
                        int rwp_len = rwp.getLength();
                        if(rwp_len > 0) {
                                a->redirect_warning_ports.ptr(new std::set<int>);
                                for(int j = 0; j < rwp_len; ++j) {
                                    int elem = rwp[j];
                                    a->redirect_warning_ports.ptr()->insert(elem);
                                }
                        }
                }
                cur_object.lookupValue("sslkeylog",a->sslkeylog);
                
                db_prof_tls[name] = a;
                
                DIA_("load_db_prof_tls: '%s': ok",name.c_str());
            } else {
                DIA_("load_db_prof_tls: '%s': not ok",name.c_str());
            }
        }
    }
    
    return num;
}

int CfgFactory::load_db_prof_alg_dns () {
    std::lock_guard<std::recursive_mutex> l(lock_);

    int num = 0;
    DIAS_("cfgapi_load_obj_alg_dns_profile: start");
    if(cfgapi.getRoot().exists("alg_dns_profiles")) {
        num = cfgapi.getRoot()["alg_dns_profiles"].getLength();
        DIA_("cfgapi_load_obj_alg_dns_profile: found %d objects",num);
        
        Setting& curr_set = cfgapi.getRoot()["alg_dns_profiles"];

        for( int i = 0; i < num; i++) {
            std::string name;
            auto* a = new ProfileAlgDns;
            
            Setting& cur_object = curr_set[i];

            if (  ! cur_object.getName() ) {
                DIA_("cfgapi_load_obj_alg_dns_profile: unnamed object index %d: not ok", i);
                continue;
            }
            
            name = cur_object.getName();

            DEB_("cfgapi_load_obj_alg_dns_profile: processing '%s'",name.c_str());
            
            a->prof_name = name;
            cur_object.lookupValue("match_request_id",a->match_request_id);
            cur_object.lookupValue("randomize_id",a->randomize_id);
            cur_object.lookupValue("cached_responses",a->cached_responses);
            
            db_prof_alg_dns[name] = a;
        }
    }
    
    return num;
}


int CfgFactory::load_db_prof_auth () {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int num = 0;
    
    DIAS_("load_db_prof_auth: start");
    
    DIAS_("load_db_prof_auth: portal settings");
    cfgapi.getRoot()["settings"]["auth_portal"].lookupValue("address",cfgapi_identity_portal_address);
    cfgapi.getRoot()["settings"]["auth_portal"].lookupValue("address6",cfgapi_identity_portal_address6);
    cfgapi.getRoot()["settings"]["auth_portal"].lookupValue("http_port",cfgapi_identity_portal_port_http);
    cfgapi.getRoot()["settings"]["auth_portal"].lookupValue("https_port",cfgapi_identity_portal_port_https);    
    
    DIAS_("load_db_prof_auth: profiles");
    if(cfgapi.getRoot().exists("auth_profiles")) {

        num = cfgapi.getRoot()["auth_profiles"].getLength();
        DIA_("load_db_prof_auth: found %d objects",num);
        
        Setting& curr_set = cfgapi.getRoot()["auth_profiles"];

        for( int i = 0; i < num; i++) {
            std::string name;
            auto* a = new ProfileAuth;
            
            Setting& cur_object = curr_set[i];

            if (  ! cur_object.getName() ) {
                DIA_("load_db_prof_auth: unnamed object index %d: not ok", i);
                continue;
            }
            
            name = cur_object.getName();

            DEB_("load_db_prof_auth: processing '%s'",name.c_str());
            
            a->prof_name = name;
            cur_object.lookupValue("authenticate",a->authenticate);
            cur_object.lookupValue("resolve",a->resolve);
            
            if(cur_object.exists("identities")) {
                DIAS_("load_db_prof_auth: profiles: subpolicies exists");
                int sub_pol_num = cur_object["identities"].getLength();
                DIA_("load_db_prof_auth: profiles: %d subpolicies detected",sub_pol_num);
                for (int j = 0; j < sub_pol_num; j++) {
                    Setting& cur_subpol = cur_object["identities"][j];
                    
                    auto* n_subpol = new ProfileSubAuth();

                    if (  ! cur_subpol.getName() ) {
                        DIA_("load_db_prof_auth: profiles: unnamed object index %d: not ok", j);
                        continue;
                    }

                    n_subpol->name = cur_subpol.getName();
                    
                    std::string name_content;
                    std::string name_detection;
                    std::string name_tls;
                    std::string name_auth;
                    std::string name_alg_dns;
                    
                    if(cur_subpol.lookupValue("detection_profile",name_detection)) {
                        ProfileDetection* prf  = lookup_prof_detection(name_detection.c_str());
                        if(prf != nullptr) {
                            DIA_("load_db_prof_auth[sub-profile:%s]: detect profile %s",n_subpol->name.c_str(),name_detection.c_str());
                            n_subpol->profile_detection = prf;
                        } else {
                            ERR_("load_db_prof_auth[sub-profile:%s]: detect profile %s cannot be loaded",n_subpol->name.c_str(),name_detection.c_str());
                        }
                    }
                    
                    if(cur_subpol.lookupValue("content_profile",name_content)) {
                        ProfileContent* prf  = lookup_prof_content(name_content.c_str());
                        if(prf != nullptr) {
                            DIA_("load_db_prof_auth[sub-profile:%s]: content profile %s",n_subpol->name.c_str(),name_content.c_str());
                            n_subpol->profile_content = prf;
                        } else {
                            ERR_("load_db_prof_auth[sub-profile:%s]: content profile %s cannot be loaded",n_subpol->name.c_str(),name_content.c_str());
                        }
                    }                
                    if(cur_subpol.lookupValue("tls_profile",name_tls)) {
                        ProfileTls* tls  = lookup_prof_tls(name_tls.c_str());
                        if(tls != nullptr) {
                            DIA_("load_db_prof_auth[sub-profile:%s]: tls profile %s",n_subpol->name.c_str(),name_tls.c_str());
                            n_subpol->profile_tls= tls;
                        } else {
                            ERR_("load_db_prof_auth[sub-profile:%s]: tls profile %s cannot be loaded",n_subpol->name.c_str(),name_tls.c_str());
                        }
                    }         

                    // we don't need auth profile in auth sub-profile
                    
                    if(cur_subpol.lookupValue("alg_dns_profile",name_alg_dns)) {
                        ProfileAlgDns* dns  = lookup_prof_alg_dns(name_alg_dns.c_str());
                        if(dns != nullptr) {
                            DIA_("load_db_prof_auth[sub-profile:%s]: DNS alg profile %s",n_subpol->name.c_str(),name_alg_dns.c_str());
                            n_subpol->profile_alg_dns = dns;
                        } else {
                            ERR_("load_db_prof_auth[sub-profile:%s]: DNS alg %s cannot be loaded",n_subpol->name.c_str(),name_alg_dns.c_str());
                        }
                    }                    

                    
                    a->sub_policies.push_back(n_subpol);
                    DIA_("load_db_prof_auth: profiles: %d:%s",j,n_subpol->name.c_str());
                }
            }
            db_prof_auth[name] = a;
            
            DIA_("load_db_prof_auth: '%s': ok",name.c_str());
        }
    }
    
    return num;
}




int CfgFactory::cleanup_db_address () {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int r = db_address.size();

    for (auto& a: db_address)
    {
        AddressObject* c = a.second;
        delete c;
    }
    
    db_address.clear();
    
    DEB_("cleanup_db_address: %d objects freed",r);
    return r;
}

int CfgFactory::cleanup_db_policy () {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int r = db_policy.size();

    for(auto* ptr: db_policy) {
        delete ptr;
    }
    db_policy.clear();
    
    DEB_("cleanup_db_policy: %d objects freed",r);
    return r;
}

int CfgFactory::cleanup_db_port () {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int r = db_port.size();
    db_port.clear();
    
    return r;
}

int CfgFactory::cleanup_db_proto () {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int r = db_proto.size();
    db_proto.clear();
    
    return r;
}


int CfgFactory::cleanup_db_prof_content () {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int r = db_prof_content.size();
    for(auto& t: db_prof_content) {
        ProfileContent* c = t.second;
        delete c;
    }
    db_prof_content.clear();
    
    return r;
}
int CfgFactory::cleanup_db_prof_detection () {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int r = db_prof_detection.size();
    for(auto& t: db_prof_detection) {
        ProfileDetection* c = t.second;
        delete c;
    }
    db_prof_detection.clear();
    
    return r;
}
int CfgFactory::cleanup_db_prof_tls () {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int r = db_prof_tls.size();
    for(auto& t: db_prof_tls) {
        ProfileTls* c = t.second;
        delete c;
    }
    db_prof_tls.clear();
    
    return r;
}

int CfgFactory::cleanup_db_prof_alg_dns () {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int r = db_prof_alg_dns.size();
    for(auto& t: db_prof_alg_dns) {
        ProfileAlgDns* c = t.second;
        delete c;
    }
    db_prof_alg_dns.clear();
    
    return r;
}



int CfgFactory::cleanup_db_prof_auth () {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int r = db_prof_auth.size();
    for(auto& t: db_prof_auth) {
        ProfileAuth* c = t.second;
        
        for(auto j: c->sub_policies) {
            delete j;
        }
        
        delete c;
    }
    db_prof_auth.clear();
    
    return r;
}


bool CfgFactory::prof_content_apply (baseHostCX *originator, baseProxy *new_proxy, ProfileContent *pc) {
    
    auto* mitm_proxy = dynamic_cast<MitmProxy*>(new_proxy);

    bool ret = true;
    bool cfg_wrt;

    if(mitm_proxy != nullptr) {
        if(pc != nullptr) {
            const char* pc_name = pc->prof_name.c_str();
            DIA_("policy_apply: policy content profile[%s]: write payload: %d", pc_name, pc->write_payload);
            mitm_proxy->write_payload(pc->write_payload);
    
            if( ! pc->content_rules.empty() ) {
                DIA_("policy_apply: policy content profile[%s]: applying content rules, size %d", pc_name, pc->content_rules.size());
                mitm_proxy->init_content_replace();
                mitm_proxy->content_replace(pc->content_rules);
            }
        }
        else if(cfgapi.getRoot()["settings"].lookupValue("default_write_payload",cfg_wrt)) {
            DIA_("policy_apply: global content profile: %d", cfg_wrt);
            mitm_proxy->write_payload(cfg_wrt);
        }
        
        if(mitm_proxy->write_payload()) {
            mitm_proxy->toggle_tlog();
            mitm_proxy->tlog()->left_write("Connection start\n");
        }
    } else {
        WARS_("policy_apply: cannot apply content profile: cast to MitmProxy failed.");
        ret = false;
    } 
    
    return ret;
}


bool CfgFactory::prof_detect_apply (baseHostCX *originator, baseProxy *new_proxy, ProfileDetection *pd) {

    auto* mitm_originator = dynamic_cast<AppHostCX*>(originator);

    const char* pd_name = "none";
    bool ret = true;
    
    // we scan connection on client's side
    if(mitm_originator != nullptr) {
        mitm_originator->mode(AppHostCX::MODE_NONE);
        if(pd != nullptr)  {
            pd_name = pd->prof_name.c_str();
            DIA_("policy_apply[%s]: policy detection profile: mode: %d", pd_name, pd->mode);
            mitm_originator->mode(pd->mode);
        }
    } else {
        WARS_("policy_apply: cannot apply detection profile: cast to AppHostCX failed.");
        ret = false;
    }    
    
    return ret;
}

bool CfgFactory::prof_tls_apply (baseHostCX *originator, baseProxy *new_proxy, ProfileTls *ps) {

    auto* mitm_proxy = dynamic_cast<MitmProxy*>(new_proxy);
    auto* mitm_originator = dynamic_cast<AppHostCX*>(originator);
    
    bool tls_applied = false;
    
    if(ps != nullptr) {
        // we should also apply tls profile to originating side! Verification is not in effect, but BYPASS is!
        if (policy_apply_tls(ps, mitm_originator->com())) {
            
            for( auto* cx: mitm_proxy->rs()) {
                baseCom* xcom = cx->com();
                
                tls_applied = policy_apply_tls(ps, xcom);
                if(!tls_applied) {
                    ERR_("%s: cannot apply TLS profile to target connection %s",new_proxy->c_name(), cx->c_name());
                } else {
                    
                    //applying bypass based on DNS cache
                    
                    auto* sslcom = dynamic_cast<SSLCom*>(xcom);
                    if(sslcom && ps->sni_filter_bypass.valid()) {
                        if( ( ! ps->sni_filter_bypass.ptr()->empty() ) && ps->sni_filter_use_dns_cache) {
                        
                            bool interrupt = false;
                            for(std::string& filter_element: *ps->sni_filter_bypass) {
                                FqdnAddress f(filter_element);
                                CIDR* c = cidr_from_str(xcom->owner_cx()->host().c_str());
                                
                                if(f.match(c)) {
                                    if(sslcom->bypass_me_and_peer()) {
                                        INF_("Connection %s bypassed: IP in DNS cache matching TLS bypass list (%s).",originator->full_name('L').c_str(),filter_element.c_str());
                                        interrupt = true;
                                    } else {
                                        WAR_("Connection %s: cannot be bypassed.",originator->full_name('L').c_str());
                                    }
                                } else if (ps->sni_filter_use_dns_domain_tree) {
                                    std::lock_guard<std::recursive_mutex> l_(domain_cache.getlock());

                                    //INF_("FQDN doesn't match SNI element, looking for sub-domains of %s", filter_element.c_str());
                                    //FIXME: we assume filter is 2nd level domain...
                                    
                                    auto subdomain_cache = domain_cache.get(filter_element);
                                    if(subdomain_cache != nullptr) {
                                        for(auto const& subdomain: subdomain_cache->cache()) {
                                            
                                            std::vector<std::string> prefix_n_domainname = string_split(subdomain.first,':');
                                            if(prefix_n_domainname.size() < 2) continue; // don't continue if we can't strip A: or AAAA:
                                            
                                            FqdnAddress ff(prefix_n_domainname.at(1)+"."+filter_element);
                                            DEB_("Connection %s: subdomain check: test if %s matches %s",originator->full_name('L').c_str(),ff.to_string().c_str(),xcom->owner_cx()->host().c_str());
                                            
                                            if(ff.match(c)) {
                                                if(sslcom->bypass_me_and_peer()) {
                                                    INF_("Connection %s bypassed: IP in DNS sub-domain cache matching TLS bypass list (%s).",originator->full_name('L').c_str(),filter_element.c_str());
                                                } else {
                                                    WAR_("Connection %s: cannot be bypassed.",originator->full_name('L').c_str());                                                    
                                                }
                                                interrupt = true; //exit also from main loop
                                                break;
                                            }
                                        }
                                    }
                                }
                                
                                delete c;
                                
                                if(interrupt) 
                                    break;
                            }                        
                            
                        }
                    }
                }
            }
        }
    } 
    
    return tls_applied;
}

bool CfgFactory::prof_alg_dns_apply (baseHostCX *originator, baseProxy *new_proxy, ProfileAlgDns *p_alg_dns) {

    auto* mitm_originator = dynamic_cast<AppHostCX*>(originator);
    auto* mh = dynamic_cast<MitmHostCX*>(mitm_originator);

    bool ret = false;
    
    if(mh != nullptr) {

        if(p_alg_dns != nullptr) {
            auto* n = new DNS_Inspector();
            if(n->l4_prefilter(mh)) {
                n->opt_match_id = p_alg_dns->match_request_id;
                n->opt_randomize_id = p_alg_dns->randomize_id;
                n->opt_cached_responses = p_alg_dns->cached_responses;
                mh->inspectors_.push_back(n);
                ret = true;
            }
            else {
                delete n;
            }
        }
        
    } else {
        NOT_("Connection %s cannot be inspected by ALGs",originator->full_name('L').c_str());
    }    
    
    return ret;
}

int CfgFactory::policy_apply (baseHostCX *originator, baseProxy *proxy) {
    std::lock_guard<std::recursive_mutex> l(lock_);
    
    int policy_num = policy_match(proxy);
    int verdict = policy_action(policy_num);
    if(verdict == POLICY_ACTION_PASS) {

        ProfileContent *pc = policy_prof_content(policy_num);
        ProfileDetection *pd = policy_prof_detection(policy_num);
        ProfileTls *pt = policy_prof_tls(policy_num);
        ProfileAuth *pa = policy_prof_auth(policy_num);
        ProfileAlgDns *p_alg_dns = policy_prof_alg_dns(policy_num);


        const char *pc_name = "none";
        const char *pd_name = "none";
        const char *pt_name = "none";
        const char *pa_name = "none";

        //Algs will be list of single letter abreviations
        // DNS alg: D
        std::string algs_name;

        /* Processing content profile */
        if (pc) {
            if (prof_content_apply(originator, proxy, pc)) {
                pc_name = pc->prof_name.c_str();
            }
        }
        
        
        /* Processing detection profile */
        if (pd) {
            if(prof_detect_apply(originator, proxy, pd)) {
                pd_name = pd->prof_name.c_str();
            }
        }
        
        /* Processing TLS profile*/
        if (pt) {
            if(prof_tls_apply(originator, proxy, pt)) {
                pt_name = pt->prof_name.c_str();
            }
        }
        
        /* Processing ALG : DNS*/
        if (p_alg_dns) {
            if (prof_alg_dns_apply(originator, proxy, p_alg_dns)) {
                algs_name += p_alg_dns->prof_name;
            }
        }

        
        auto* mitm_proxy = dynamic_cast<MitmProxy*>(proxy);
        auto* mitm_originator = dynamic_cast<AppHostCX*>(originator);
        
        /* Processing Auth profile */
        if(pa && mitm_proxy) {
            // auth is applied on proxy
            mitm_proxy->opt_auth_authenticate = pa->authenticate;
            mitm_proxy->opt_auth_resolve = pa->resolve;
            
            pa_name = pa->prof_name.c_str();
        } 
        
        // ALGS can operate only on MitmHostCX classes

        
        INF_("Connection %s accepted: policy=%d cont=%s det=%s tls=%s auth=%s algs=%s",originator->full_name('L').c_str(),policy_num,pc_name,pd_name,pt_name,pa_name,algs_name.c_str());
        
    } else {
        INF_("Connection %s denied: policy=%d",originator->full_name('L').c_str(),policy_num);
    }
    
    return policy_num;
}


bool CfgFactory::policy_apply_tls (int policy_num, baseCom *xcom) {
    ProfileTls* pt = policy_prof_tls(policy_num);
    return policy_apply_tls(pt, xcom);
}

bool CfgFactory::should_redirect (ProfileTls *pt, SSLCom *com) {
    
    bool ret = false;
    
    DEB_("should_redirect[%s]",com->hr());
    
    if(com && com->owner_cx()) {
        
        try {
            int num_port = std::stoi(com->owner_cx()->port());
            DEB_("should_redirect[%s]: owner port %d",com->hr(), num_port);
            
            
            if(pt->redirect_warning_ports.ptr()) {
                // we have port redirection list (which ports should be redirected/replaced for cert issue warning)
                DEB_("should_redirect[%s]: checking port list present",com->hr());
                
                auto it = pt->redirect_warning_ports.ptr()->find(num_port);
                
                if(it != pt->redirect_warning_ports.ptr()->end()) {
                    DIA_("should_redirect[%s]: port %d in redirect list",com->hr(),num_port);
                    ret = true;
                }
            }
            else {
                // if we have list empty (uninitialized), we assume only 443 should be redirected
                if(num_port == 443) {
                    DEB_("should_redirect[%s]: implicit 443 redirection allowed (no port list)",com->hr());
                    ret = true;
                }
            }
        }
        catch(std::invalid_argument& ) {}
        catch(std::out_of_range& ) {}
    }
    
    return ret;
}

bool CfgFactory::policy_apply_tls (ProfileTls *pt, baseCom *xcom) {

    bool tls_applied = false;     
    
    if(pt != nullptr) {
        auto* sslcom = dynamic_cast<SSLCom*>(xcom);
        if(sslcom != nullptr) {
            sslcom->opt_bypass = !pt->inspect;
            sslcom->opt_allow_unknown_issuer = pt->allow_untrusted_issuers;
            sslcom->opt_allow_self_signed_chain = pt->allow_untrusted_issuers;
            sslcom->opt_allow_not_valid_cert = pt->allow_invalid_certs;
            sslcom->opt_allow_self_signed_cert = pt->allow_self_signed;

            auto* peer_sslcom = dynamic_cast<SSLCom*>(sslcom->peer());

            if( peer_sslcom &&
                    pt->failed_certcheck_replacement &&
                    should_redirect(pt, peer_sslcom)) {

                DEB_("policy_apply_tls: applying profile, repl=%d, repl_ovrd=%d, repl_ovrd_tmo=%d, repl_ovrd_tmo_type=%d",
                     pt->failed_certcheck_replacement,
                     pt->failed_certcheck_override,
                     pt->failed_certcheck_override_timeout,
                     pt->failed_certcheck_override_timeout_type );

                peer_sslcom->opt_failed_certcheck_replacement = pt->failed_certcheck_replacement;
                peer_sslcom->opt_failed_certcheck_override = pt->failed_certcheck_override;
                peer_sslcom->opt_failed_certcheck_override_timeout = pt->failed_certcheck_override_timeout;
                peer_sslcom->opt_failed_certcheck_override_timeout_type = pt->failed_certcheck_override_timeout_type;
            }
            
            // set accordingly if general "use_pfs" is specified, more conrete settings come later
            sslcom->opt_left_kex_dh = pt->use_pfs;
            sslcom->opt_right_kex_dh = pt->use_pfs;
            
            sslcom->opt_left_kex_dh = pt->left_use_pfs;
            sslcom->opt_right_kex_dh = pt->right_use_pfs;
            
            sslcom->opt_left_no_tickets = pt->left_disable_reuse;
            sslcom->opt_right_no_tickets = pt->right_disable_reuse;
            
            sslcom->opt_ocsp_mode = pt->ocsp_mode;
            sslcom->opt_ocsp_stapling_enabled = pt->ocsp_stapling;
            sslcom->opt_ocsp_stapling_mode = pt->ocsp_stapling_mode;
       
            if(pt->sni_filter_bypass.valid()) {
                if( ! pt->sni_filter_bypass.ptr()->empty() ) {
                    sslcom->sni_filter_to_bypass().ref(pt->sni_filter_bypass);
                }
            }
            
            sslcom->sslkeylog = pt->sslkeylog;
            
            tls_applied = true;
        }        
    }
    
    return tls_applied;
}


void CfgFactory::cfgapi_cleanup()
{
    cleanup_db_policy();
    cleanup_db_address();
    cleanup_db_port();
    cleanup_db_proto();
    cleanup_db_prof_content();
    cleanup_db_prof_detection();
    cleanup_db_prof_tls();
    cleanup_db_prof_auth();
    cleanup_db_prof_alg_dns();
}


void CfgFactory::log_version (bool warn_delay)
{
    CRI_("Starting Smithproxy %s (socle %s)",SMITH_VERSION,SOCLE_VERSION);
    
    if(SOCLE_DEVEL || SMITH_DEVEL) {
        WARS_("");
        if(SOCLE_DEVEL) {
            WAR_("Socle library version %s (dev)",SOCLE_VERSION);
        }
#ifdef SOCLE_MEM_PROFILE
        WARS_("*** PERFORMANCE: Socle library has extra memory profiling enabled! ***");
#endif
        if(SMITH_DEVEL) {
            WAR_("Smithproxy version %s (dev)",SMITH_VERSION);
        }        
        WARS_("");
        
        if(warn_delay) {
            WARS_("  ... start will continue in 3 sec.");
            sleep(3);
        }
    }
}

int CfgFactory::apply_tenant_index(std::string& what, unsigned int& idx) {
    DEB_("apply_index: what=%s idx=%d",what.c_str(),idx);
    int port = std::stoi(what);
    what = std::to_string(port + idx);

    return 0;
}


bool CfgFactory::apply_tenant_config () {
    int ret = 0;

    if( (  tenant_index > 0 ) && ( ! tenant_name.empty() ) ) {
        ret += apply_tenant_index(listen_tcp_port, tenant_index);
        ret += apply_tenant_index(listen_tls_port, tenant_index);
        ret += apply_tenant_index(listen_dtls_port, tenant_index);
        ret += apply_tenant_index(listen_udp_port, tenant_index);
        ret += apply_tenant_index(listen_socks_port, tenant_index);
        ret += apply_tenant_index(cfgapi_identity_portal_port_http, tenant_index);
        ret += apply_tenant_index(cfgapi_identity_portal_port_https, tenant_index);

        cli_port += tenant_index;
    }

    return (ret == 0);
}