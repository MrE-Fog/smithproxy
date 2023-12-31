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
#include <ostream>
#include <ios>

#include <getopt.h>

#include <socle.hpp>

#include <log/logger.hpp>
#include <hostcx.hpp>
#include <baseproxy.hpp>
#include <threadedacceptor.hpp>

#include <main.hpp>

#include <libconfig.h++>
#include <service/cfgapi/cfgapi.hpp>

#include <service/daemon.hpp>
#include <service/netservice.hpp>
#include <smithlog.hpp>

#include <service/smithd/smithdcx.hpp>

//#define MEM_DEBUG 1
#ifdef MEM_DEBUG
    #include <mcheck.h>
#endif


class SmithServerCX : public SmithProtoCX, private LoganMate {
public:
    SmithServerCX(baseCom* c, int s) : SmithProtoCX(c,s) {};
    SmithServerCX(baseCom* c, const char* h, const char* p) : SmithProtoCX(c,h,p) {};
    ~SmithServerCX() override = default;

    std::string& class_name() const override {  static std::string s = "SmithServerCX"; return s; };
    std::string hr() const override { return class_name(); }

    void process_package(LTVEntry* e) override {
        _deb("Package dump: \n%s",e->hr().c_str());
        
        
        LTVEntry* m = e->search({CL_REQTYPE});
        if(m && m->data_int() == RQT_RATEURL) {
        
            LTVEntry* rating_rq = e->search({CL_PAYLOAD,RQT_RATEURL});
            if(rating_rq) {
                _inf("found rating URI: %s",rating_rq->data_str().c_str());
                
                LTVEntry* resp = pkg_rating_response(111);
                resp->pack();
                
                send(resp);
                
                delete resp;
                
            } else {
                _err("No payload or rating URL entry.");
                LTVEntry* resp = pkg_error_response("nothing to rate");
                resp->pack();
                send(resp);
                
                delete resp;
            }

        } else {
            if(m) {
                _err("Unsupported request type %d", m->id());
            } else {
                _err("Request type unspecified, ignoring message.");
            }
            
            LTVEntry* resp = pkg_error_response("unknown request");
            resp->pack();
            send(resp);
            
            delete resp;
        }
    };
    
    [[nodiscard]] // discarding leaks memory
    static LTVEntry* pkg_error_response(const char* text = nullptr) {
        auto* e = new LTVEntry();
        e->container(CL_INIT);
 
        auto* err = new LTVEntry();
        err->set_num(255,LTVEntry::num,255);
        e->add(err);
        
        if(text != nullptr) {
            auto* err2 = new LTVEntry();
            err2->set_str(254,LTVEntry::str,text);
            e->add(err2);
        }
        
        return e;
    }

    [[nodiscard]] // discarding leaks memory
    static LTVEntry* pkg_rating_response(int category) {
        auto* e = new LTVEntry();
        e->container(CL_INIT);
        
        auto* r = new LTVEntry();
        r->container(RQT_RATEURL);
        
        auto* x = new LTVEntry();
        x->set_num(RATEURL_CAT,LTVEntry::num,category);
        r->add(x);
        
        e->add(r);
        
        return e;
    }

    TYPENAME_BASE("SmithServerCX")
private:
    logan_lite log {"com.smithd"};

};


class SmithdProxy : public baseProxy {

public:
    explicit SmithdProxy(baseCom* c) : baseProxy(c) {};
    ~SmithdProxy() override = default;

    std::string to_string(int verbosity) const override {
        return "SmithdProxy|" + baseProxy::to_string(verbosity);
    }

    DECLARE_LOGGING(to_string)

    void on_left_error(baseHostCX*)  override {  state().dead(true); };
    void on_right_error(baseHostCX*) override { state().dead(true); };
    
    void on_left_bytes(baseHostCX* cx) override {
        _inf("Left %d bytes arrived to 0x%x",cx->readbuf()->size(), cx);
    }

private:
    logan_lite log {"com.smithd"};
};


class UxAcceptor : public ThreadedAcceptorProxy<SmithdProxy> {
public:
    UxAcceptor(baseCom* c, int worker_id, proxyType t = proxyType::none() ) :
        ThreadedAcceptorProxy<SmithdProxy>(c,worker_id, t) {};
    
