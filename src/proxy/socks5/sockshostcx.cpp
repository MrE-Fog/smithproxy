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

#include <service/cfgapi/cfgapi.hpp>
#include <log/logger.hpp>
#include <proxy/socks5/sockshostcx.hpp>
#include <inspect/dnsinspector.hpp>


bool socksServerCX::global_async_dns = true;

socksServerCX::socksServerCX(baseCom* c, unsigned int s) : baseHostCX(c,s) {
    state_ = socks5_state::INIT;

    // copy setting from global/static variable - don't allow to change async
    // flag on the background during the object life
    async_dns = global_async_dns;
}

std::size_t socksServerCX::process_in() {
    switch(state_) {
        case socks5_state::INIT:
            return process_socks_hello();
        case socks5_state::HELLO_SENT:
            return 0; // we sent response to client hello, don't process anything
        case socks5_state::WAIT_REQUEST:
            return process_socks_request();
        default:
            break;
    }
    
    return 0;
}

int socksServerCX::process_socks_hello() {
        
    buffer* b = readbuf();
    if(b->size() < 3) {
        // minimal size of "client hello" is 3 bytes
        return 0;
    }
                   version = b->get_at<unsigned char>(0);
    unsigned char nmethods = b->get_at<unsigned char>(1);
    
    if(b->size() < (unsigned int)(2 + nmethods)) {
        return 0;
    }
    
    // at this stage we have full client hello received
    if(version == 5) {
        _dia("socksServerCX::process_socks_init: version %d", version);
        
        unsigned char server_hello[2];
        server_hello[0] = 5; // version
        server_hello[1] = 0; // no authentication
        
        writebuf()->append(server_hello,2);
        state_ = socks5_state::HELLO_SENT;
        state_ = socks5_state::WAIT_REQUEST;
        
        // flush all data, assuming 
        return b->size();
    }
    else if(version == 4) {
        _dia("socksServerCX::process_socks_init: version %d", version);
        return process_socks_request();
    } else {
        _dia("socksServerCX::process_socks_init: unsupported socks version");
        error(true);
    }

    return 0;
}

bool socksServerCX::choose_server_ip(std::vector<std::string>& target_ips) {

    if(target_ips.empty()) {
        _dia("choose_server_ip: empty");
        return false;
    }

    uint64_t index = 0;

    if(target_ips.size() > 1) {
        //use some semi-random target
        uint64_t baz = (uint64_t)this * (uint64_t)com() * time(nullptr);
        index = baz % target_ips.size();
    }

    std::string target = target_ips.at(index);

    _dia("choose_server_ip: chosen target: %s (index %d out of size %d)",target.c_str(), index, target_ips.size());
    com()->nonlocal_dst_host() = target;
    com()->nonlocal_dst_resolved(true);

    return true;
}

bool socksServerCX::process_dns_response(std::shared_ptr<DNS_Response> resp) {

    std::vector<std::string> target_ips;
    bool ret = true;

    if (resp) {
        for (auto const& a: resp->answers()) {
            std::string a_ip = a.ip(false);
            if (! a_ip.empty()) {
                _dia("process_dns_response: target candidate: %s", a_ip.c_str());
                target_ips.push_back(a_ip);
            }
        }

        if (! target_ips.empty()) {

            DNS_Inspector di;
            di.store(resp);
        }
    }

    if(!target_ips.empty() && choose_server_ip(target_ips)) {
        setup_target();
        _dia("process_dns_response: waiting for policy check");

    } else {
        _dia("process_dns_response: unable to find destination address for the request");
        ret = false;
    }

    return ret;
}


std::string socksServerCX::choose_dns_server() const {

    std::string nameserver = "1.1.1.1";
    if (!CfgFactory::get()->db_nameservers.empty()) {
        return CfgFactory::get()->db_nameservers.at(0);
    }

    return nameserver;
}

void socksServerCX::setup_dns_async(std::string const& fqdn, DNS_Record_Type type, std::string const& nameserver) {
    int dns_sock = DNSFactory::get().send_dns_request(fqdn, type, nameserver);
    if (dns_sock) {
        _dia("dns request sent: %s", fqdn.c_str());

        using std::placeholders::_1;
        async_dns_query = std::make_unique<AsyncDnsQuery>(this,
                                            std::bind(&socksServerCX::dns_response_callback, this,
                                                      _1));

        switch (type) {
            case AAAA:
                tested_dns_aaaa = true;
                break;
            case A:
                [[fallthrough]];
            default:
                tested_dns_a = true;
        }

        async_dns_query->tap(dns_sock);
        state_ = socks5_state::DNS_QUERY_SENT;
    } else {
        _err("failed to send dns request: %s", fqdn.c_str());
        error(true);
    }
}


