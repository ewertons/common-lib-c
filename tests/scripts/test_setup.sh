#!/bin/bash

./cert_gen.sh create_root_and_intermediate  
./cert_gen.sh create_client_certificate_from_intermediate client1
./cert_gen.sh create_server_certificate_from_intermediate localhost

sed -i "s@#define CLIENT_CERT_PATH \".*\"@#define CLIENT_CERT_PATH \"$(pwd)/certs/client.cert.pem\"@g" ../src/test_socket.c
sed -i "s@#define CLIENT_PK_PATH \".*\"@#define CLIENT_PK_PATH \"$(pwd)/private/client.key.pem\"@g" ../src/test_socket.c
sed -i "s@#define SERVER_CERT_PATH \".*\"@#define SERVER_CERT_PATH \"$(pwd)/certs/server.cert.pem\"@g" ../src/test_socket.c
sed -i "s@#define SERVER_PK_PATH \".*\"@#define SERVER_PK_PATH \"$(pwd)/private/server.key.pem\"@g" ../src/test_socket.c
sed -i "s@#define CA_CHAIN_PATH \".*\"@#define CA_CHAIN_PATH \"$(pwd)/certs/chain.ca.cert.pem\"@g" ../src/test_socket.c
