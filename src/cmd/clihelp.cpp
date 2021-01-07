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
#include <algorithm>

#include <ext/libcli/libcli.h>

#include <cmd/clihelp.hpp>
#include <cmd/cligen.hpp>
#include <common/log/logan.hpp>
#include <cfgapi.hpp>


// @brief  - return true for numbers in the closed range
// @param template A integer range1
// @param template B interer range2
// @param v - string to check
// @note  template lambdas supported since C++20


CliElement::value_filter_retval VALUE_UINT_RANGE_GEN(std::function<int()> callableA, std::function<int()> callableB, std::string const& v) {

    auto [ may_val, descr ] = CliElement::VALUE_UINT(v);

    int intA = callableA();
    int intB = callableB();

    auto err = string_format("value must be a non-negative number in range <%d,%d>", intA, intB);

    if(may_val.has_value()) {
        int port_value = std::any_cast<int>(may_val);

        if(port_value < intA or port_value > intB)
            return std::make_pair(std::any(), err);
        else
            return { may_val, "" };
    }

    return { may_val, err };

}

template <int A, int B>
CliElement::value_filter_retval VALUE_UINT_RANGE(std::string const& v) {

    auto a = []() { return A; };
    auto b = []() { return B; };

    return VALUE_UINT_RANGE_GEN(a, b, v);
};


