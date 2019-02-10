#!/usr/bin/env python

"""
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
    """
from __future__ import print_function

import os
import sys
import json
import datetime
import ipaddress


from cryptography import x509
from cryptography.x509.oid import NameOID
from cryptography.hazmat.primitives import hashes

from cryptography.hazmat.backends import default_backend
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives.asymmetric import ec


SETTINGS = {}
SETTINGS["ca"] = {}
SETTINGS["srv"] = {}
SETTINGS["clt"] = {}

SETTINGS["path"] = "/tmp/"


def _write_default_settings():
    global SETTINGS

    # we want to extend, but not overwrite already existing settings
    load_settings()

    r = SETTINGS

    if "cn" not in r["ca"]: r["ca"]["cn"] = "Smithproxy Root CA"
    if "ou" not in r["ca"]: r["ca"]["ou"] = None
    if "o" not in r["ca"]: r["ca"]["o"] = "Smithproxy Software"
    if "s" not in r["ca"]: r["ca"]["s"] = None
    if "l" not in r["ca"]: r["ca"]["l"] = None
    if "c" not in r["ca"]: r["ca"]["c"] = "CZ"
    if "grant_ca" not in r["ca"]: r["ca"]["grant_ca"] = False

    if "cn" not in r["srv"]: r["srv"]["cn"] = "Smithproxy Default Server Certificate"
    if "ou" not in r["srv"]: r["srv"]["ou"] = None
    if "o" not in r["srv"]: r["srv"]["o"] = "Smithproxy Software"
    if "s" not in r["srv"]: r["srv"]["s"] = None
    if "l" not in r["srv"]: r["srv"]["l"] = None
    if "c" not in r["srv"]: r["srv"]["c"] = "CZ"

    #print("config to be written: %s" % (r,))

    try:
        with open(os.path.join(SETTINGS["path"],"sslca.json"),"w") as f:
            json.dump(r,f,indent=4)

    except Exception as e:
        print("write_default_settings: exception caught: " + str(e))

def load_settings():
    global SETTINGS
    try:
        with open(os.path.join(SETTINGS["path"],"sslca.json"),"r") as f:
            r = json.load(f)
            #print("load_settings: loaded settings: {}", str(r))

            SETTINGS = r

    except Exception as e:
        print("load_default_settings: exception caught: " + str(e))



def generate_rsa_key(size):
    return rsa.generate_private_key(public_exponent=65537, key_size=size, backend=default_backend())

def generate_ec_key(curve):
    return ec.generate_private_key(curve=curve, backend=default_backend())



def save_key(key, keyfile, passphrase):

    def choose_enc(pwd):
        if not pwd:
            return serialization.NoEncryption()
        return serialization.BestAvailableEncryption(pwd)

    try:
        with open(os.path.join(SETTINGS['path'],keyfile), "wb") as f:
            f.write(key.private_bytes(
                encoding=serialization.Encoding.PEM,
                format=serialization.PrivateFormat.TraditionalOpenSSL,
                encryption_algorithm=choose_enc(passphrase),
                ))

    except Exception as e:
        print("save_key: exception caught: " + str(e))


def construct_sn(profile):
    snlist = []

    if "cn" in SETTINGS[profile] and SETTINGS[profile]["cn"]:
        snlist.append(x509.NameAttribute(NameOID.COMMON_NAME,SETTINGS[profile]["cn"]))
    if "ou" in SETTINGS[profile] and SETTINGS[profile]["ou"]:
        snlist.append(x509.NameAttribute(NameOID.ORGANIZATIONAL_UNIT_NAME,SETTINGS[profile]["ou"]))
    if "o" in SETTINGS[profile] and SETTINGS[profile]["o"]:
        snlist.append(x509.NameAttribute(NameOID.ORGANIZATION_NAME,SETTINGS[profile]["o"]))
    if "l" in SETTINGS[profile] and SETTINGS[profile]["l"]:
        snlist.append(x509.NameAttribute(NameOID.LOCALITY_NAME,SETTINGS[profile]["l"]))
    if "s" in SETTINGS[profile] and SETTINGS[profile]["s"]:
        snlist.append(x509.NameAttribute(NameOID.STATE_OR_PROVINCE_NAME,SETTINGS[profile]["s"]))
    if "c" in SETTINGS[profile] and SETTINGS[profile]["c"]:
        snlist.append(x509.NameAttribute(NameOID.COUNTRY_NAME,SETTINGS[profile]["c"]))

    return snlist


