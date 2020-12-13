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

#ifndef POLICY_HPP
 #define POLICY_HPP
 
 
#include <vector> 

#include <hostcx.hpp>
#include <baseproxy.hpp>
#include <ext/libcidr/cidr.hpp>
#include <ranges.hpp>

#include <policy/addrobj.hpp>
#include <policy/profiles.hpp>

#include <sobject.hpp>

#define POLICY_ACTION_DENY  0
#define POLICY_ACTION_PASS  1

#define POLICY_NAT_NONE     0
#define POLICY_NAT_AUTO     1
#define POLICY_NAT_POOL     2


class PolicyRule : public ProfileList , public socle::sobject {

public:
    using group_of_ports = std::vector<std::shared_ptr<CfgRange>>;
    using group_of_addresses = std::vector<std::shared_ptr<AddressObject>>;

    unsigned int cnt_matches = 0;

    CfgUint8 proto;
    bool proto_default = true;

    group_of_addresses src;
    bool src_default = true;

    group_of_ports  src_ports;
    bool src_ports_default = true;

    group_of_addresses  dst;
    bool dst_default = true;

    group_of_ports dst_ports;
    bool dst_ports_default = true;

    int action = POLICY_ACTION_PASS;
    std::string action_name;
    int nat    = POLICY_NAT_NONE;
    std::string nat_name;

    PolicyRule() : ProfileList(), socle::sobject(),
                   proto(0),
                   log(this, "policy.rule")
                   {};

    virtual ~PolicyRule() = default;

    bool match(baseProxy*);
    bool match(std::vector<baseHostCX*>& l, std::vector<baseHostCX*>& r);


    int sock_2_net(int sock_type);
    bool match_proto_cx(int acl_proto, baseHostCX* cx);
    bool match_proto_vecx(int acl_proto, std::vector<baseHostCX*> const& vec_cx);

    bool match_addrgrp_cx(group_of_addresses &sources, baseHostCX* cx);
    bool match_addrgrp_vecx(group_of_addresses &sources, std::vector<baseHostCX*>& vecx);

    bool match_rangegrp_cx(group_of_ports& ranges,baseHostCX* cx);
    bool match_rangegrp_vecx(group_of_ports& ranges,std::vector<baseHostCX*>& vecx);

    bool ask_destroy() override { return false; }
    std::string to_string(int verbosity = 6) const override;

    DECLARE_C_NAME("PolicyRule");
    DECLARE_LOGGING(to_string);

    logan_attached<PolicyRule> log;

public:
    logan_attached<PolicyRule> const& get_log() const { return log; }
};


#endif