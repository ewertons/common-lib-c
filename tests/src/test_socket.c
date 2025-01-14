#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "tests.h"
#include "socket.h"

#include <unistd.h> 

static void socket_client_and_server_success(void** state)
{
    (void)state;
    int port = 5577;

    socket_config_t client_socket_config = socket_get_default_secure_client_config();
    client_socket_config.remote.hostname = span_from_str_literal("localhost");
    client_socket_config.remote.port = port;
    client_socket_config.tls.certificate_file = "/home/ewertons/code/s1/azure-iot-sdk-c/tools/CACertificates/certs/client.cert.pem";
    client_socket_config.tls.private_key_file = "/home/ewertons/code/s1/azure-iot-sdk-c/tools/CACertificates/private/client.key.pem";
    client_socket_config.tls.trusted_certificate_file = "/home/ewertons/code/s1/azure-iot-sdk-c/tools/CACertificates/certs/chain.ca.cert.pem";

    socket_config_t server_socket_config = socket_get_default_secure_server_config();
    server_socket_config.local.port = port;
    server_socket_config.tls.certificate_file = "/home/ewertons/code/s1/azure-iot-sdk-c/tools/CACertificates/certs/server.cert.pem";
    server_socket_config.tls.private_key_file = "/home/ewertons/code/s1/azure-iot-sdk-c/tools/CACertificates/private/server.key.pem";

    socket_t client_socket;
    socket_t server_listen_socket;
    socket_t server_socket;

    assert_int_equal(socket_init(&client_socket, &client_socket_config), ok);
    assert_int_equal(socket_init(&server_listen_socket, &server_socket_config), ok);

    task_t* accept_task = socket_accept_async(&server_listen_socket, &server_socket);
    assert_non_null(accept_task);

    // openssl s_server -port 5577 -cert /home/ewertons/code/s1/azure-iot-sdk-c/tools/CACertificates/certs/server.cert.pem -key /home/ewertons/code/s1/azure-iot-sdk-c/tools/CACertificates/private/server.key.pem
    assert_int_equal(socket_connect(&client_socket), ok);

    // for(int i = 0; i < 20000; i++)
    // {
    //     usleep(100);
    // }

    // assert_true(task_wait(accept_task));

    for (int i = 0; i < 10; i++)
    {
      assert_int_equal(socket_write(&client_socket, span_from_str_literal("BLA!")), ok);
    }

    assert_int_equal(socket_deinit(&client_socket), ok);
    assert_int_equal(socket_deinit(&server_socket), ok);
}

int test_socket()
{
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(socket_client_and_server_success),
  };

  return cmocka_run_group_tests_name("socket_client_and_server_success", tests, NULL, NULL);
}
