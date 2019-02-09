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

#include <policy.hpp>

DEFINE_LOGGING(PolicyRule);

std::string PolicyRule::to_string(int verbosity) {

    std::string from = "PolicyRule:";
    
    switch(proto) {
        case 6:
            from += " [tcp] ";
            break;
        case 17:
            from += " [udp] ";
            break;
        default:
            from += string_format(" [%3d] ",proto);
    }
    
    if(src_default) from += "*";
    for(auto it: src) {
        if(verbosity > iINF) {
            from += string_format("(0x%x)",this);
        }
        from += " ";
        from += it->to_string();
    }
    from += ":";
    if(src_ports_default) from += "*";
    for(auto it: src_ports) {
        from += string_format("(%d,%d) ",it.first,it.second);
    }
    
    std::string to = "";
    if(dst_default) to += "*";
    for(auto it: dst) {
        to += " ";
        to += it->to_string();
    }
    to += ":";
    if(dst_ports_default) to+="*";
    for(auto it: dst_ports) {
        to += string_format("(%d,%d) ",it.first,it.second);
    }
    
    std::string out = from + " -> " + to + "= ";
    
    switch(action) {
        case POLICY_ACTION_PASS:
            out += "ACCEPT";
            break;
        case POLICY_ACTION_DENY:
            out += "REJECT";
            break;
        default:
            out += "???";
    }
    
    switch(nat) {
        case POLICY_NAT_NONE:
            out += "(nonat)";
            break;
        case POLICY_NAT_AUTO:
            out += "(iface)";
            break;
        case POLICY_NAT_POOL:
            out += "( pool)";
            break;
        default:
            out += "(  ?  )";
            break;
    }
    
    out += " [" + std::to_string(cnt_matches) + "]";
    
    if(verbosity > INF) {
        out+=": ";
        if(profile_auth) out += string_format("\n    auth=%s  (0x%x) ",profile_auth->prof_name.c_str(),profile_auth);
        if(profile_tls) out += string_format("\n    tls=%s  (0x%x) ",profile_tls->prof_name.c_str(),profile_tls);
        if(profile_detection) out += string_format("\n    det=%s  (0x%x) ",profile_detection->prof_name.c_str(),profile_detection);
        if(profile_content) out += string_format("\n    cont=%s  (0x%x) ",profile_content->prof_name.c_str(),profile_content);
        if(profile_alg_dns) out += string_format("\n    alg_dns=%s  (0x%x) ",profile_alg_dns->prof_name.c_str(),profile_alg_dns);
    }
    
    return out;
}


bool PolicyRule::match_addrgrp_cx(std::vector< AddressObject* >& sources, baseHostCX* cx) {
    bool match = false;
    
    if(sources.size() == 0) {
        match = true;
//                 DIAS_("PolicyRule: matched ");
    } else {
        CIDR* l = cidr_from_str(cx->host().c_str());
        for(std::vector<AddressObject*>::iterator j = sources.begin(); j != sources.end(); ++j ) {
            AddressObject* comp = (*j);
            
            if(comp->match(l)) {
                if(LEV_(DIA)) {
                    char* a = cidr_to_str(l);
                    DIA_("PolicyRule::match_addrgrp_cx: comparing %s with rule %s: matched",a,comp->to_string().c_str());
                    delete a;
                }
                match = true;
                break;
            } else {
                if(LEV_(DIA)) {
                    char* a = cidr_to_str(l);
                    DIA_("PolicyRule::match_addrgrp_cx: comparing %s with rule %s: not matched",a,comp->to_string().c_str());
                    delete[] a;
                }
            }
        }
        cidr_free(l);
    }

    return match;
}

bool PolicyRule::match_rangegrp_cx(std::vector< range >& ranges, baseHostCX* cx) {
    bool match = false;
    
    if(ranges.size() == 0) {
        match = true;
//                 DIAS_("PolicyRule: matched ");
    } else {
        int p = std::stoi(cx->port());
        for(std::vector<range>::iterator j = ranges.begin(); j != ranges.end(); ++j ) {
            range& comp = (*j);
            if((p >= comp.first) && (p <= comp.second)) {
                DIA_("PolicyRule::match_rangergrp_cx: comparing %d with %s: matched",p,rangetos(comp).c_str());
                match = true;
                break;
            } else {
                DIA_("PolicyRule::match_rangergrp_cx: comapring %d with %s: not matched",p,rangetos(comp).c_str());
            }
        }
    }

    return match;
}

