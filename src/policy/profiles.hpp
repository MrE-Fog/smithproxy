//
// Created by astib on 13.12.20.
//

#ifndef SMITHPROXY_PROFILES_HPP
#define SMITHPROXY_PROFILES_HPP

#include <policy/cfgelement.hpp>
#include <policy/addrobj.hpp>

class ProfileDetection : public socle::sobject, public CfgElement {

public:
    /*
     *  0   MODE_NONE
     *  1   MODE_POST -- works in all scenarios, but sometimes we can read data, which should
     *                   have been processed by upgraded com. Use MODE_PRE if possible.
     *  2   MODE_PRE  -- should be default, but not safe when cannot peek()
     */
    int mode = 0;

    bool ask_destroy() override { return false; };
    std::string to_string(int verbosity) const override {
        return string_format("ProfileDetection: name=%s mode=%d", element_name().c_str(), mode);
    };

TYPENAME_OVERRIDE("ProfileDetection")
};

class ProfileContentRule : public socle::sobject, public CfgElement {

public:
    std::string match;
    std::string replace;
    bool fill_length = false;
    int replace_each_nth = 0;
    int replace_each_counter_ = 0;

    bool ask_destroy() override { return false; };
    std::string to_string(int verbosity) const override {
        return string_format("ProfileContentRule: matching %s", ESC_(match).c_str());
    }

TYPENAME_OVERRIDE("ProfileContentRule")
};

struct ContentCaptureFormat {
    enum class write_format_type_t { SMCAP, PCAP_SINGLE, PCAP };
    using type_t = write_format_type_t;

    type_t value;

    using map_t = std::initializer_list<std::pair<type_t, const char*>>;
    static const inline map_t map_ = { { type_t::SMCAP, "smcap" },
                                       { type_t::PCAP, "pcap" },
                                       { type_t::PCAP_SINGLE, "pcap_single" }
                               };

    ContentCaptureFormat() : value(type_t::SMCAP) {}
    ContentCaptureFormat(std::string const& v) : value(from_str(v)) {};
    ContentCaptureFormat(type_t v) : value(v) {};

    static std::string to_str(write_format_type_t t)  {
        std::string to_ret;
        for(auto const& r: map_) {
            if(r.first == t) {
                to_ret  = r.second;
                break;
            }
        }
        return to_ret;
    }
    static write_format_type_t from_str(std::string const& write_format) {
        type_t to_ret = write_format_type_t::SMCAP;

        for(auto const& r: map_) {
            if(r.second == write_format) {
                to_ret  = r.first;
                break;
            }
        }
        return to_ret;

    }

    std::string to_str() const { return to_str(value); }
};

class ProfileContent  : public socle::sobject, public CfgElement {
public:
    // if true, content of proxy transmission will dumped to file
    bool write_payload = false;

    ContentCaptureFormat write_format;
    std::vector<ProfileContentRule> content_rules;

    bool ask_destroy() override { return false; };
    std::string to_string(int verbosity) const override {
        std::string ret = string_format("ProfileContent: name=%s capture=%d", element_name().c_str(), write_payload);
        if(verbosity > INF) {
            for(auto const& it: content_rules)
                ret += string_format("\n        match: '%s'", ESC_(it.match).c_str());
        }

        return ret;
    };

TYPENAME_OVERRIDE("ProfileContent")
};


class FqdnAddress;
class CidrAddress;

class ProfileTls : public socle::sobject, public CfgElement  {
public:
    bool inspect = false;
    bool allow_untrusted_issuers = false;
    bool allow_invalid_certs = false;
    bool allow_self_signed = false;
    bool failed_certcheck_replacement = true;           //instead of resetting connection, spoof and display human-readable explanation why connection failed
    bool failed_certcheck_override = false;             //failed ssl replacement will contain option to temporarily allow the connection for the source IP.
    int  failed_certcheck_override_timeout = 600;       // if failed ssl override is active, this is the timeout.
    int  failed_certcheck_override_timeout_type = 0;    // 0 - just expire after the timeout
    // 1 - reset timeout on traffic (aka idle timer)

    bool use_pfs = true;         // general switch, more concrete take precedence
    bool left_use_pfs = true;
    bool right_use_pfs = true;
    bool left_disable_reuse = false;
    bool right_disable_reuse = false;

    int ocsp_mode = 0;           // 0 = disable OCSP checks ; 1 = check only end certificate ; 2 = check all certificates
    bool ocsp_stapling = false;
    int  ocsp_stapling_mode = 0; // 0 = loose, 1 = strict, 2 = require

    bool opt_ct_enable = true;
    bool opt_alpn_block = false;

    std::shared_ptr<std::vector<std::string>> sni_filter_bypass;
    std::shared_ptr<std::vector<FqdnAddress>> sni_filter_bypass_addrobj;
    socle::spointer_set_int redirect_warning_ports;

