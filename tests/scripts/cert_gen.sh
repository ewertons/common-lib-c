#!/bin/bash

set -x # Set trace on
set -o errexit # Exit if command failed
set -o pipefail # Exit if pipe failed

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

ca_name="My Certificate Authority"
root_ca_dir="."
home_dir="."
algorithm="genrsa"
COUNTRY="US"
STATE="WA"
LOCALITY="Redmond"
ORGANIZATION_NAME="My Organization"
root_ca_password="1234"
key_bits_length="4096"
days_till_expire=30
ca_chain_prefix="chain.ca"
intermediate_ca_dir="."
openssl_root_config_file="$SCRIPT_DIR/openssl_root_ca.cnf"
openssl_intermediate_config_file="$SCRIPT_DIR/openssl_device_intermediate_ca.cnf"
intermediate_ca_password="1234"
root_ca_prefix="root.ca"
intermediate_ca_prefix="intermediate"

function makeCNsubject()
{
    local result="/CN=${1}"
    case $OSTYPE in win32) result="/${result}"
    esac
    echo "$result"
}

function generate_root_ca()
{
    local common_name="$ca_name Root Certificate"
    local password_cmd=" -aes256 -passout pass:${root_ca_password} "

    cd "${home_dir}"
    echo "Creating the Root CA Private Key"

    openssl "${algorithm}" \
            ${password_cmd} \
            -out "${root_ca_dir}/private/${root_ca_prefix}.key.pem" \
            ${key_bits_length}
    [ $? -eq 0 ] || exit $?
    chmod 400 "${root_ca_dir}/private/${root_ca_prefix}.key.pem"
    [ $? -eq 0 ] || exit $?

    echo "Creating the Root CA Certificate"
    password_cmd=" -passin pass:${root_ca_password} "

    openssl req \
            -new \
            -x509 \
            -config "${openssl_root_config_file}" \
            ${password_cmd} \
            -key "${root_ca_dir}/private/${root_ca_prefix}.key.pem" \
            -subj "$(makeCNsubject "${common_name}")" \
            -days ${days_till_expire} \
            -sha256 \
            -extensions v3_ca \
            -out "${root_ca_dir}/certs/${root_ca_prefix}.cert.pem"
    [ $? -eq 0 ] || exit $?
    chmod 444 "${root_ca_dir}/certs/${root_ca_prefix}.cert.pem"
    [ $? -eq 0 ] || exit $?

    echo "CA Root Certificate Generated At:"
    echo "---------------------------------"
    echo "    ${root_ca_dir}/certs/${root_ca_prefix}.cert.pem"
    echo ""
    openssl x509 -noout -text \
            -in "${root_ca_dir}/certs/${root_ca_prefix}.cert.pem"

    [ $? -eq 0 ] || exit $?
}



