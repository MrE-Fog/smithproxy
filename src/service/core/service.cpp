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


#include <unistd.h>

#include <service/core/service.hpp>
#include <basecom.hpp>

void Service::my_terminate (int param) {

    Service* service = self();
    service->cnt_terminate++;

    if (not service->cfg_daemonize ) {
        _cons("Terminating ...");
    }

    // don't run multiple times
    if(service->cnt_terminate == 1) {
        service->terminate_flag = true;
    }
    else if(not service->cfg_daemonize) {
        _cons("... shutdown already requested");
    }

    if(service->cnt_terminate == 3) {
        if (not service->cfg_daemonize )
            _cons("Failed to terminate gracefully. Next attempt will force the abort.");
    }
    if(service->cnt_terminate > 3) {
        if (not service->cfg_daemonize )
            _cons("Enforced exit.");
        abort();
    }
}


void Service::my_usr1 (int param) {
    auto const& log = service_log();

    _dia("USR1 signal handler started (param %d)", param);
    self()->reload();
}


bool Service::abort_sleep(unsigned int steps, time_t step) {

    timespec ts{};
    ts.tv_sec = step;

    for(unsigned int i = 0; i < steps; i++) {
        nanosleep(&ts, nullptr);
        if (Service::self()->terminate_flag) {
            return true;
        }
    }

    return false;
}