    baseHostCX* new_cx(const char* h, const char* p) override { return new SmithServerCX(com()->slave(),h,p); };
    baseHostCX* new_cx(int s) override { return new SmithServerCX(com()->slave(), s); };
    void on_left_new(baseHostCX* cx) override {
        auto* p = new SmithdProxy(com()->slave());
        p->ladd(cx);
        this->add_proxy(p);
    }
};

typedef ThreadedAcceptor<UxAcceptor> UxProxy;

// running threads and their proxies

static std::vector<std::unique_ptr<UxProxy>> backend_proxies;
std::vector<std::shared_ptr<std::thread>> backend_threads;


// Configuration variables

static std::string config_file;
std::recursive_mutex smithd_cfg_write_lock;
static bool cfg_daemonize = false;
bool cfg_mtrace_enable = false;
static loglevel cfg_log_level = INF;
static int cfg_log_console = false;
static int cfg_smithd_workers = 0;

static std::string cfg_log_file = "/var/log/smithproxy/smithd.%s.log";
static std::string cfg_smithd_listen_port = "/var/run/smithd.%s.sock";

// Tenant configuration
static std::string cfg_tenant_name = "default";

// Various
static std::atomic_int cnt_terminate = 0;
static loglevel  args_debug_flag = NON;
bool config_file_check_only = false;


class SmithD {
    logan_lite log_ = logan_lite("service");
public:
    SmithD() = default;

    static logan_lite& get_log() {
        return instance().log_;
    }

    static SmithD& instance() {
        static SmithD d;
        return d;
    }

    static void my_terminate (int param) {

        auto const& log = get_log();

        if (!cfg_daemonize) {
            _fat("Terminating ...");
            printf("Terminating ...\n");
        }

        for(auto& backend_proxy: backend_proxies) {
            backend_proxy->state().dead(true);
        }

        cnt_terminate++;
        if(cnt_terminate == 3) {
            if (!cfg_daemonize) {
                _err("Failed to terminate gracefully. Next attempt will be enforced.");
                printf("Failed to terminate gracefully. Next attempt will be enforced.\n");
            }
        }
        if(cnt_terminate > 3) {
            if (!cfg_daemonize) {
                _err("Enforced exit.\n");
                printf("Enforced exit.\n");
            }
            abort();
        }
    }

    static void my_usr1 (int param) {

        auto const& log = get_log();

        _dia("USR1 signal handler started");
        _not("reloading policies and its objects !!");
        load_config(config_file,true);
        _dia("USR1 signal handler finished");
    }

};



static struct option long_options[] =
    {
    /* These options set a flag. */
    {"debug",   no_argument,       (int*)&args_debug_flag.level_ref(), iDEB},
    {"diagnose",   no_argument,       (int*)&args_debug_flag.level_ref(), iDIA},

    
    {"config-file", required_argument,  nullptr, 'c'},
    {"config-check-only",no_argument,nullptr,'o'},
    {"daemonize", no_argument, nullptr, 'D'},
    {"version", no_argument, nullptr, 'v'},
    
    // multi-tenancy support: listening ports will be shifted by number 'i', while 't' controls logging, pidfile, etc.
    // both, or none of them have to be set
    {"tenant-name", required_argument, nullptr, 't'},
    {nullptr, 0, nullptr, 0}
};  