int socksServerCX::process_socks_request() {

    socks5_request_error e = socks5_request_error::NONE;
    
    if(readbuf()->size() < 5) {
        return 0; // wait for more complete request
    }

    _dia("socksServerCX::process_socks_request");

    if(state_ == socks5_state::DNS_QUERY_SENT) {
        _dia("process_socks_request: triggered when waiting for DNS response");
        return 0;
    }

    _dum("Request dump:\r\n%s",hex_dump(readbuf()->data(),readbuf()->size(), 4, 0, true).c_str());
    
              version = readbuf()->get_at<unsigned char>(0);
    [[maybe_unused]] auto cmd = readbuf()->get_at<unsigned char>(1);
    //@2 is reserved
    
    if(version < 4 or version > 5) {
        e = socks5_request_error::UNSUPPORTED_VERSION;
        goto error;
    }
    
       
    if(version == 5) {

        if(readbuf()->size() < 10) {
            _dia("process_socks_request: socks5 request header too short");
            goto error;
        }

        auto atype   = static_cast<socks5_atype>(readbuf()->get_at<unsigned char>(3));

        if(atype != socks5_atype::IPV4 && atype != socks5_atype::FQDN) {
            e = socks5_request_error::UNSUPPORTED_ATYPE;
            goto error;
        }

        if(atype == socks5_atype::FQDN) {
            req_atype = socks5_atype::FQDN;
            state_ = socks5_state::REQ_RECEIVED;

            auto fqdn_sz = readbuf()->get_at<unsigned char>(4);
            if((unsigned int)fqdn_sz + 4 + 2 >= readbuf()->size()) {
                _err("process_socks_request: protocol error: request header out of boundary.");
                goto error;
            }

            _dia("process_socks_request: fqdn size: %d",fqdn_sz);
            std::string fqdn((const char*)&readbuf()->data()[5],fqdn_sz);
            _dia("process_socks_request: fqdn requested: %s",fqdn.c_str());
            req_str_addr = fqdn;

            req_port = ntohs(readbuf()->get_at<uint16_t>(5+fqdn_sz));
            _dia("process_socks_request: port requested: %d",req_port);

            com()->nonlocal_dst_port() = req_port;
            com()->nonlocal_src(true);
            _dia("process_socks_request: request (FQDN) for %s -> %s:%d",c_type(),com()->nonlocal_dst_host().c_str(),com()->nonlocal_dst_port());




            std::vector<std::string> target_ips;

            struct sockaddr_storage _ss{};
            com()->resolve_socket_src(socket(), nullptr, nullptr, &_ss);
            auto ipver = com()->l3_proto();

            // Some implementations use atype FQDN, but target is an IP address
            auto* adr_as_fqdn = cidr::cidr_from_str(fqdn.c_str());
            if(adr_as_fqdn != nullptr) {
                // hmm, it's an address
                cidr_free(adr_as_fqdn);

                target_ips.push_back(fqdn);
            } else {
                // really FQDN.

                std::scoped_lock<std::recursive_mutex> l_(DNS::get_dns_lock());


                auto dns_resp = DNS::get_dns_cache().get(( ipver == AF_INET6 ? "AAAA:" : "A:")+fqdn);
                if(dns_resp) {
                    if ( ! dns_resp->answers().empty() ) {
                        long ttl = (dns_resp->loaded_at + dns_resp->answers().at(0).ttl_) - time(nullptr);
                        if(ttl > 0) {
                            for( auto const& a: dns_resp->answers() ) {
                                std::string a_ip = a.ip(false);
                                if(! a_ip.empty() ) {
                                    _dia("process_socks_request: cache candidate: %s",a_ip.c_str());
                                    target_ips.push_back(a_ip);
                                }
                            }
                        }
                    }
                }
            }


            // cache is not populated - send out query
            if(target_ips.empty()) {
                // no targets, send DNS query

                std::string nameserver = choose_dns_server();

                if(!async_dns) {

                    std::shared_ptr<DNS_Response> resp(DNSFactory::get().resolve_dns_s(fqdn, A, nameserver));

                    process_dns_response(resp);
                    setup_target();

                } else {
                    _dia("process_socks_request:");

                    auto dns_req_type = DNS_Record_Type::A;



                    if(ipver == AF_INET6) {
                        _dia("process_socket_request: carrier ipv6");
                        dns_req_type = DNS_Record_Type::AAAA;
                    }
                    else {
                        _dia("process_socks_request: carrier default");

                        if(prefer_ipv6) {
                            _dia("process_socks_request: DNS override to query AAAA");
                            dns_req_type = DNS_Record_Type::AAAA;
                        }
                    }

                    setup_dns_async(fqdn, dns_req_type, nameserver);
                }
            } else {
                if(! target_ips.empty() && choose_server_ip(target_ips)) {
                    setup_target();
                } else {
                    _err("process_socks_request: unable to find destination address for the request");
                    error(true);
                }
            }
        }
        else if(atype == socks5_atype::IPV4) {

            req_atype = socks5_atype::IPV4;
            state_ = socks5_state::REQ_RECEIVED;
            _dia("process_socks_request: request received, type %d", atype);

            auto dst = readbuf()->get_at<uint32_t>(4);
            req_port = ntohs(readbuf()->get_at<uint16_t>(8));
            com()->nonlocal_dst_port() = req_port;
            com()->nonlocal_src(true);
            _dia("process_socks_request: request for %s -> %s:%d",c_type(),com()->nonlocal_dst_host().c_str(),com()->nonlocal_dst_port());


            req_addr.s_addr=dst;

            com()->nonlocal_dst_host() = string_format("%s",inet_ntoa(req_addr));
            com()->nonlocal_dst_port() = req_port;
            com()->nonlocal_src(true);
            _dia("process_socks_request: request (IPv4) for %s -> %s:%d",c_type(),com()->nonlocal_dst_host().c_str(),com()->nonlocal_dst_port());
            setup_target();
        } else {
            _err("unknown address type %d", atype);
        }
    }

    else if (version == 4) {
        _dia("process_socks_request: socks4");

        if(readbuf()->size() < 8) {
            _dia("process_socks_request: socks4 request header too short");
            goto error;
        }

        req_atype = socks5_atype::IPV4;
        state_ = socks5_state::REQ_RECEIVED;
        _dia("socksServerCX::process_socks_request: socks4 request received");

        req_port = ntohs(readbuf()->get_at<uint16_t>(2));
        auto dst = readbuf()->get_at<uint32_t>(4);


        req_addr.s_addr=dst;

        com()->nonlocal_dst_host() = string_format("%s",inet_ntoa(req_addr));
        com()->nonlocal_dst_port() = req_port;
        com()->nonlocal_src(true);
        _dia("socksServerCX::process_socks_request: request (SOCKSv4) for %s -> %s:%d",c_type(),com()->nonlocal_dst_host().c_str(),com()->nonlocal_dst_port());

        setup_target();
    }
    else {
        _dia("process_socks_request: unexpected socks version");
        goto error;
    }


    return readbuf()->size();

    error:
        _dia("socksServerCX::process_socks_request: error %d",e);
        error(true);
        return readbuf()->size();
}