bool PolicyRule::match_rangegrp_vecx(std::vector< range >& ranges, std::vector< baseHostCX* >& vecx) {
    bool match = false;
    
    int idx = -1;
    for(std::vector<baseHostCX*>::iterator i = vecx.begin(); i != vecx.end(); ++i ) {
        ++idx;
        baseHostCX* cx = (*i);
        
        match = match_rangegrp_cx(ranges,cx);
        if(match) {
            DIA_("PolicyRule::match_rangegrp_vecx: %s matched",cx->c_name());
            break;
        } else {
            DIA_("PolicyRule::match_rangegrp_vecx: %s not matched",cx->c_name())
        }
    }
    
    return match;
}


bool PolicyRule::match_addrgrp_vecx(std::vector< AddressObject* >& sources, std::vector< baseHostCX* >& vecx) {
    bool match = false;
    
    int idx = -1;
    for(std::vector<baseHostCX*>::iterator i = vecx.begin(); i != vecx.end(); ++i ) {
        ++idx;
        baseHostCX* cx = (*i);
        
        match = match_addrgrp_cx(sources,cx);
        if(match) {
            DIA_("PolicyRule::match_addrgrp_vecx: %s matched",cx->c_name())
            break;
        } else {
            DIA_("PolicyRule::match_addrgrp_vecx: %s not matched",cx->c_name())
        }
    }
    
    return match;
}


bool PolicyRule::match(baseProxy* p) {
    
    bool lmatch = false;
    bool lpmatch = false;
    bool rmatch = false;
    bool rpmatch = false;
    
    if(p != nullptr) {
        DIAS_("PolicyRule::match");
        
        lmatch = match_addrgrp_vecx(src,p->ls()) || match_addrgrp_vecx(src,p->lda());
        if(!lmatch) goto end;

        lpmatch = match_rangegrp_vecx(src_ports,p->ls()) || match_rangegrp_vecx(src_ports,p->lda());
        if(!lpmatch) goto end;

        rmatch = match_addrgrp_vecx(dst,p->rs()) || match_addrgrp_vecx(dst,p->rda());
        if(!rmatch) goto end;

        rpmatch = match_rangegrp_vecx(dst_ports,p->rs()) || match_rangegrp_vecx(dst_ports,p->rda());
        if(!rpmatch) goto end;
        
    } else {
        DIAS_("PolicyRule::match: p is nullptr");
    }
    
    end:
    
    if (lmatch && lmatch && rmatch && rpmatch) {
        DIAS_("PolicyRule::match ok");
        cnt_matches++;
        
        return true;
    } else {
        DIA_("PolicyRule::match failed: %d:%d->%d:%d",lmatch,lpmatch,rmatch,rpmatch);
    }

    return false;
}

bool PolicyRule::match(std::vector<baseHostCX*>& l, std::vector<baseHostCX*>& r) {
    bool lmatch = false;
    bool lpmatch = false;
    bool rmatch = false;
    bool rpmatch = false;
    

    DIAS_("PolicyRule::match_lr");
    
    lmatch = match_addrgrp_vecx(src,l);
    if(!lmatch) goto end;

    lpmatch = match_rangegrp_vecx(src_ports,l);
    if(!lpmatch) goto end;

    rmatch = match_addrgrp_vecx(dst,r);
    if(!rmatch) goto end;

    rpmatch = match_rangegrp_vecx(dst_ports,r);
    if(!rpmatch) goto end;
    
    
    if(LEV >= DEB ) {
        for(auto i: l) DUM_("PolicyRule::match_lr L: %s", i->to_string().c_str());
        for(auto i: r) DUM_("PolicyRule::match_lr R: %s", i->to_string().c_str());
        DEB_("PolicyRule::match_lr Success: %d:%d->%d:%d",lmatch,lpmatch,rmatch,rpmatch);
    }

    end:
    
    
    if (lmatch && lpmatch && rmatch && rpmatch) {
        DIAS_("PolicyRule::match_lr ok");
        cnt_matches++;
        
        return true;
    } else {
        DIA_("PolicyRule::match_lr failed: %d:%d->%d:%d",lmatch,lpmatch,rmatch,rpmatch);
    }

    return false;
}

PolicyRule::~PolicyRule() {
}

