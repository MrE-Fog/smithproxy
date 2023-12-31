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

#ifndef SMITHPROXY_AUTHFACTORY_HPP
#define SMITHPROXY_AUTHFACTORY_HPP

#include <string>
#include <mutex>
#include <shm/shmauth.hpp>

#include <log/logger.hpp>

class baseHostCX;

struct AuthFactoryOptions {
    // publicly accessible config values
    unsigned int token_timeout = 20; // token expires _from_cache_ after this timeout (in seconds).
    int global_idle_timeout = 600;
    std::string portal_address = "0.0.0.0";
    std::string portal_address6 = "[::]";
    std::string portal_port_http = "8008";
    std::string portal_port_https = "8043";
};

class AuthFactory {

    using shared_token_table_t = shared_table<shm_logon_token>;
    using token_map_t = std::unordered_map<std::string,std::pair<unsigned int,std::string>>;

    using shared_ip4_map_t = shared_logoninfotype_ntoa_map<shm_logon_info,4>;
    using shared_ip6_map_t = shared_logoninfotype_ntoa_map<shm_logon_info6,16>;

    using ip6_map_t = std::unordered_map<std::string,IdentityInfo6>;
    using ip4_map_t = std::unordered_map<std::string,IdentityInfo> ;

    mutable std::recursive_mutex token_lock_;
    mutable std::recursive_mutex ip4_lock_;
    mutable std::recursive_mutex ip6_lock_;

    AuthFactory() = default;
    virtual ~AuthFactory() = default;

public:

    AuthFactory& operator=(AuthFactory const&) = delete;
    AuthFactory(AuthFactory const&) = delete;

    static AuthFactory& get() {
        static AuthFactory a;
        return a;
    };

    AuthFactoryOptions options;

    inline std::recursive_mutex& token_lock () const { return token_lock_; };
    inline std::recursive_mutex& ip4_lock () const { return ip4_lock_; };
    inline std::recursive_mutex& ip6_lock () const { return ip6_lock_; };

    static std::recursive_mutex& get_token_lock () { return get().token_lock(); };
    static std::recursive_mutex& get_ip4_lock () { return get().ip4_lock(); };
    static std::recursive_mutex& get_ip6_lock () { return get().ip6_lock(); };

    ip4_map_t ip4_map_;
    shared_ip4_map_t  shm_ip4_map;

    static ip4_map_t& get_ip4_map() { return get().ip4_map_; };

    ip6_map_t ip6_map_;
    shared_ip6_map_t shm_ip6_map;

    static ip6_map_t& get_ip6_map() { return get().ip6_map_; };

    token_map_t token_map_;
    shared_token_table_t shm_token_map_;

    static token_map_t& get_token_map() { return get().token_map_; };


    std::optional<std::vector<std::string>> ip4_get_groups(std::string const& host);
    std::optional<std::vector<std::string>> ip6_get_groups(std::string const& host);

    // refresh from shared memory
    int shm_token_table_refresh ();
    int shm_ip6_table_refresh ();
    int shm_ip4_table_refresh ();


    void ip4_timeout_check ();
    void ip6_timeout_check ();

    IdentityInfo *ip4_get (std::string &);
    IdentityInfo6 *ip6_get (std::string &);

    bool ip4_inc_counters (const std::string &host, unsigned int rx, unsigned int tx);
    bool ip6_inc_counters (const std::string &host, unsigned int rx, unsigned int tx);

    // lookup by ip -> returns pointer IN the auth_ip_map
    void ip4_remove (const std::string &host);
    void ip6_remove (const std::string &ip6_address);

    bool ipX_inc_counters (baseHostCX *cx, unsigned int rx, unsigned int tx);
    bool ipX_inc_counters (baseHostCX *cx);


    std::string to_string([[maybe_unused]] int verbosity) const { return "AuthFactory"; };

    TYPENAME_BASE("AuthFactory")
    DECLARE_LOGGING(to_string)

    static logan_lite& get_log() {
        static logan_lite l("auth");
        return l;
    }

    logan_lite& log = get_log();
};

#endif //SMITHPROXY_AUTHFACTORY_HPP