bool socksServerCX::setup_target() {
        // prepare a new CX!
        
        // LEFT
        int s = socket();
        
        baseCom* new_com = nullptr;
        switch(com()->nonlocal_dst_port()) {
            case 443:
            case 465:
            case 636:
            case 993:
            case 995:
                new_com = new baseSSLMitmCom<SSLCom>();
                handoff_as_ssl = true;
                break;
            default:
                new_com = new TCPCom();
        }
        
        auto* n_cx = new MitmHostCX(new_com, s);
        n_cx->waiting_for_peercom(true);

        n_cx->com()->nonlocal_dst(true);
        n_cx->com()->nonlocal_dst_host() = com()->nonlocal_dst_host();
        n_cx->com()->nonlocal_dst_port() = com()->nonlocal_dst_port();
        n_cx->com()->nonlocal_dst_resolved(true);
        
        
        // RIGHT


        auto *target_cx = new MitmHostCX(n_cx->com()->slave(), n_cx->com()->nonlocal_dst_host().c_str(),
                                            string_format("%d",n_cx->com()->nonlocal_dst_port()).c_str()
                                            );
        target_cx->waiting_for_peercom(true);
        
        std::string h;
        std::string p;
        n_cx->com()->resolve_socket_src(n_cx->socket(),&h,&p);
        
        
        target_cx->com()->nonlocal_src(false);
        target_cx->com()->nonlocal_src_host() = h;
        target_cx->com()->nonlocal_src_port() = std::stoi(p); 
        
        
        
        left.reset(n_cx);
        _dia("socksServerCX::setup_target: prepared left: %s",left->c_type());
        right.reset(target_cx);
        _dia("socksServerCX::setup_target: prepared right: %s",right->c_type());
        
        // peers are now prepared for handover. Owning proxy will wipe this CX (it will be empty)
        // and if policy allows, left and right will be set (also in proxy owning this cx).
        
        state_ = socks5_state::WAIT_POLICY;
        read_waiting_for_peercom(true);
        
        return true;
}