void CliHelp::init() {

    add("default", "")
    .help_quick("enter <value>");


    add("settings.certs_path", "directory for TLS-resigning CA certificate and key")
            .help_quick("<string>: (default: /etc/smithproxy/certs/default)")
            .may_be_empty(false)
            .value_filter(CliElement::VALUE_DIR);

    add("settings.certs_ctlog", "file containing certificate transparency log list")
            .help_quick("<string>: file with certificate transparency keys (default: ct_log_list.cnf)")
            .value_filter(CliElement::VALUE_FILE);

    add("settings.certs_ca_key_password", "TLS-resigning CA private key protection password")
            .help_quick("<string>: enter string value");

    add("settings.ca_bundle_path", "trusted CA store path (to verify server-side connections)")
            .help_quick("<string>: enter valid path")
            .may_be_empty(false)
            .value_filter(CliElement::VALUE_DIR);

    // listening ports

    add("settings.plaintext_port", "base divert port for non-SSL TCP traffic")
            .help_quick("<number>: a high port number")
            .may_be_empty(false)
            .value_filter(VALUE_UINT_RANGE<1024, 65535>);

    add("settings.ssl_port", "base divert port for SSL TCP traffic")
            .help_quick("<number>: a high port number")
            .may_be_empty(false)
            .value_filter(VALUE_UINT_RANGE<1024, 65535>);

    add("settings.udp_port", "base divert port for non-DTLS UDP traffic")
            .help_quick("<number>: a high port number")
            .may_be_empty(false)
            .value_filter(VALUE_UINT_RANGE<1024, 65535>);

    add("settings.dtls_port", "base divert port for DTLS UDP traffic")
            .help_quick("<number>: a high port number")
            .may_be_empty(false)
            .value_filter(VALUE_UINT_RANGE<1024, 65535>);

    add("settings.socks_port", "base SOCKS proxy listening port")
            .help_quick("<number>: a high port number")
            .may_be_empty(false)
            .value_filter(VALUE_UINT_RANGE<1024, 65535>);


    // worker setup

    add("settings.accept_tproxy", "whether to accept incoming connections via TPROXY")
            .help_quick("<bool>: set to 'true' to disable tproxy acceptor (default: false)")
            .may_be_empty(false)
            .value_filter(CliElement::VALUE_BOOL);

    add("settings.accept_redirect", "whether to accept incoming connections via REDIRECT")
            .help_quick("<bool>: set to 'true' to disable redirect acceptor (default: false)")
            .may_be_empty(false)
            .value_filter(CliElement::VALUE_BOOL);

    add("settings.accept_socks", "whether to accept incoming connections via SOCKS")
            .help_quick("<bool>: set to 'true' to disable socks acceptor (default: false)")
            .may_be_empty(false)
            .value_filter(CliElement::VALUE_BOOL);

    //


    auto VALUE_ZERO = []() -> int { return 0; };
    auto HW_THREADS = []() -> int { return static_cast<int>(std::thread::hardware_concurrency()); };
    auto HW_FILTER = [&] (std::string const& v) {
        return VALUE_UINT_RANGE_GEN(VALUE_ZERO, HW_THREADS, v);
    };

    add("settings.plaintext_workers", "non-SSL TCP traffic worker thread count")
            .help_quick("<number> acceptor subordinate worker threads count (max 4xCPU)")
            .may_be_empty(false)
            .value_filter(HW_FILTER);


    add("settings.ssl_workers", "SSL TCP traffic worker thread count")
            .help_quick("<number> acceptor subordinate worker threads count (max 4xCPU)")
            .may_be_empty(false)
            .value_filter(HW_FILTER);

    add("settings.udp_workers", "non-DTLS traffic worker thread count")
            .help_quick("<number> acceptor subordinate worker threads count (max 4xCPU)")
            .may_be_empty(false)
            .value_filter(HW_FILTER);


    add("settings.dtls_workers", "DTLS traffic worker thread count")
            .help_quick("<number> acceptor subordinate worker threads count (max 4xCPU)")
            .may_be_empty(false)
            .value_filter(HW_FILTER);

    add("settings.socks_workers", "SOCKS proxy traffic thread count")
            .help_quick("<number> acceptor subordinate worker threads count (max 4xCPU)")
            .may_be_empty(false)
            .value_filter(HW_FILTER);




    add("settings.ssl_autodetect", "Detect TLS ClientHello on unusual ports")
            .help_quick("<bool> set true to wait a short moment for TLS ClientHello on plaintext ports")
            .may_be_empty(false)
            .value_filter(CliElement::VALUE_BOOL);

    add("settings.ssl_autodetect_harder", "Detect TSL ClientHello on unusual ports - wait a bit longer")
            .help_quick("<bool> set true to wait a bit longer for TLS ClientHello on plaintext ports")
            .may_be_empty(false)
            .value_filter(CliElement::VALUE_BOOL);


    add("settings.ssl_ocsp_status_ttl", "obsoleted - hardcoded TTL for OCSP response validity")
            .may_be_empty(false)
            .value_filter(CliElement::VALUE_NONE);


    add("settings.ssl_crl_status_ttl", "obsoleted - hardcoded TTL for downloaded CRL files")
            .may_be_empty(false)
            .value_filter(CliElement::VALUE_NONE);


    add("settings.log_level", "default file logging verbosity level")
            .help_quick("<number> between 0 and 8 to highest verbosity. Debug level is set by topics in CLI")
            .may_be_empty(false)
            .value_filter(VALUE_UINT_RANGE<0,8>);

    add("settings.log_file", "log file")
            .help_quick("<filename template> file for logging. Must include '%%s' for tenant name expansion.")
            .may_be_empty(false)
            .value_filter(CliElement::VALUE_BASEDIR);

    add("settings.log_console", "toggle logging to standard output");
    add("settings.syslog_server", "IP address of syslog server");
    add("settings.syslog_port", "syslog server port");

    add("settings.syslog_facility", "syslog facility");
    help_quick("settings.syslog_facility", "syslog facility (default 23 = local7)");

    add("settings.syslog_level", "syslog logging verbosity level");
    help_quick("settings.syslog_level", "syslog level (default 6 = informational)");

    add("settings.syslog_family", "IPv4 or IPv6?");
    help_quick("settings.syslog_family", "set to 4 or 6 for ip version");

    add("settings.sslkeylog_file", "where to dump TLS keying material");
    help_quick("settings.sslkeylog_file", "file path where to dump tls keys (if set)");

    add("settings.messages_dir", "replacement text directory");
    help_quick("settings.messages_dir", "directory path to message files");

    add("settings.write_payload_dir", "root directory for packet dumps");
    help_quick("settings.write_payload_dir", "directory path for payload dump files");

    add("settings.write_payload_file_prefix", "packet dumps file prefix");
    help_quick("settings.write_payload_file_prefix", "dump filename prefix");

    add("settings.write_payload_file_suffix", "packet dumps file suffix");
    help_quick("settings.write_payload_file_suffix", "dump filename suffix");

    add("settings.auth_portal", "** configure authentication portal settings");
    help_quick("settings.auth_portal", "");

    add("settings.cli", "** configure CLI specific settings");
    help_quick("settings.cli", "");

    add("settings.socks", "** configure SOCKS specific settings");
    help_quick("settings.socks", "");




    add("debug.log_data_crc", "calculate received CRC data (helps to identify proxy bugs)");

    add("proto_objects", "IP protocols")
        .help_quick("list of protocol objects");

    add("proto_objects.[x].id", "IP protocol number (tcp=6, udp=17)")
        .help_quick("<number> protocol id (1-255)")
        .may_be_empty(false)
        .value_filter(VALUE_UINT_RANGE<1,255>);


    add("port_objects", "TCP/UDP ports");
    add("port_objects[x].start", "port range start");
    add("port_objects[x].end", "port range end");

    add("policy.[x].proto", "protocol to match (see proto_objects)")
        .may_be_empty(false);

}