###############################################################################
# Generate Intermediate CA Cert
###############################################################################
function generate_intermediate_ca()
{
    local common_name="$ca_name Intermediate Cert"

    local password_cmd=" -aes256 -passout pass:${intermediate_ca_password} "
    echo "Creating the Intermediate Device CA"
    echo "-----------------------------------"
    cd "${home_dir}"

    openssl "${algorithm}" \
            ${password_cmd} \
            -out "${intermediate_ca_dir}/private/${intermediate_ca_prefix}.key.pem" \
            ${key_bits_length}
    [ $? -eq 0 ] || exit $?
    chmod 400 "${intermediate_ca_dir}/private/${intermediate_ca_prefix}.key.pem"
    [ $? -eq 0 ] || exit $?


    echo "Creating the Intermediate Device CA CSR"
    echo "-----------------------------------"
    password_cmd=" -passin pass:${intermediate_ca_password} "

    openssl req -new -sha256 \
        ${password_cmd} \
        -config "${openssl_intermediate_config_file}" \
        -subj "$(makeCNsubject "${common_name}")" \
        -key "${intermediate_ca_dir}/private/${intermediate_ca_prefix}.key.pem" \
        -out "${intermediate_ca_dir}/csr/${intermediate_ca_prefix}.csr.pem"
    [ $? -eq 0 ] || exit $?

    echo "Signing the Intermediate Certificate with Root CA Cert"
    echo "-----------------------------------"
    password_cmd=" -passin pass:${root_ca_password} "

    openssl ca -batch \
        -config "${openssl_root_config_file}" \
        ${password_cmd} \
        -extensions v3_intermediate_ca \
        -days ${days_till_expire} -notext -md sha256 \
        -in "${intermediate_ca_dir}/csr/${intermediate_ca_prefix}.csr.pem" \
        -out "${intermediate_ca_dir}/certs/${intermediate_ca_prefix}.cert.pem"
    [ $? -eq 0 ] || exit $?
    chmod 444 "${intermediate_ca_dir}/certs/${intermediate_ca_prefix}.cert.pem"
    [ $? -eq 0 ] || exit $?

    echo "Verify signature of the Intermediate Device Certificate with Root CA"
    echo "-----------------------------------"
    openssl verify \
            -CAfile "${root_ca_dir}/certs/${root_ca_prefix}.cert.pem" \
            "${intermediate_ca_dir}/certs/${intermediate_ca_prefix}.cert.pem"
    [ $? -eq 0 ] || exit $?

    echo "Intermediate CA Certificate Generated At:"
    echo "-----------------------------------------"
    echo "    ${intermediate_ca_dir}/certs/${intermediate_ca_prefix}.cert.pem"
    echo ""
    openssl x509 -noout -text \
            -in "${intermediate_ca_dir}/certs/${intermediate_ca_prefix}.cert.pem"
    [ $? -eq 0 ] || exit $?

    echo "Create Root + Intermediate CA Chain Certificate"
    echo "-----------------------------------"
    cat "${intermediate_ca_dir}/certs/${intermediate_ca_prefix}.cert.pem" \
        "${root_ca_dir}/certs/${root_ca_prefix}.cert.pem" > \
        "${intermediate_ca_dir}/certs/${ca_chain_prefix}.cert.pem"
    [ $? -eq 0 ] || exit $?
    chmod 444 "${intermediate_ca_dir}/certs/${ca_chain_prefix}.cert.pem"
    [ $? -eq 0 ] || exit $?

    echo "Root + Intermediate CA Chain Certificate Generated At:"
    echo "------------------------------------------------------"
    echo "    ${intermediate_ca_dir}/certs/${ca_chain_prefix}.cert.pem"
}

###############################################################################
# Generate a Certificate for a device using specific openssl extension and
# signed with either the root or intermediate cert.
###############################################################################
function generate_device_certificate_common()
{
    local common_name="${1}"
    local device_prefix="${2}"
    local certificate_dir="${3}"
    local ca_password="${4}"
    local server_pfx_password="1234"
    local password_cmd=" -passin pass:${ca_password} "
    local openssl_config_file="${5}"
    local openssl_config_extension="${6}"
    local cert_type_diagnostic="${7}"

    echo "Creating ${cert_type_diagnostic} Certificate"
    echo "----------------------------------------"
    cd "${home_dir}"

    openssl "${algorithm}" \
            -out "${certificate_dir}/private/${device_prefix}.key.pem" \
            ${key_bits_length}
    [ $? -eq 0 ] || exit $?
    chmod 444 "${certificate_dir}/private/${device_prefix}.key.pem"
    [ $? -eq 0 ] || exit $?

    echo "Create the ${cert_type_diagnostic} Certificate Request"
    echo "----------------------------------------"
    openssl req -config "${openssl_config_file}" \
        -key "${certificate_dir}/private/${device_prefix}.key.pem" \
        -subj "$(makeCNsubject "${common_name}")" \
        -new -sha256 -out "${certificate_dir}/csr/${device_prefix}.csr.pem"
    [ $? -eq 0 ] || exit $?

    openssl ca -batch -config "${openssl_config_file}" \
            ${password_cmd} \
            -extensions "${openssl_config_extension}" \
            -days ${days_till_expire} -notext -md sha256 \
            -in "${certificate_dir}/csr/${device_prefix}.csr.pem" \
            -out "${certificate_dir}/certs/${device_prefix}.cert.pem"
    [ $? -eq 0 ] || exit $?
    chmod 444 "${certificate_dir}/certs/${device_prefix}.cert.pem"
    [ $? -eq 0 ] || exit $?

    echo "Verify signature of the ${cert_type_diagnostic}" \
         " certificate with the signer"
    echo "-----------------------------------"
    openssl verify \
            -CAfile "${certificate_dir}/certs/${ca_chain_prefix}.cert.pem" \
            "${certificate_dir}/certs/${device_prefix}.cert.pem"
    [ $? -eq 0 ] || exit $?

    echo "${cert_type_diagnostic} Certificate Generated At:"
    echo "----------------------------------------"
    echo "    ${certificate_dir}/certs/${device_prefix}.cert.pem"
    echo ""
    openssl x509 -noout -text \
            -in "${certificate_dir}/certs/${device_prefix}.cert.pem"
    [ $? -eq 0 ] || exit $?
    echo "Create the ${cert_type_diagnostic} PFX Certificate"
    echo "----------------------------------------"
    openssl pkcs12 -in "${certificate_dir}/certs/${device_prefix}.cert.pem" \
            -inkey "${certificate_dir}/private/${device_prefix}.key.pem" \
            -password pass:${server_pfx_password} \
            -export -out "${certificate_dir}/certs/${device_prefix}.cert.pfx"
    [ $? -eq 0 ] || exit $?
    echo "${cert_type_diagnostic} PFX Certificate Generated At:"
    echo "--------------------------------------------"
    echo "    ${certificate_dir}/certs/${device_prefix}.cert.pfx"
    [ $? -eq 0 ] || exit $?
}

