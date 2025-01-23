#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#include "tests.h"
#include "socket.h"

#include <unistd.h> 

#define CLIENT_CERT_PATH "TBD"
#define CLIENT_PK_PATH "TBD"
#define SERVER_CERT_PATH "TBD"
#define SERVER_PK_PATH "TBD"
#define CA_CHAIN_PATH "TBD"

static void socket_client_and_server_success(void** state)
{
    (void)state;
    int port = 5578;
    uint8_t raw_write_buffer[10];
    uint8_t raw_read_buffer[10];
    span_t read_buffer = span_from_memory(raw_read_buffer);

    socket_config_t client_socket_config = socket_get_default_secure_client_config();
    client_socket_config.remote.hostname = span_from_str_literal("localhost");
    client_socket_config.remote.port = port;
    client_socket_config.tls.certificate_file = CLIENT_CERT_PATH;
    client_socket_config.tls.private_key_file = CLIENT_PK_PATH;
    client_socket_config.tls.trusted_certificate_file = CA_CHAIN_PATH;

    socket_config_t server_socket_config = socket_get_default_secure_server_config();
    server_socket_config.local.port = port;
    server_socket_config.tls.certificate_file = SERVER_CERT_PATH;
    server_socket_config.tls.private_key_file = SERVER_PK_PATH;

    socket_t client_socket;
    socket_t server_listen_socket;
    socket_t server_socket;

    assert_int_equal(socket_init(&client_socket, &client_socket_config), ok);
    assert_int_equal(socket_init(&server_listen_socket, &server_socket_config), ok);

    task_t* accept_task = socket_accept_async(&server_listen_socket, &server_socket);
    assert_non_null(accept_task);

    // openssl s_client -host localhost -port 5578  -verifyCAfile code/s1/http_listener/deps/common-lib-c/tests/scripts/certs/chain.ca.cert.pem
    // openssl s_server -port 5577 -cert /home/ewertons/code/s1/azure-iot-sdk-c/tools/CACertificates/certs/server.cert.pem -key /home/ewertons/code/s1/azure-iot-sdk-c/tools/CACertificates/private/server.key.pem
    assert_int_equal(socket_connect(&client_socket), ok);

    // for(int i = 0; i < 20000; i++)
    // {
    //     usleep(100);
    // }

    // assert_true(task_wait(accept_task));

    // for(int i = 0; i < 200000; i++)
    // {
    //     usleep(100);
    // }

    for (int i = 0; i < 10; i++)
    {
      span_t bytes_read;
      span_t write_buffer = span_from_memory(raw_write_buffer);
      write_buffer = span_copy_int32(write_buffer, i, NULL);

      assert_int_not_equal(0, span_get_size(write_buffer));
      assert_int_equal(socket_write(&client_socket, write_buffer), ok);
      assert_int_equal(socket_read(&server_socket, read_buffer, &bytes_read), ok);
      assert_int_equal(span_get_size(bytes_read), span_get_size(write_buffer));
      assert_memory_equal(span_get_ptr(bytes_read), span_get_ptr(write_buffer), span_get_size(bytes_read));
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