bool socksServerCX::new_message() const {
    if(state_ == socks5_state::WAIT_POLICY && verdict_ == socks5_policy::PENDING) {
        return true;
    }
    return state_ == socks5_state::HANDOFF;

}

void socksServerCX::verdict(socks5_policy p) {
    verdict_ = p;
    state_ = socks5_state::POLICY_RECEIVED;
    
    if(verdict_ == socks5_policy::ACCEPT || verdict_ == socks5_policy::REJECT) {
        process_socks_reply();
    }
}

int socksServerCX::process_socks_reply() {
    if(version == 5) {
        
        unsigned char b[128];
        
        b[0] = 5;
        b[1] = 2; // denied
        if(verdict_ == socks5_policy::ACCEPT) b[1] = 0; //accept
        b[2] = 0;
        b[3] = static_cast<int>(req_atype);
        
        int cur = 4;
        
        if(req_atype == socks5_atype::IPV4) {
            *((uint32_t*)&b[cur]) = req_addr.s_addr;
            cur += sizeof(uint32_t);
        }
        else if(req_atype == socks5_atype::FQDN) {
            
            b[cur] = (unsigned char)req_str_addr.size();
            cur++;
            
            for (char c: req_str_addr) {
                b[cur] = c;
                cur++;
            }
        }
            
        *((uint16_t*)&b[cur]) = htons(req_port);
        cur += sizeof(uint16_t);
        
        writebuf()->append(b,cur);
        state_ = socks5_state::REQRES_SENT;

        _dum("socksServerCX::process_socks_reply: response dump:\r\n%s",hex_dump(b,cur, 4, 0, true).c_str());

        // response is about to be sent. In most cases client sends data on left,
        // but in case it's waiting ie. for banner, we must trigger proxy code to
        // actually connect the right side.
        // Because now are all data handled, there is no way how we get to proxy code,
        // unless:
        //      * new data appears on left
        //      * some error occurs on left
        //      * other way how socket appears in epoll result.
        //
        // we can acheive that to simply put left socket to write monitor.
        // This will make left socket writable (dummy - we don't have anything to write),
        // but also triggers proxy's on_message().

        com()->set_write_monitor(socket());
        return cur;
    } 
    else if(version == 4) {
        unsigned char b[8];
        
        b[0] = 0;
        b[1] = 91; // denied
        if(verdict_ == socks5_policy::ACCEPT) b[1] = 90; //accept
        
        *((uint16_t*)&b[2]) = htons(req_port);
        *((uint32_t*)&b[4]) = req_addr.s_addr;
        
        writebuf()->append(b,8);        
        state_ = socks5_state::REQRES_SENT;
        return 8;
    }
    
    return 0;
}

void socksServerCX::pre_write() {
    _deb("socksServerCX::pre_write[%s]: writebuf=%d, readbuf=%d",c_type(),writebuf()->size(),readbuf()->size());
    if(state_ == socks5_state::REQRES_SENT ) {
        if(writebuf()->empty()) {
            _dia("socksServerCX::pre_write[%s]: all flushed, state change to HANDOFF: writebuf=%d, readbuf=%d",c_type(),writebuf()->size(),readbuf()->size());
            waiting_for_peercom(true);
            state(socks5_state::HANDOFF);
        }
    }
    else if(state_ == socks5_state::DNS_RESP_FAILED) {

        if(mixed_ip_versions)  {
            if (not tested_dns_aaaa) {
                tested_dns_aaaa = true;

                auto nameserver = choose_dns_server();
                setup_dns_async(req_str_addr, AAAA, nameserver);
            }
            else if(not tested_dns_a) {
                tested_dns_a = true;

                auto nameserver = choose_dns_server();
                setup_dns_async(req_str_addr, A, nameserver);
            }

        } else {
            _deb("socksServerCX::pre_write[%s]: dns failed", c_type());
            error(true);
        }
    }
}


void socksServerCX::dns_response_callback(dns_response_t const& rresp) {

    auto resp = std::shared_ptr<DNS_Response>(rresp.first);
    int red = rresp.second;
    state_ = socks5_state::DNS_RESP_RECV;

    if(red <= 0) {
        _deb("handle_event: socket read returned %d",red);
        error(true);
    } else {
        _deb("handle_event: OK - socket read returned %d",red);
        if(process_dns_response(resp)) {
            _deb("handle_event: OK, done");
        } else {
            _err("handle_event: processing DNS response failed.");
            state_ = socks5_state::DNS_RESP_FAILED;
        }
    }

    //provoke proxy to act.
    com()->set_monitor(socket());
    com()->set_write_monitor(socket());
}


void socksServerCX::handle_event (baseCom *xcom) {

}