###############################################################################
# Generate a certificate for a leaf device
# signed with either the root or intermediate cert.
###############################################################################
function generate_leaf_certificate()
{
    local common_name="${1}"
    local device_prefix="${2}"
    local certificate_dir="${3}"
    local ca_password="${4}"
    local openssl_config_file="${5}"

    generate_device_certificate_common "${common_name}" "${device_prefix}" \
                                       "${certificate_dir}" "${ca_password}" \
                                       "${openssl_config_file}" "usr_cert" \
                                       "Leaf Device"
}

###############################################################################
# Generate a certificate for a server
# signed with either the root or intermediate cert.
###############################################################################
function generate_server_certificate()
{
    local common_name="${1}"
    local device_prefix="${2}"
    local certificate_dir="${3}"
    local ca_password="${4}"
    local openssl_config_file="${5}"

    generate_device_certificate_common "${common_name}" "${device_prefix}" \
                                       "${certificate_dir}" "${ca_password}" \
                                       "${openssl_config_file}" "server_cert" \
                                       "Server"
}

###############################################################################
#  Creates required directories and removes left over cert files.
#  Run prior to creating Root CA; after that these files need to persist.
###############################################################################
function prepare_filesystem()
{
    if [ ! -f ${openssl_root_config_file} ]; then
        echo "Missing file ${openssl_root_config_file}"
        exit 1
    fi

    if [ ! -f ${openssl_intermediate_config_file} ]; then
        echo "Missing file ${openssl_intermediate_config_file}"
        exit 1
    fi

    rm -rf csr
    rm -rf private
    rm -rf certs
    rm -rf intermediateCerts
    rm -rf newcerts

    mkdir -p csr
    mkdir -p private
    mkdir -p certs
    mkdir -p intermediateCerts
    mkdir -p newcerts

    rm -f ./index.txt
    touch ./index.txt

    rm -f ./serial
    echo 01 > ./serial
}

###############################################################################
# Generates a root and intermediate certificate for CA certs.
###############################################################################
function initial_cert_generation()
{
    prepare_filesystem
    generate_root_ca
    generate_intermediate_ca
}

###############################################################################
# Generates a certificate for verification, chained directly to the root.
###############################################################################
function generate_verification_certificate()
{
    if [ -z $1 ]; then
        echo "Usage: create_verification_certificate <subjectName>"
        exit 1
    fi

    rm -f ./private/verification-code.key.pem
    rm -f ./certs/verification-code.cert.pem
    generate_leaf_certificate "${1}" "verification-code" \
                              "${root_ca_dir}" "${root_ca_password}" \
                              "${openssl_root_config_file}"
}