bool CliHelp::value_check(std::string const& varname, std::string const& v, cli_def* cli) {

    std::regex match ("\\[[0-9]+\\]");
    std::string masked_varname  = std::regex_replace (varname, match, "[x]");

    _debug(cli, "value_check: varname = %s, value = %s", varname.c_str(), v.c_str());
    _debug(cli, "value_check:  masked = %s, value = %s", masked_varname.c_str(), v.c_str());

    auto cli_e = find(masked_varname);
    bool may_be_empty = true;

    struct filter_result_e {
        filter_result_e(bool b, std::string const& v) : value_filter_check(b), value_filter_check_response(v) {};
        bool value_filter_check = true;
        std::string value_filter_check_response;
    };
    std::list<filter_result_e> filter_result;

    if(cli_e.has_value()) {
        may_be_empty = cli_e->get().may_be_empty();

        if(not v.empty()) {

            unsigned int i = 0;
            for(auto this_filter: cli_e->get().value_filter()) {
                auto[ret, msg] = std::invoke(this_filter, v);

                filter_result.emplace_back(ret.has_value(), msg);
                _debug(cli, " CliElement value filter check[%u] : %d : '%s'", i, ret.has_value(), msg.c_str());

                i++;
            }

        }
    }


    // empty value check
    if(v.empty() and not may_be_empty) {

        _debug(cli, "this attribute cannot be empty");

        cli_print(cli," ");
        cli_print(cli, "Value check failed: cannot be set with empty value");

        return false;
    }


    for(auto const& fr: filter_result) {
        if(not fr.value_filter_check) {

            cli_print(cli," ");
            cli_print(cli, "Value check failed: %s", fr.value_filter_check_response.c_str());
            return false;
        }
    }



    auto path_elems = string_split(masked_varname, '.');
    try {
        if (masked_varname.find("policy.[x]") == 0) {

            _debug(cli, "policy values check");

            // check policy
            if(path_elems[2] == "src" || path_elems[2] == "dst") {

                _debug(cli, "policy values for %s", path_elems[2].c_str());

                auto addrlist = CfgFactory::get().keys_of_db_address();
                if(std::find(addrlist.begin(), addrlist.end(), v) == addrlist.end()) {
                    _debug(cli, "policy values for %s: %s not found address db", path_elems[2].c_str(), v.c_str());
                    return false;
                }
            }
        }
        else {
        _debug(cli, "value_check: no specific check procedure programmed");
        }
    }
    catch(std::out_of_range const& e) {
        _debug(cli, "value_check: returning FAILED: out of range");
        return false;
    }

    _debug(cli, "value_check: returning OK");
    return true;
}


std::string CliHelp::help(help_type_t htype, const std::string& section, const std::string& key) {

    std::regex match ("\\[[0-9]+\\]");
    std::string masked_section  = std::regex_replace (section, match, "[x]");

    auto what = masked_section + "." + key;
    auto cli_e = find(what);

    if(not cli_e.has_value()) {
        std::regex remove_last_part("\\.[^.]+$");
        masked_section = std::regex_replace(section, remove_last_part, ".[x]");
        cli_e = find(masked_section + "." + key);
    }


    if(cli_e.has_value()) {
        if (htype == CliHelp::help_type_t::HELP_QMARK) {
            return cli_e->get().help_quick();
        } else {
            return cli_e->get().help();
        }
    }

    return std::string();
}