    bool sni_filter_use_dns_cache = true;       // if sni_filter_bypass is set, check during policy match if target IP isn't in DNS cache matching SNI filter entries.
    // For example:
    // Connection to 1.1.1.1 policy check will look in all SNI filter entries ["abc.com","mybank.com"] and will try to find them in DNS cache.
    // Sni filter entry mybank.com is found in DNS cache pointing to 1.1.1.1. Connection is bypassed.
    // Load increases with SNI filter length lineary, but DNS cache lookup is fast.
    // DNS cache has to be active this to be working.
    bool sni_filter_use_dns_domain_tree = true;
    // check IP address in full domain tree for each SNI filter entry.
    // if SNI filter entry can't be found in DNS cache, try to look in all DNS subdomains of SNI filter entries.
    // Example:
    // Consider SNI filter from previous example. You are now connecting to ip 2.2.2.2.
    // Based on previous DNS traffic, there is subdomain cache for "mybank.com" filled with entries "www" and "ecom".
    // Both "www" and "ecom" are searched in DNS cache. www points to 1.1.1.1, but ecom points to 2.2.2.2.
    // Connection is bypassed.
    // DNS cache has to active and sni_filter_use_dns_cache enabled before this feature can be activated.
    // Load increases with SNI filter size and subdomain cache, both lineary, so it's intensive feature.

    bool sslkeylog = false;                     // disable or enable ssl keylogging on this profile


    bool ask_destroy() override { return false; };
    std::string to_string(int verbosity) const override {
        std::string ret = string_format("ProfileTls: name=%s inspect=%d ocsp=%d ocsp_stap=%d pfs=%d,%d abr=%d,%d",
                                        element_name().c_str(),
                                        inspect,
                                        ocsp_mode, ocsp_stapling_mode,
                                        left_use_pfs, right_use_pfs,
                                        !left_disable_reuse, !right_disable_reuse);
        if(verbosity > INF) {

            ret += string_format("\n        allow untrusted issuers: %d", allow_untrusted_issuers);
            ret += string_format("\n        allow invalid certs: %d", allow_invalid_certs);
            ret += string_format("\n        allow self-signed certs: %d", allow_self_signed);
            ret += string_format("\n        failed cert check html warnings: %d", failed_certcheck_replacement);
            ret += string_format("\n        failed cert check allow user override: %d", failed_certcheck_override);
            ret += string_format("\n        failed cert check user override timeout: %d", failed_certcheck_override_timeout);
            ret += string_format("\n        failed cert check user override timeout type: %d", failed_certcheck_override_timeout_type);

            bool sni_out = false;
            if(sni_filter_bypass)
                for(auto const& it: *sni_filter_bypass) {
                    sni_out = true;
                    ret += string_format("\n        sni exclude: '%s'",ESC_(it).c_str());
                }

            if(sni_out) {
                ret += string_format("\n        sni exclude - use dns cache: %d",sni_filter_use_dns_cache);
                ret += string_format("\n        sni exclude - use dns domain tree: %d",sni_filter_use_dns_domain_tree);
                ret += "\n";
            }

            if(redirect_warning_ports.ptr())
                for(auto it: *redirect_warning_ports.ptr())
                    ret += string_format("\n        html warning port: '%d'",it);

        }

        return ret;
    }

TYPENAME_OVERRIDE("ProfileTls")
};

struct ProfileAuth;
struct ProfileAlgDns;
struct ProfileScript;
struct ProfileRouting;


struct ProfileList {
    std::shared_ptr<ProfileContent> profile_content = nullptr;
    std::shared_ptr<ProfileDetection> profile_detection = nullptr;
    std::shared_ptr<ProfileTls> profile_tls = nullptr;
    std::shared_ptr<ProfileAuth> profile_auth = nullptr;
    std::shared_ptr<ProfileAlgDns> profile_alg_dns = nullptr;
    std::shared_ptr<ProfileScript> profile_script = nullptr;
    std::shared_ptr<ProfileRouting> profile_routing = nullptr;
};
struct ProfileSubAuth : public ProfileList, public CfgElement {
};

struct ProfileAuth : public CfgElement {
    bool authenticate = false;
    bool resolve = false;  // resolve traffic by ip in auth table
    std::vector<std::shared_ptr<ProfileSubAuth>> sub_policies;
};

struct ProfileAlgDns : public CfgElement {
    bool match_request_id = false;
    bool randomize_id = false;
    bool cached_responses = false;
};

struct ProfileScript : public CfgElement {
    std::string module_path;

    using script_t = enum { ST_PYTHON = 0, ST_GOLANG = 1};
    int script_type = -1;
};

struct ProfileRouting: public CfgElement {
    std::vector<std::string> dnat_addresses;
    std::vector<std::string> dnat_ports;

    using dnat_lb_method_t = enum class lb_method { LB_RR, LB_L3, LB_L4 };
    dnat_lb_method_t dnat_lb_method;

    // update internal state - run once per one request
    inline void update() { lb_state.expand_candidates(dnat_addresses); lb_state.rr_counter++; }

    // get (cached) address lookup candidates
    inline auto lb_candidates(int family) const { return family == CIDR_IPV6 ? lb_state.candidates_v6 : lb_state.candidates_v4; }

    // helper to get index based on RR scheme
    [[nodiscard]] inline size_t lb_index_rr(size_t sz) const { return sz == 0 ? 0 : lb_state.rr_counter % sz; }

    struct LbState {
        constexpr static unsigned int refresh_interval = 5;

        std::mutex lock_;

        std::atomic_long rr_counter = 0;

        time_t last_refresh = 0;
        std::vector<std::shared_ptr<CidrAddress>> candidates_v4;
        std::vector<std::shared_ptr<CidrAddress>> candidates_v6;
        bool expand_candidates(std::vector<std::string> const& addresses);

        // update internal counters, should be run once per request to have consistent results

    };

    LbState lb_state;
};


#endif //SMITHPROXY_PROFILES_HPP