bool load_config(std::string& config_f, bool reload) {

    bool ret = true;
    std::lock_guard<std::recursive_mutex> l(smithd_cfg_write_lock);

    auto this_daemon = DaemonFactory::instance();
    auto const& log  = this_daemon->get_log();

    using namespace libconfig;
    Config cfgapi;

    _dia("Reading config file");
    
    // Read the file. If there is an error, report it and exit.
    try {
        cfgapi.readFile(config_f.c_str());

        if (cfgapi.getRoot().exists("settings")) {
            if (load_if_exists(cfgapi.getRoot()["settings"], "log_file", cfg_log_file)) {

                std::string log_target = cfg_log_file;
                log_target = string_format(log_target.c_str(), cfg_tenant_name.c_str());

                //std::cout << "log target: " << log_target << std::endl;

                // prepare custom crashlog file
                std::string crlog = log_target + ".crashlog.log";
                this_daemon->set_crashlog(crlog.c_str());

                auto *o = new std::ofstream(log_target.c_str(), std::ios::app);

                if(chmod(log_target.c_str(), 0600) != 0) { _err("load_config: cannot chmod logfile: %s", string_error().c_str()); }

                Log::get()->targets(log_target, o);
                Log::get()->dup2_cout(false);
                Log::get()->level(cfg_log_level);

                auto lp = std::make_unique<logger_profile>();
                lp->print_srcline_ = Log::get()->print_srcline();
                lp->print_srcline_always_ = Log::get()->print_srcline_always();
                lp->level_ = cfg_log_level;
                Log::get()->target_profiles()[(uint64_t) o] = std::move(lp);

                if (load_if_exists(cfgapi.getRoot()["settings"], "log_console", cfg_log_console)) {
                    Log::get()->dup2_cout(cfg_log_console);
                }
            }

            load_if_exists_atomic(cfgapi.getRoot()["settings"], "log_level",cfg_log_level.level_ref());
        }

    }
    catch(const FileIOException &fioex)
    {
        _err("I/O error while reading config file: %s",config_f.c_str());
        ret = false;   
    }
    catch(const ParseException &pex)
    {
        _err("Parse error in %s at %s:%d - %s", config_f.c_str(), pex.getFile(), pex.getLine(), pex.getError());
        ret = false;
    }

    
    if(! ret ) {
        _fat("Unable to load config.");
        return ret;
    }
    
    try {
        load_if_exists_atomic(cfgapi.getRoot()["settings"], "log_level",cfg_log_level.level_ref());
    }
    catch(const SettingNotFoundException &nfex) {
    
        _fat("Setting not found: %s",nfex.getPath());
        ret = false;
    }
    
    return ret;
}

int smithd_apply_index(std::string& what , const std::string& idx) {

    auto this_daemon = DaemonFactory::instance();
    auto const& log = this_daemon->get_log();

    _deb("apply_index: what=%s idx=%s",what.c_str(),idx.c_str());
    int port = std::stoi(what);
    int index = std::stoi(idx);
    what = std::to_string(port + index);
    
    return 0;
}

bool smithd_apply_tenant_config() {
    int ret = 0;
    
    if(cfg_tenant_name.size() > 0) {
        cfg_smithd_listen_port = string_format(cfg_smithd_listen_port.c_str(),cfg_tenant_name.c_str());
    }
    
    
    return (ret == 0);
}