def generate_csr(key, profile, sans_dns=[], sans_ip=[], isca=False):
    global SETTINGS

    sn = x509.Name(construct_sn(profile))


    sans = None
    sans_list = []

    if sans_dns:
        for s in sans_dns:
            sans_list.append(x509.DNSName(s))


    if sans_ip:
        sans_ip_list = []
        for i in sans_ip:
            ii = ipaddress.IPv4Address(i)
            sans_list.append(x509.IPAddress(ii))


    sans = x509.SubjectAlternativeName(sans_list)


    builder = x509.CertificateSigningRequestBuilder()
    builder = builder.subject_name(sn)

    if sans:
        builder = builder.add_extension(sans, critical=False)


    builder = builder.add_extension(
        x509.BasicConstraints(ca=isca, path_length=None), critical=True)

    csr = builder.sign(key, hashes.SHA256(), default_backend())

    return csr

def sign_csr(key, csr, caprofile, valid=30, isca=False):
    global SETTINGS

    one_day = datetime.timedelta(1, 0, 0)

    builder = x509.CertificateBuilder()
    builder = builder.subject_name(csr.subject)
    builder = builder.issuer_name(
        x509.Name(construct_sn(caprofile))
    )
    builder = builder.not_valid_before(datetime.datetime.today() - one_day)
    builder = builder.not_valid_after(datetime.datetime.today() + (one_day * valid))
    builder = builder.serial_number(x509.random_serial_number())
    builder = builder.public_key(csr.public_key())


    print("sign CSR: == extensions ==")
    for e in csr.extensions:
        if isinstance(e.value,x509.BasicConstraints):
            print("sign CSR: %s" % (e.oid,))

            if e.value.ca == True:
                print("           CA=TRUE requested")

                if isca and not SETTINGS["ca"]["grant_ca"]:
                    print("           not allowed but overridden")
                if not SETTINGS["ca"]["grant_ca"]:
                    print("           not allowed by rule")
                    continue
                else:
                    print("           allowed by rule")

        builder = builder.add_extension(e.value, e.critical)

    certificate = builder.sign(private_key=key, algorithm=hashes.SHA256(),backend=default_backend())
    return certificate


def save_certificate(cert,certfile):
    try:
        with open(os.path.join(SETTINGS['path'],certfile), "wb") as f:
            f.write(cert.public_bytes(
                encoding=serialization.Encoding.PEM))

    except Exception as e:
        print("save_certificate: exception caught: " + str(e))

if __name__ == "__main__":

    _write_default_settings()
    load_settings()

    # generate CA RSA key
    ca_key = generate_rsa_key(2048)
    save_key(ca_key,"ca-key.pem",None)

    # generate CA CSR for self-signing & self-sign
    ca_csr = generate_csr(ca_key,"ca",isca=True)
    ca_cert = sign_csr(ca_key,ca_csr,"ca",valid=3*30,isca=True)
    save_certificate(ca_cert,"ca-cert.pem")


    # generate default server key and certificate & sign by CA
    srv_key = generate_rsa_key(2048)
    srv_csr = generate_csr(srv_key,"srv",sans_dns=["portal.demo.smithproxy.net",])
    srv_cert = sign_csr(ca_key,srv_csr,"ca")
    save_certificate(srv_cert,"srv-cert.pem")


    # Experimental: generate EC CA key
    ec_ca_key = generate_ec_key(ec.SECP256K1())
    save_key(ec_ca_key,"ec-ca-key.pem",None)

    # self-sign
    ec_ca_csr = generate_csr(ec_ca_key,"ca",isca=True)
    ec_ca_cert = sign_csr(ec_ca_key,ec_ca_csr,"ca",valid=6*30,isca=True)
    save_certificate(ec_ca_cert,"ec-ca-cert.pem")
