#include "hyperion_client.h"
#include "log.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "hyperion_reply_reader.h"
#include "hyperion_request_builder.h"

static int _connect_unix_socket(const char* hostname);
static int _connect_inet_socket(const char* hostname, int port);
static int _send_message(const void* buffer, size_t size);
static bool _parse_reply(hyperionnet_Reply_table_t reply);

static int sockfd;
static bool _registered = false;
static int _priority = 0;
static const char* _origin = NULL;
static bool _connected = false;
unsigned char recvBuff[1024];

int hyperion_client(const char* origin, const char* hostname, int port, bool unix_socket, int priority)
{
    _origin = origin;
    _priority = priority;
    _connected = false;
    _registered = false;
    sockfd = 0;

    if (unix_socket) {
        return _connect_unix_socket(hostname);
    } else {
        return _connect_inet_socket(hostname, port);
    }
}

int hyperion_read()
{
    if (!sockfd)
        return -1;
    uint8_t headbuff[4];
    int n = read(sockfd, headbuff, 4);
    uint32_t messageSize = ((headbuff[0] << 24) & 0xFF000000) | ((headbuff[1] << 16) & 0x00FF0000) | ((headbuff[2] << 8) & 0x0000FF00) | ((headbuff[3]) & 0x000000FF);
    if (n < 0 || messageSize >= sizeof(recvBuff))
        return -1;
    n = read(sockfd, recvBuff, messageSize);
    if (n < 0)
        return -1;
    _parse_reply(hyperionnet_Reply_as_root(recvBuff));
    return 0;
}

int hyperion_destroy()
{
    if (!sockfd)
        return 0;
    close(sockfd);
    sockfd = 0;
    return 0;
}

int hyperion_set_image(const unsigned char* image, int width, int height)
{
    flatbuffers_builder_t B;
    flatcc_builder_init(&B);
    flatbuffers_uint8_vec_ref_t imgData = flatcc_builder_create_type_vector(&B, image, width * height * 3);
    hyperionnet_RawImage_ref_t rawImg = hyperionnet_RawImage_create(&B, imgData, width, height);
    hyperionnet_Image_ref_t imageReq = hyperionnet_Image_create(&B, hyperionnet_ImageType_as_RawImage(rawImg), -1);
    hyperionnet_Request_create_as_root(&B, hyperionnet_Command_as_Image(imageReq));
    size_t size;
    void* buf = flatcc_builder_finalize_buffer(&B, &size);
    int ret = _send_message(buf, size);
    free(buf);
    flatcc_builder_clear(&B);
    return ret;
}

int hyperion_set_register(const char* origin, int priority)
{
    if (!sockfd)
        return 0;
    flatbuffers_builder_t B;
    flatcc_builder_init(&B);
    hyperionnet_Register_ref_t registerReq = hyperionnet_Register_create(&B, flatcc_builder_create_string_str(&B, origin), priority);
    hyperionnet_Request_create_as_root(&B, hyperionnet_Command_as_Register(registerReq));

    size_t size;
    void* buf = flatcc_builder_finalize_buffer(&B, &size);
    uint8_t header[4] = {
        (uint8_t)((size >> 24) & 0xFF),
        (uint8_t)((size >> 16) & 0xFF),
        (uint8_t)((size >> 8) & 0xFF),
        (uint8_t)(size & 0xFF),
    };

    // write message
    int ret = 0;
    if (write(sockfd, header, 4) < 0)
        ret = -1;
    if (write(sockfd, buf, size) < 0)
        ret = -1;

    free(buf);
    flatcc_builder_clear(&B);
    return ret;
}

int _send_message(const void* buffer, size_t size)
{
    if (!sockfd)
        return 0;
    if (!_connected)
        return 0;

    if (!_registered) {
        return hyperion_set_register(_origin, _priority);
    }

    const uint8_t header[] = {
        (uint8_t)((size >> 24) & 0xFF),
        (uint8_t)((size >> 16) & 0xFF),
        (uint8_t)((size >> 8) & 0xFF),
        (uint8_t)(size & 0xFF)
    };

    // write message
    int ret = 0;
    if (write(sockfd, header, 4) < 0)
        ret = -1;
    if (write(sockfd, buffer, size) < 0)
        ret = -1;
    return ret;
}

bool _parse_reply(hyperionnet_Reply_table_t reply)
{
    if (!hyperionnet_Reply_error(reply)) {
        // no error set must be a success or registered or video
        int32_t videoMode = hyperionnet_Reply_video(reply);
        int32_t registered = hyperionnet_Reply_registered(reply);
        if (videoMode != -1) {
            // We got a video reply.
            // printf("set video mode %d\n", videoMode);
            return true;
        }

        // We got a registered reply.
        if (registered == _priority) {
            _registered = true;
        }

        return true;
    } else {
        flatbuffers_string_t error = hyperionnet_Reply_error(reply);
        WARN("Error received from server: %s", error);
    }

    return false;
}

int _connect_inet_socket(const char* hostname, int port)
{
    struct sockaddr_in serv_addr_in;

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        WARN("Could not create socket: %s", strerror(errno));
        return 1;
    }
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout,
            sizeof(timeout))
        < 0) {
        WARN("setsockopt(SO_SNDTIMEO) failed: %s", strerror(errno));
        return 1;
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout,
            sizeof(timeout))
        < 0) {
        WARN("setsockopt(SO_RCVTIMEO) failed: %s", strerror(errno));
        return 1;
    }

    memset(&serv_addr_in, 0, sizeof(serv_addr_in));

    serv_addr_in.sin_family = AF_INET;
    serv_addr_in.sin_port = htons(port);

    if (inet_pton(AF_INET, hostname, &serv_addr_in.sin_addr) <= 0) {
        WARN("inet_pton error occured (hostname: %s): %s", hostname, strerror(errno));
        return 1;
    }

    if (connect(sockfd, (struct sockaddr*)&serv_addr_in, sizeof(serv_addr_in)) < 0) {
        WARN("connect() to %s:%d failed: %s", hostname, port, strerror(errno));
        return 1;
    }
    _connected = true;

    return 0;
}

int _connect_unix_socket(const char* hostname)
{
    struct sockaddr_un serv_addr_un;

    if ((sockfd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        WARN("Could not create socket: %s", strerror(errno));
        return 1;
    }
    struct timeval timeout;
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char*)&timeout,
            sizeof(timeout))
        < 0) {
        WARN("setsockopt(SO_SNDTIMEO) failed: %s", strerror(errno));
        return 1;
    }

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout,
            sizeof(timeout))
        < 0) {
        WARN("setsockopt(SO_RCVTIMEO) failed: %s", strerror(errno));
        return 1;
    }

    memset(&serv_addr_un, 0, sizeof(serv_addr_un));

    serv_addr_un.sun_family = AF_LOCAL;
    strcpy(serv_addr_un.sun_path, hostname);

    if (connect(sockfd, (struct sockaddr*)&serv_addr_un, sizeof(serv_addr_un)) < 0) {
        WARN("connect() to unix socket %s failed: %s", hostname, strerror(errno));
        return 1;
    }
    _connected = true;

    return 0;
}