###############################################################################
# Generates a certificate for a device, chained directly to the root.
###############################################################################
function generate_device_certificate()
{
    if [ -z $1 ]; then
        echo "Usage: create_device_certificate <subjectName>"
        exit 1
    fi

    rm -f ./private/new-device.key.pem
    rm -f ./certs/new-device.key.pem
    rm -f ./certs/new-device-full-chain.cert.pem
    generate_leaf_certificate "${1}" "new-device" \
                              "${root_ca_dir}" "${root_ca_password}" \
                              "${openssl_root_config_file}"
}


###############################################################################
# Generates a certificate for a client, chained to the intermediate.
###############################################################################
function generate_client_certificate_from_intermediate()
{
    if [ -z $1 ]; then
        echo "Usage: create_client_certificate_from_intermediate <subjectName>"
        exit 1
    fi

    rm -f ./private/client.key.pem
    rm -f ./certs/client.key.pem
    rm -f ./certs/client-full-chain.cert.pem
    generate_leaf_certificate "${1}" "client" \
                              "${intermediate_ca_dir}" "${intermediate_ca_password}" \
                              "${openssl_intermediate_config_file}"
}

###############################################################################
# Generates a certificate for a Server, chained to the intermediate.
###############################################################################
function generate_server_certificate_from_intermediate()
{
    if [ -z $1 ]; then
        echo "Usage: create_server_certificate_from_intermediate <subjectName>"
        exit 1
    fi

    rm -f ./private/server.key.pem
    rm -f ./certs/server.key.pem
    rm -f ./certs/server-full-chain.cert.pem
    generate_server_certificate "${1}" "server" \
                              "${intermediate_ca_dir}" "${intermediate_ca_password}" \
                              "${openssl_intermediate_config_file}"
}

###############################################################################
# Generates a certificate for a Edge device, chained to the intermediate.
###############################################################################
function generate_edge_device_certificate()
{
    local device_prefix="new-edge-device"
    if [ -z $1 ]; then
        echo "Usage: create_edge_device_certificate <subjectName>"
        exit 1
    fi
    rm -f ./private/new-edge-device.key.pem
    rm -f ./certs/new-edge-device.cert.pem
    rm -f ./certs/new-edge-device-full-chain.cert.pem

    # Note: Appending a '.ca' to the common name is useful in situations
    # where a user names their hostname as the edge device name.
    # By doing so we avoid TLS validation errors where we have a server or
    # client certificate where the hostname is used as the common name
    # which essentially results in "loop" for validation purposes.
    generate_device_certificate_common "${1}.ca" \
                                       "${device_prefix}" \
                                       "${intermediate_ca_dir}" \
                                       "${intermediate_ca_password}" \
                                       "${openssl_intermediate_config_file}" \
                                       "v3_intermediate_ca" "Edge Device"
}

set +x
if [ "${1}" == "create_root_and_intermediate" ]; then
    initial_cert_generation
elif [ "${1}" == "create_verification_certificate" ]; then
    generate_verification_certificate "${2}"
elif [ "${1}" == "create_device_certificate" ]; then
    generate_device_certificate "${2}"
elif [ "${1}" == "create_client_certificate_from_intermediate" ]; then
    generate_client_certificate_from_intermediate "${2}"
elif [ "${1}" == "create_server_certificate_from_intermediate" ]; then
    generate_server_certificate_from_intermediate "${2}"
elif [ "${1}" == "create_edge_device_certificate" ]; then
    generate_edge_device_certificate "${2}"
else
    echo "Usage: create_root_and_intermediate                               # Creates a new root and intermediate certificates"
    echo "       create_verification_certificate <subjectName>              # Creates a verification certificate, signed with <subjectName>"
    echo "       create_device_certificate <subjectName>                    # Creates a device certificate signed by root with <subjectName>"
    echo "       create_client_certificate_from_intermediate <subjectName>  # Creates a device certificate signed by intermediate with <subjectName>"
    echo "       create_server_certificate_from_intermediate <subjectName>  # Creates a device certificate signed by intermediate with <subjectName>"
    echo "       create_edge_device_certificate <subjectName>               # Creates an edge device certificate, signed with <subjectName>"
    exit 1
fi