int main(int argc, char *argv[]) {
    
    auto this_daemon = DaemonFactory::instance();
    auto const& log = this_daemon->get_log();

    this_daemon->pid_file="/var/run/smithd.%s.pid";

    config_file = "/etc/smithproxy/smithd.cfg";
    std::string config_file_tenant = "/etc/smithproxy/smithd.%s.cfg";
    bool custom_config_file = false;
    
    while(true) {
    /* getopt_long stores the option index here. */
        int option_index = 0;
    
        int c = getopt_long (argc, argv, "voDct",
                        long_options, &option_index);
        if (c < 0) break;

        switch(c) {
            case 0:
                break;
                
            case 'c':
                config_file = std::string(optarg);    
                custom_config_file = true;
                break;      
                
            case 'o':
                config_file_check_only = true;
                break;
                
            case 'D':
                cfg_daemonize = true;
                break;
                
            case 'v':
                std::cout << SMITH_VERSION << "+" << SOCLE_VERSION << std::endl;
                exit(0);

            case 't':
                cfg_tenant_name = std::string(optarg);
                break;                
                
            default:
                std::cerr << "unknown option: '" << (char)c << "'";
                exit(1);                 
        }
    }

    Log::init();
    Log::set(std::make_shared<QueueLogger>());
    
    if(!cfg_daemonize) {
        std::thread* log_thread  = create_log_writer();
        if(log_thread != nullptr) {
            pthread_setname_np(log_thread->native_handle(),"sxd_lwr");
        }    
    }

    Log::get()->level(DEB);
    
    if(not cfg_tenant_name.empty()) {
        _war("Starting tenant: '%s'",cfg_tenant_name.c_str());
        this_daemon->set_tenant("smithd", cfg_tenant_name);
    }
    else {
        _war("Starting tenant: 0 (default)");
        this_daemon->set_tenant("smithd", "default");
    }
    
    
    // if logging set in cmd line, use it 
    if(args_debug_flag > NON) {
        Log::get()->level(args_debug_flag);
    }
    
    if(! custom_config_file) {
        // look for tenant config (no override set)
        
        std::string tenant_cfg = string_format(config_file_tenant.c_str(),cfg_tenant_name.c_str());
        
        _dia("tenant config: %s", tenant_cfg.c_str());
        
        struct stat s;
        if (stat(tenant_cfg.c_str(),&s) == 0) {
            _war("Tenant config: %s",tenant_cfg.c_str());
            config_file = tenant_cfg;
        } else {
            _war("Tenant config %s not found. Using default.",tenant_cfg.c_str());
        }
    }

    if(!smithd_apply_tenant_config()) {
        _fat("Failed to apply tenant specific configuration!");
        exit(2);
    }

    _dia("loading config from %s", config_file.c_str());
    
    if (!load_config(config_file)) {
        if(config_file_check_only) {
            _err("Config check: error loading config file.");
            exit(1);
        }
        else {
            _err("Error loading config file on startup.");
            std::cerr << "error loading config" << std::endl;
            exit(1);
        }
    }

    if(config_file_check_only) {
        _dia("Exiting, asked to check config file only.");
        exit(0);
    }

#ifdef MEM_DEBUG
    if(cfg_mtrace_enable) {
        putenv("MALLOC_TRACE=/var/log/smithd_mtrace.log");
        mtrace();
    }
#endif

    if(cfg_daemonize) {
        if(Log::get()->targets().size() <= 0) {
            _fat("Cannot daemonize without logging to file.");
            exit(-5);
        }

        Log::get()->dup2_cout(false);
        _inf("entering service mode");
        int dem = this_daemon->daemonize();

        if(dem == 0) {
            // master - to exit
            exit(0);
        } else if (dem < 0) {
            exit(-1);
        }
    } else {
        // also when non-daemon, write out PID file
        this_daemon->write_pidfile();
    }

    #ifndef USE_OPENSSL300
    CRYPTO_mem_ctrl(CRYPTO_MEM_CHECK_ON);
    #endif

    std::string friendly_thread_name_smithd = "sxy_smithd";

    // no mercy here.
    unlink(cfg_smithd_listen_port.c_str());

    try {
        std::string path = cfg_smithd_listen_port;
        if( path.empty() ) {
            path = "/var/run/smithd.sock";
        }

        backend_proxies = NetworkServiceFactory::prepare_listener<UxProxy,UxCom,const char*>(
                path.c_str(),
                "ux-plain",
                cfg_smithd_workers,
                proxyType::none());
    }
    catch(socle::com_error const& e) {
        _fat("Exception caught when creating listener: %s", e.what());
    }
    catch(sx::netservice_cannot_bind const& e) {
        _fat("Exception caught when creating listener: %s", e.what());
    }
    catch(sx::netservice_error const& e) {
        _fat("Exception caught when creating listener: %s", e.what());
    }
    catch(mempool_error const& e) {
        _fat("Exception caught when creating listener: %s", e.what());
    }


    if(backend_proxies.empty() && cfg_smithd_workers >= 0) {
        
        _fat("Failed to setup proxies. Bailing!");
        exit(-1);
    }

    DaemonFactory::set_daemon_signals(SmithD::my_terminate, SmithD::my_usr1);
    
    for(auto& backend_proxy: backend_proxies) {
        _inf("Starting smithd listener");
        auto backend_thread = std::make_shared<std::thread>(std::thread([&backend_proxy]() {

            auto this_daemon = DaemonFactory::instance();
            auto const& log = this_daemon->get_log();

            DaemonFactory::set_daemon_signals(SmithD::my_terminate, SmithD::my_usr1);
            _dia("smithd: max file descriptors: %d", this_daemon->get_limit_fd());
            
            backend_proxy->run(); 
            _dia("smithd workers torn down.");
            backend_proxy->shutdown(); 
        } ));

        pthread_setname_np(backend_thread->native_handle(),string_format("smithd_%s",cfg_tenant_name.c_str()).c_str());
        backend_threads.push_back(backend_thread);
    }
    
    
    _cri("Smithd backend (smithproxy %s, socle %s)  started",SMITH_VERSION,SOCLE_VERSION);
    
    pthread_setname_np(pthread_self(),"smithd");
    
    if(!backend_threads.empty()) {

        for(auto th: backend_threads) {
            th->join();
        }
    }

    // cleanup
    unlink(cfg_smithd_listen_port.c_str());
}

 
