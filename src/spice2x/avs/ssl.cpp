#define SECURITY_WIN32

#include <cstring>
#include <iostream>
#include <vector>

#include <windows.h>
#include <schnlsp.h>
#include <sspi.h>

#include "util/logging.h"

#include "ssl.h"
#include "core.h"

#ifndef SP_PROT_TLS1_1_CLIENT
#define SP_PROT_TLS1_1_CLIENT 0x00000200
#endif

#ifndef SP_PROT_TLS1_2_CLIENT
#define SP_PROT_TLS1_2_CLIENT 0x00000800
#endif

#define IO_BUF_INITIAL_CAPACITY 524288

namespace avs {

    // These structures are initialized by AVS and our functions are handed pointers to
    // them, do not use C++ objects in them!

    struct io_buf {
        uint8_t *bytes;
        size_t capacity;
        size_t pos;
        size_t limit;
    };

    struct core::avs_net_proto_desc_work {
        void *proto_desc_work;
        uint32_t padding[16];
    };
    struct core::avs_net_sock_desc_work {
        void *proto_desc_work;
        int sock_fd;
        uint32_t address;
        avs_net_port_t port;
        const char *hostname;
        uint32_t send_timeout;
        uint32_t recv_timeout;
        bool non_blocking;
        CtxtHandle security_context;
        CredHandle credentials_handle;
        SecPkgContext_StreamSizes sizes;
        struct io_buf send_buf;
        struct io_buf recv_buf;
        uint8_t *recv_plaintext_tail;
        size_t recv_plaintext_count;
        uint8_t *recv_ciphertext_tail;
        size_t recv_ciphertext_count;
        bool got_shutdown;
        uint32_t padding[16];
    };

    namespace ssl {
        using avs::core::avs_iovec;
        using avs::core::avs_net_port_t;
        using avs::core::avs_net_poll_fd;
        using avs::core::avs_net_poll_fd_opaque;
        using avs::core::avs_net_pollfds_size_t;
        using avs::core::avs_net_proto_desc_work;
        using avs::core::avs_net_size_t;
        using avs::core::avs_net_sock_desc_work;
        using avs::core::avs_net_timeout_t;
        using avs::core::AVS_ERROR_CLASS_NET;
        using avs::core::AVS_ERROR_SUBCLASS_NET_TIMEOUT;
        using avs::core::AVS_ERROR_SUBCLASS_SC_BADMSG;
        using avs::core::AVS_ERROR_SUBCLASS_SC_INVAL;
        using avs::core::AVS_NET_POLL_POLLIN;
        using avs::core::AVS_NET_PROTOCOL_SSL_TLS_V1_1;
        using avs::core::AVS_SO_SNDTIMEO;
        using avs::core::AVS_SO_RCVTIMEO;
        using avs::core::AVS_SO_NONBLOCK;
        using avs::core::AVS_SO_SSL_PROTOCOL;
        using avs::core::AVS_SO_SSL_VERIFY_CN;
        using avs::core::T_NET_PROTO_ID_DEFAULT;

        enum tls_recv_payload {
            TLS_RECV_PAYLOAD_NONE,
            TLS_RECV_PAYLOAD_DATA,
            TLS_RECV_PAYLOAD_SHUTDOWN_TOKEN,
        };

        static constexpr size_t alignment = 16;

        static int io_buf_init(struct io_buf *buf, size_t initial_capacity) {
            uint8_t *tmp_bytes = nullptr;
            int err = 0;

            if (buf == nullptr) {
                err = -1;
                goto arg_fail;
            }

            tmp_bytes = reinterpret_cast<uint8_t *>(_aligned_malloc(initial_capacity, alignment));

            if (tmp_bytes == nullptr) {
                err = -1;

                goto alloc_fail;
            }

            buf->bytes = tmp_bytes;
            buf->capacity = initial_capacity;
            buf->pos = 0;
            buf->limit = initial_capacity;

            return 0;

alloc_fail:
            memset(buf, 0, sizeof(*buf));

arg_fail:
            return err;
        }

        static inline int io_buf_validate(const struct io_buf *buf) {
            if (buf == nullptr || buf->bytes == nullptr) {
                return -1;
            }

            if (buf->pos > buf->limit || buf->limit > buf->capacity) {
                return -1;
            }

            return 0;
        }

        static int io_buf_grow_to(struct io_buf *buf, size_t min_capacity) {
            auto err = io_buf_validate(buf);

            if (err != 0) {
                return err;
            }

            auto tmp_capacity = buf->capacity;

            while (tmp_capacity < min_capacity) {
                tmp_capacity *= 2;
            }

            if (tmp_capacity <= buf->capacity) {
                return -1;
            }

            auto tmp_bytes = reinterpret_cast<uint8_t *>(_aligned_malloc(tmp_capacity, alignment));

            if (tmp_bytes == NULL) {
                return -1;
            }

            memcpy(tmp_bytes, buf->bytes, buf->pos);

            _aligned_free(buf->bytes);

            buf->bytes = tmp_bytes;
            buf->capacity = tmp_capacity;

            return 0;
        }

        static int io_buf_grow(struct io_buf *buf) {
            auto err = io_buf_validate(buf);

            if (err != 0) {
                return err;
            }

            return io_buf_grow_to(buf, buf->capacity * 2);
        }

        static int io_buf_append(struct io_buf *buf, const void *src, size_t *nbytes) {
            auto err = io_buf_validate(buf);

            if (err != 0) {
                return err;
            }

            if (src == nullptr || nbytes == nullptr) {
                return -1;
            }

            if (*nbytes > buf->limit - buf->pos) {
                *nbytes = buf->limit - buf->pos;
            }

            memcpy(&buf->bytes[buf->pos], src, *nbytes);

            buf->pos += *nbytes;

            return 0;
        }

        static int io_buf_flip(struct io_buf *buf) {
            auto err = io_buf_validate(buf);

            if (err != 0) {
                return err;
            }

            buf->limit = buf->pos;
            buf->pos = 0;

            return 0;
        }

        static void io_buf_finish(struct io_buf *buf) {
            if (buf != nullptr && buf->bytes != nullptr) {
                _aligned_free(buf->bytes);

                buf->bytes = nullptr;
                buf->capacity = 0;
                buf->pos = 0;
                buf->limit = 0;
            }
        }

        static int impl_socket_recv(struct avs_net_sock_desc_work *work, struct io_buf *buf) {
            int result = 0;
            int err = 0;

            if (work == nullptr || work->sock_fd < 0) {
                return -1;
            }

            err = io_buf_validate(buf);

            if (err != 0) {
                return -1;
            }

            if (buf->pos == buf->limit) {
                return -1;
            }

            result = core::avs_net_recv(work->sock_fd, &buf->bytes[buf->pos], buf->limit - buf->pos);

            if (result < 0) {
                log_warning("avs::ssl", "avs_net_recv failed: 0x{:08x}", result);

                return -1;
            }

            if (result == 0) {
                log_misc("avs::ssl", "connection closed");

                return -2;
            }

            buf->pos += result;

            return 0;
        }

        static int impl_socket_recv_all(
                struct avs_net_sock_desc_work *work,
                struct io_buf *buf,
                uint32_t recv_timeout)
        {
            uint8_t old_non_blocking_value = 0;
            uint8_t non_blocking = 0;
            avs_net_size_t old_non_blocking_value_size = 0;
            struct avs_net_poll_fd poll_fds[1] {};
            int ret = 0;

            if (work == nullptr || work->sock_fd < 0) {
                return -1;
            }

            auto err = io_buf_validate(buf);

            if (err != 0) {
                return -1;
            }

            if (buf->pos == buf->limit) {
                return -1;
            }

            poll_fds[0].socket = work->sock_fd;
            poll_fds[0].events = AVS_NET_POLL_POLLIN;

            non_blocking = 1;
            old_non_blocking_value_size = sizeof(old_non_blocking_value);

            core::avs_net_getsockopt(work->sock_fd, AVS_SO_NONBLOCK, &old_non_blocking_value, &old_non_blocking_value_size);
            core::avs_net_setsockopt(work->sock_fd, AVS_SO_NONBLOCK, &non_blocking, sizeof(non_blocking));

            while (true) {
                auto result = core::avs_net_poll(poll_fds, std::size(poll_fds), recv_timeout);

                if (result < 0) {
                    log_warning("avs::ssl", "avs_net_poll failed: 0x{:08x}", result);

                    ret = -1;

                    goto out;
                }

                if (!result) {
#if 0
                    log_warning("avs::ssl", "socket timeout, no data received after {} milliseconds", recv_timeout);
#endif

                    goto poll_succeeded_or_empty;
                }

                if (poll_fds[0].r_events & AVS_NET_POLL_POLLIN) {
                    result = core::avs_net_recv(work->sock_fd, &buf->bytes[buf->pos], buf->limit - buf->pos);

                    if (result < 0) {
                        break;
                    }

                    if (result == 0) {
                        log_misc("avs::ssl", "connection closed");

                        ret = -1;

                        goto out;
                    }

#if 0
                    log_warning("avs::ssl", "socket({}) got {} bytes during handshake", work->sock_fd, result);
#endif

                    buf->pos += result;
                }
            }

poll_succeeded_or_empty:
            ret = 0;

out:
            core::avs_net_setsockopt(work->sock_fd, AVS_SO_NONBLOCK, &old_non_blocking_value, sizeof(old_non_blocking_value));

            return ret;
        }

        static int impl_socket_send(struct avs_net_sock_desc_work *work, struct io_buf *buf) {
            if (work == nullptr || work->sock_fd < 0) {
                return -1;
            }

            auto err = io_buf_validate(buf);

            if (err != 0) {
                return -1;
            }

            auto result = core::avs_net_send(work->sock_fd, &buf->bytes[buf->pos], buf->limit - buf->pos);

#if 0
            log_warning("avs::ssl", "socket({}) sending {} bytes", work->sock_fd, buf->limit - buf->pos);
#endif

            if (result != static_cast<int>(buf->limit - buf->pos)) {
                log_warning("avs::ssl", "avs_net_send failed: 0x{:08x}", result);

                return -1;
            }

            buf->pos = buf->limit;

            return 0;
        }

        static int tls_begin_buffer(struct avs_net_sock_desc_work *work) {
            auto status = QueryContextAttributes(&work->security_context, SECPKG_ATTR_STREAM_SIZES, &work->sizes);

            if (status != SEC_E_OK) {
                log_warning("avs::ssl", "QueryContextAttributes failed: {}", FMT_HRESULT(status));

                return -1;
            }

            auto total = work->sizes.cbHeader + work->sizes.cbTrailer + work->sizes.cbMaximumMessage;

            auto err = io_buf_grow_to(&work->send_buf, total);

            if (err != 0) {
                return -1;
            }

            return 0;
        }

        static int tls_send_chunk(struct avs_net_sock_desc_work *work, const uint8_t *bytes, size_t nbytes) {
            int err = 0;
            SecBuffer send_bufs[4];
            SecBufferDesc send_vec;
            SECURITY_STATUS status = 0;

            send_vec.ulVersion = SECBUFFER_VERSION;
            send_vec.pBuffers = send_bufs;
            send_vec.cBuffers = std::size(send_bufs);

            send_bufs[0].BufferType = SECBUFFER_STREAM_HEADER;
            send_bufs[0].pvBuffer = &work->send_buf.bytes[0];
            send_bufs[0].cbBuffer = work->sizes.cbHeader;

            send_bufs[1].BufferType = SECBUFFER_DATA;
            send_bufs[1].pvBuffer = &work->send_buf.bytes[work->sizes.cbHeader];
            send_bufs[1].cbBuffer = nbytes;

            send_bufs[2].BufferType = SECBUFFER_STREAM_TRAILER;
            send_bufs[2].pvBuffer = &work->send_buf.bytes[work->sizes.cbHeader + nbytes];
            send_bufs[2].cbBuffer = work->sizes.cbTrailer;

            send_bufs[3].BufferType = SECBUFFER_EMPTY;
            send_bufs[3].pvBuffer = nullptr;
            send_bufs[3].cbBuffer = 0;

            memcpy(send_bufs[1].pvBuffer, bytes, nbytes);

            status = EncryptMessage(&work->security_context, 0, &send_vec, 0);

            if (status != SEC_E_OK) {
                log_warning("avs::ssl", "EncryptMessage failed: {}", FMT_HRESULT(status));

                return -1;
            }

            work->send_buf.pos = 0;
            work->send_buf.limit = send_bufs[0].cbBuffer + send_bufs[1].cbBuffer + send_bufs[2].cbBuffer;

            err = impl_socket_send(work, &work->send_buf);

            if (err != 0) {
                log_warning("avs::ssl", "impl_socket_send failed: {}", err);
            }

            return 0;
        }

        static int tls_send(struct avs_net_sock_desc_work *work, struct io_buf *buf) {
            size_t chunk_size = 0;
            int err = 0;

            err = io_buf_validate(buf);

            if (err != 0) {
                return err;
            }

            if (buf->pos == buf->limit) {
                return -1;
            }

            while (buf->pos < buf->limit) {
                chunk_size = buf->limit - buf->pos;

                if (chunk_size > work->sizes.cbMaximumMessage) {
                    chunk_size = work->sizes.cbMaximumMessage;
                }

                err = tls_send_chunk(work, &buf->bytes[buf->pos], chunk_size);

                if (err != 0) {
                    return err;
                }

                buf->pos += chunk_size;
            }

            return err;
        }

        static int tls_recv_dequeue_plaintext(struct avs_net_sock_desc_work *work, struct io_buf *buf) {
            size_t tail_nbytes = 0;
            int err = 0;

            tail_nbytes = work->recv_plaintext_count;

            err = io_buf_append(buf, work->recv_plaintext_tail, &tail_nbytes);

            if (err != 0) {
                return err;
            }

            SecureZeroMemory(work->recv_plaintext_tail, tail_nbytes);

            work->recv_plaintext_count -= tail_nbytes;
            work->recv_plaintext_tail += tail_nbytes;

            return 0;
        }

        static int tls_recv_common(struct avs_net_sock_desc_work *work, enum tls_recv_payload *payload) {
            int err = 0;
            SecBuffer recv_bufs[4];
            SecBufferDesc recv_vec;
            SECURITY_STATUS status = SEC_E_OK;

            // Consolidate any leftover ciphertext at the start of the buffer
            memmove(work->recv_buf.bytes, work->recv_ciphertext_tail, work->recv_ciphertext_count);

            work->recv_buf.pos = work->recv_ciphertext_count;
            work->recv_buf.limit = work->recv_buf.capacity;
            work->recv_ciphertext_tail = nullptr;
            work->recv_ciphertext_count = 0;

            recv_vec.ulVersion = SECBUFFER_VERSION;
            recv_vec.cBuffers = std::size(recv_bufs);
            recv_vec.pBuffers = recv_bufs;

            while (true) {
                recv_bufs[0].BufferType = SECBUFFER_DATA;
                recv_bufs[0].pvBuffer = work->recv_buf.bytes;
                recv_bufs[0].cbBuffer = work->recv_buf.pos;

                for (size_t i = 1; i < std::size(recv_bufs); i++) {
                    recv_bufs[i].BufferType = SECBUFFER_EMPTY;
                    recv_bufs[i].pvBuffer = nullptr;
                    recv_bufs[i].cbBuffer = 0;
                }

                status = DecryptMessage(&work->security_context, &recv_vec, 0, nullptr);

                if (status != SEC_E_INCOMPLETE_MESSAGE) {
                    break;
                }

                err = impl_socket_recv(work, &work->recv_buf);

                if (err != 0) {
                    if (err != -2) {
                        log_warning("avs::ssl", "impl_socket_recv failed: {}", err);
                    }

                    return err;
                }
            }

            // Deal with whatever it is we received
            switch (status) {
                case SEC_E_OK:

                    // Walk buffers and mark up the plaintext and ciphertext span within our
                    // own io_buf as appropriate
                    for (size_t i = 0; i < std::size(recv_bufs); i++) {
                        switch (recv_bufs[i].BufferType) {
                            case SECBUFFER_DATA:
                                work->recv_plaintext_tail = reinterpret_cast<uint8_t *>(recv_bufs[i].pvBuffer);
                                work->recv_plaintext_count = recv_bufs[i].cbBuffer;
                                break;

                            case SECBUFFER_EXTRA:
                                work->recv_ciphertext_tail = reinterpret_cast<uint8_t *>(recv_bufs[i].pvBuffer);
                                work->recv_ciphertext_count = recv_bufs[i].cbBuffer;
                                break;

                            default:
                                break;
                        }
                    }

                    *payload = TLS_RECV_PAYLOAD_DATA;

                    return 0;

                case SEC_I_CONTEXT_EXPIRED:
                    *payload = TLS_RECV_PAYLOAD_SHUTDOWN_TOKEN;

                    return 0;

                default:
                    log_warning("avs::ssl", "DecryptMessage failed: {}", FMT_HRESULT(status));

                    work->recv_buf.pos = 0;
                    work->recv_buf.limit = 0;

                    return -1;
            }
        }

        static int tls_recv(struct avs_net_sock_desc_work *work, struct io_buf *buf) {
            enum tls_recv_payload payload = TLS_RECV_PAYLOAD_NONE;
            int err = 0;

            err = io_buf_validate(buf);

            if (err != 0) {
                return err;
            }

            if (buf->pos == buf->limit) {
                return -1;
            }

            // Try to drain any leftover plaintext in the receive buffer
            if (work->recv_plaintext_count > 0) {
                return tls_recv_dequeue_plaintext(work, buf);
            }

            err = tls_recv_common(work, &payload);

            if (err != 0) {
                return err;
            }

            switch (payload) {
                case TLS_RECV_PAYLOAD_DATA:
                    return tls_recv_dequeue_plaintext(work, buf);

                case TLS_RECV_PAYLOAD_SHUTDOWN_TOKEN:
                    work->got_shutdown = true;

                    return -2;

                default:
                    return -1;
            }
        }

        static int tls_recv_shutdown(struct avs_net_sock_desc_work *work) {
            enum tls_recv_payload payload = TLS_RECV_PAYLOAD_NONE;

            if (work->recv_plaintext_count > 0) {
                return -1;
            }

            if (work->got_shutdown) {
                return 0;
            }

            auto err = tls_recv_common(work, &payload);

            if (err != 0) {
                return err;
            }

            if (payload != TLS_RECV_PAYLOAD_SHUTDOWN_TOKEN) {
                return -1;
            }

            return 0;
        }

        static int tls_send_shutdown(struct avs_net_sock_desc_work *work) {
            static uint32_t tls_shutdown_token = SCHANNEL_SHUTDOWN;
            ULONG attrs = 0;
            SecBuffer cmd_buf;
            SecBufferDesc cmd_vec;
            SECURITY_STATUS status = SEC_E_OK;

            cmd_vec.ulVersion = SECBUFFER_VERSION;
            cmd_vec.pBuffers = &cmd_buf;
            cmd_vec.cBuffers = 1;

            cmd_buf.BufferType = SECBUFFER_TOKEN;
            cmd_buf.pvBuffer = static_cast<void *>(&tls_shutdown_token);
            cmd_buf.cbBuffer = sizeof(tls_shutdown_token);

            status = ApplyControlToken(&work->security_context, &cmd_vec);

            if (status != SEC_E_OK) {
                log_warning("avs::ssl", "{}: ApplyControlToken failed: {}", __func__, FMT_HRESULT(status));

                return -1;
            }

            cmd_vec.ulVersion = SECBUFFER_VERSION;
            cmd_vec.pBuffers = &cmd_buf;
            cmd_vec.cBuffers = 1;

            cmd_buf.BufferType = SECBUFFER_TOKEN;
            cmd_buf.pvBuffer = work->send_buf.bytes;
            cmd_buf.cbBuffer = work->send_buf.capacity;

            log_info("avs::ssl", "calling InitializeSecurityContextA to generate token");

            status = InitializeSecurityContextA(
                    &work->credentials_handle,
                    &work->security_context,
                    const_cast<SEC_CHAR *>(work->hostname),
                    0,
                    0,
                    0,
                    nullptr,
                    0,
                    nullptr,
                    &cmd_vec,
                    &attrs,
                    nullptr);

            if (status != SEC_E_OK) {
                log_warning("avs::ssl", "{}: InitializeSecurityContextA failed: {}", __func__, FMT_HRESULT(status));

                return -1;
            }

            work->send_buf.pos = 0;
            work->send_buf.limit = cmd_buf.cbBuffer;

            auto err = impl_socket_send(work, &work->send_buf);

            if (err != 0) {
                log_warning("avs::ssl", "impl_socket_send failed: {}", err);
            }

            return err;
        }

        static int ssl_protocol_initialize(struct avs_net_proto_desc_work *work) {
            return 0;
        }

        static int ssl_protocol_finalize(struct avs_net_proto_desc_work *work) {
            return 0;
        }

        static int ssl_allocate_socket(struct avs_net_sock_desc_work *work) {
            SCHANNEL_CRED credentials;
            CredHandle credentials_handle;

            memset(work, 0, sizeof(*work));
            memset(&credentials, 0, sizeof(credentials));

            credentials.dwVersion = SCHANNEL_CRED_VERSION;
            credentials.cCreds = 0;
            credentials.paCred = nullptr;
            credentials.dwFlags = SCH_CRED_AUTO_CRED_VALIDATION | SCH_CRED_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT | SCH_CRED_IGNORE_REVOCATION_OFFLINE;
            credentials.grbitEnabledProtocols = SP_PROT_TLS1_CLIENT | SP_PROT_TLS1_1_CLIENT | SP_PROT_TLS1_2_CLIENT;

            auto status = AcquireCredentialsHandleA(
                    nullptr,
                    const_cast<SEC_CHAR *>(UNISP_NAME),
                    SECPKG_CRED_OUTBOUND,
                    nullptr,
                    &credentials,
                    nullptr,
                    nullptr,
                    &credentials_handle,
                    nullptr);

            if (status != SEC_E_OK) {
                log_warning("avs::ssl", "AcquireCredentialsHandleA failed: {}", status);

                return core::avs_error_make(AVS_ERROR_CLASS_NET, AVS_ERROR_SUBCLASS_SC_INVAL);
            }

            work->credentials_handle = credentials_handle;

            return 0;
        }

        static void ssl_free_socket(struct avs_net_sock_desc_work *work) {
            if (work != nullptr) {
                if (work->hostname != nullptr) {
                    free(const_cast<void *>(reinterpret_cast<const void *>(work->hostname)));

                    work->hostname = nullptr;
                }

                FreeCredentialsHandle(&work->credentials_handle);
            }
        }

        static int ssl_initialize_socket(struct avs_net_sock_desc_work *work) {
            auto sock_fd = core::avs_net_socket(T_NET_PROTO_ID_DEFAULT);

            if (sock_fd > 0) {
                work->sock_fd = sock_fd;

                return 1;
            }

            return 0;
        }

        static void ssl_finalize_socket(struct avs_net_sock_desc_work *work) {
        }

        static int ssl_setsockopt(
                struct avs_net_sock_desc_work *work,
                unsigned int option_name,
                const void *option_value,
                avs_net_size_t option_len)
        {
            // SSL specific options here
            switch (option_name) {
                case AVS_SO_SSL_PROTOCOL:
                    log_info("avs::ssl", "AVS_SO_SSL_PROTOCOL = {}", *reinterpret_cast<const int *>(option_value));

                    return 0;

                case AVS_SO_SSL_VERIFY_CN:
                    log_info("avs::ssl", "AVS_SO_SSL_VERIFY_CN = {}", *reinterpret_cast<const int *>(option_value));

                    return 0;

                default:
                    break;
            }

            // Generic network options here
            auto result = core::avs_net_setsockopt(work->sock_fd, option_name, option_value, option_len);

            if (result < 0) {
                return result;
            }

            switch (option_name) {
                case AVS_SO_SNDTIMEO:
                    work->send_timeout = *reinterpret_cast<const avs_net_timeout_t *>(option_value);

                    break;

                case AVS_SO_RCVTIMEO:
                    work->recv_timeout = *reinterpret_cast<const avs_net_timeout_t *>(option_value);

                    break;

                case AVS_SO_NONBLOCK:
                    work->non_blocking = *reinterpret_cast<const uint8_t *>(option_value);

                    break;

                default:
                    break;
            }

            return 0;
        }

        static int ssl_socket_getsockopt(
                struct avs_net_sock_desc_work *work,
                unsigned int option_name,
                void *option_value,
                avs_net_size_t *option_len)
        {
            switch (option_name) {
                case AVS_SO_SNDTIMEO:
                    *reinterpret_cast<avs_net_timeout_t *>(option_value) = work->send_timeout;
                    *option_len = sizeof(avs_net_timeout_t);

                    break;

                case AVS_SO_RCVTIMEO:
                    *reinterpret_cast<avs_net_timeout_t *>(option_value) = work->recv_timeout;
                    *option_len = sizeof(avs_net_timeout_t);

                    break;

                case AVS_SO_NONBLOCK:
                    *reinterpret_cast<uint8_t *>(option_value) = work->non_blocking;
                    *option_len = sizeof(uint8_t);

                    break;

                case AVS_SO_SSL_PROTOCOL:
                    *reinterpret_cast<uint32_t *>(option_value) = AVS_NET_PROTOCOL_SSL_TLS_V1_1;
                    *option_len = sizeof(uint32_t);

                    break;

                case AVS_SO_SSL_VERIFY_CN:
                    *reinterpret_cast<uint8_t *>(option_value) = 0;
                    *option_len = sizeof(uint8_t);

                    break;

                default:
                    return core::avs_net_getsockopt(work->sock_fd, option_name, option_value, option_len);
            }

            return 0;
        }

        static int ssl_socket_bind(
                struct avs_net_sock_desc_work *work,
                uint32_t address,
                avs_net_port_t port)
        {
            auto result = core::avs_net_bind(work->sock_fd, address, port);

            if (result > 0) {
                work->address = address;
                work->port = port;
            }

            return result;
        }

        static int ssl_socket_connect(
                struct avs_net_sock_desc_work *work,
                uint32_t address,
                avs_net_port_t port)
        {
            constexpr uint32_t security_context_flags = ISC_REQ_CONFIDENTIALITY |
                ISC_REQ_INTEGRITY |
                ISC_REQ_STREAM |
                ISC_REQ_SEQUENCE_DETECT |
                ISC_REQ_REPLAY_DETECT;

            SecBuffer send_bufs[2] {};
            SecBufferDesc send_vec {};
            SecBuffer recv_bufs[2] {};
            SecBufferDesc recv_vec {};
            CtxtHandle security_context;
            ULONG attributes = 0;
            SECURITY_STATUS status = SEC_E_OK;
            int result = 0;
            int err = 0;

            char hostname[256];

            memset(&security_context, 0, sizeof(security_context));

            result = core::avs_net_addrinfobyaddr(address, hostname, sizeof(hostname), 1);

            if (result < 0) {
                log_warning("avs::ssl", "avs_net_addrinfobyaddr failed: {}", FMT_HRESULT(result));

                return result;
            }

            work->hostname = strdup(hostname);

            err = io_buf_init(&work->send_buf, IO_BUF_INITIAL_CAPACITY);

            if (err != 0) {
                goto send_buf_fail;
            }

            err = io_buf_init(&work->recv_buf, IO_BUF_INITIAL_CAPACITY);

            if (err != 0) {
                goto recv_buf_fail;
            }

            result = core::avs_net_connect(work->sock_fd, address, port);

            if (result < 0) {
                log_warning("avs::ssl", "avs_net_connect failed: {}", FMT_HRESULT(result));

                err = result;

                goto connect_fail;
            }

            send_vec.ulVersion = SECBUFFER_VERSION;
            send_vec.pBuffers = send_bufs;
            send_vec.cBuffers = std::size(send_bufs);

            recv_vec.ulVersion = SECBUFFER_VERSION;
            recv_vec.pBuffers = recv_bufs;
            recv_vec.cBuffers = std::size(recv_bufs);

            send_bufs[0].BufferType = SECBUFFER_TOKEN;
            send_bufs[0].pvBuffer = work->send_buf.bytes;
            send_bufs[0].cbBuffer = work->send_buf.capacity;

            send_bufs[1].BufferType = SECBUFFER_EMPTY;
            send_bufs[1].pvBuffer = nullptr;
            send_bufs[1].cbBuffer = 0;

            status = InitializeSecurityContextA(
                    &work->credentials_handle,
                    nullptr,
                    const_cast<SEC_CHAR *>(work->hostname),
                    security_context_flags,
                    0,
                    0,
                    nullptr,
                    0,
                    &security_context,
                    &send_vec,
                    &attributes,
                    nullptr);

            if (status != SEC_I_CONTINUE_NEEDED) {
                log_warning("ssl", "{}: InitializeSecurityContextA failed: {}", __func__, FMT_HRESULT(status));

                err = core::avs_error_make(AVS_ERROR_CLASS_NET, AVS_ERROR_SUBCLASS_SC_BADMSG);

                goto first_isc_fail;
            }

            while (status != SEC_E_OK) {
                switch (status) {
                    case SEC_I_CONTINUE_NEEDED:
                        work->send_buf.pos = 0;
                        work->send_buf.limit = send_bufs[0].cbBuffer;

                        // Only send data if we need to
                        if (send_bufs[0].cbBuffer > 0) {
                            err = impl_socket_send(work, &work->send_buf);

                            if (err != 0) {
                                goto loop_fail;
                            }
                        }

                        work->recv_buf.pos = 0;
                        work->recv_buf.limit = work->recv_buf.capacity;

                        err = impl_socket_recv_all(work, &work->recv_buf, 100);

                        if (err != 0) {
                            goto loop_fail;
                        }

                        break;

                    case SEC_E_INCOMPLETE_MESSAGE: 
                        if (recv_bufs[1].BufferType != SECBUFFER_MISSING) {
                            err = core::avs_error_make(AVS_ERROR_CLASS_NET, AVS_ERROR_SUBCLASS_SC_BADMSG);

                            goto loop_fail;
                        }

                        err = io_buf_grow_to(&work->recv_buf, work->recv_buf.pos + recv_bufs[1].cbBuffer);

                        if (err != 0) {
                            goto loop_fail;
                        }

                        work->recv_buf.limit = work->recv_buf.capacity;

                        err = impl_socket_recv(work, &work->recv_buf);

                        if (err != 0) {
                            goto loop_fail;
                        }

                        break;

                    case SEC_E_BUFFER_TOO_SMALL:
                        err = io_buf_grow(&work->send_buf);

                        if (err != 0) {
                            goto loop_fail;
                        }

                        break;

                    case SEC_E_WRONG_PRINCIPAL:
                    case SEC_E_CERT_EXPIRED:
                    case SEC_E_UNTRUSTED_ROOT:
                    case SEC_E_ALGORITHM_MISMATCH:
                    case SEC_E_INCOMPLETE_CREDENTIALS:
                        log_warning("avs::ssl", "unable to verify server/client certificate: {}", FMT_HRESULT(status));

                        err = core::avs_error_make(AVS_ERROR_CLASS_NET, AVS_ERROR_SUBCLASS_SC_INVAL);

                        goto loop_fail;

                    default:
                        log_warning("avs::ssl", "TLS handshake failed with status: {}", FMT_HRESULT(status));

                        err = core::avs_error_make(AVS_ERROR_CLASS_NET, AVS_ERROR_SUBCLASS_SC_BADMSG);

                        goto loop_fail;
                }

                send_bufs[0].BufferType = SECBUFFER_TOKEN;
                send_bufs[0].pvBuffer = work->send_buf.bytes;
                send_bufs[0].cbBuffer = work->send_buf.capacity;

                send_bufs[1].BufferType = SECBUFFER_EMPTY;
                send_bufs[1].pvBuffer = nullptr;
                send_bufs[1].cbBuffer = 0;

                recv_bufs[0].BufferType = SECBUFFER_TOKEN;
                recv_bufs[0].pvBuffer = work->recv_buf.bytes;
                recv_bufs[0].cbBuffer = work->recv_buf.pos;

                recv_bufs[1].BufferType = SECBUFFER_EMPTY;
                recv_bufs[1].pvBuffer = nullptr;
                recv_bufs[1].cbBuffer = 0;

                status = InitializeSecurityContextA(
                        &work->credentials_handle,
                        &security_context,
                        reinterpret_cast<SEC_CHAR *>(hostname),
                        security_context_flags,
                        0,
                        0,
                        &recv_vec,
                        0,
                        nullptr,
                        &send_vec,
                        &attributes,
                        nullptr);
            }

            log_misc("avs::ssl", "TLS handshake complete");

            work->security_context = security_context;

            tls_begin_buffer(work);

            return 0;

loop_fail:
            DeleteSecurityContext(&security_context);

connect_fail:
first_isc_fail:
            io_buf_finish(&work->recv_buf);

recv_buf_fail:
            io_buf_finish(&work->send_buf);

send_buf_fail:
            return err;
        }

        static int ssl_socket_listen(struct avs_net_sock_desc_work *work, int backlog) {
            return -1;
        }

        static int ssl_socket_accept(
                struct avs_net_sock_desc_work *work,
                void *new_sock,
                uint32_t *address,
                avs_net_port_t *port)
        {
            return -1;
        }

        static int ssl_socket_close(struct avs_net_sock_desc_work *work) {
            return core::avs_net_close(work->sock_fd);
        }

        static int ssl_socket_shutdown(struct avs_net_sock_desc_work *work, int how) {
            auto err = tls_send_shutdown(work);

            if (err != 0) {
                goto fail;
            }

            err = tls_recv_shutdown(work);

            if (err != 0) {
                goto fail;
            }

fail:
            io_buf_finish(&work->recv_buf);

            return core::avs_net_shutdown(work->sock_fd, how);
        }

        static int ssl_socket_sendtov(
                struct avs_net_sock_desc_work *work,
                const struct avs_iovec *iovec,
                int iov_count,
                uint32_t address,
                avs_net_port_t port)
        {
            struct io_buf send_io_buf;
            int err = 0;
            int result = -1;
            int bytes_sent = 0;

            for (int i = 0; i < iov_count; i++) {
                auto iovp = &iovec[i];
                auto iov_len = iovp->iov_len;

                err = io_buf_init(&send_io_buf, IO_BUF_INITIAL_CAPACITY);

                if (err != 0) {
                    goto fail;
                }

                err = io_buf_append(&send_io_buf, iovp->iov_base, &iov_len);

                if (err != 0) {
                    goto fail;
                }

                err = io_buf_flip(&send_io_buf);

                if (err != 0) {
                    goto fail;
                }

                result = tls_send(work, &send_io_buf);

                if (result < 0) {
                    log_warning("avs::ssl", "tls_send failed: {}", result);

                    return result;
                }

                // Use the original length
                bytes_sent += iovp->iov_len;

                io_buf_finish(&send_io_buf);
            }

            result = bytes_sent;

fail:
            return result;
        }

        static int ssl_socket_recvfromv(
                struct avs_net_sock_desc_work *work,
                struct avs_iovec *iovec,
                int iov_count,
                uint32_t *address,
                avs_net_port_t *port)
        {
            struct io_buf recv_io_buf;
            int result = -1;
            int bytes_received = 0;

            for (int i = 0; i < iov_count; i++) {
                auto iovp = &iovec[i];
                auto iov_len = iovp->iov_len;

                if (!iov_len) {
                    continue;
                }

                recv_io_buf.bytes = reinterpret_cast<uint8_t *>(iovp->iov_base);
                recv_io_buf.pos = 0;
                recv_io_buf.limit = iov_len;
                recv_io_buf.capacity = iov_len;

                result = tls_recv(work, &recv_io_buf);

                if (result < 0) {

                    // connection closed returns -2, convert to -1
                    if (result == -2) {
                        result = -1;
                    } else {
                        log_warning("avs::ssl", "{}: tls_recv failed: {}", __func__, result);
                    }

                    return result;
                }

                bytes_received += recv_io_buf.pos;
            }

            if (address != nullptr) {
                *address = work->address;
            }

            if (port != nullptr) {
                *port = work->port;
            }

#if 0
            log_warning("avs::ssl", "socket({}) received {} bytes", work->sock_fd, bytes_received);
#endif

            return bytes_received;
        }

        static int ssl_socket_pollfds_add(
                struct avs_net_sock_desc_work *work,
                struct avs_net_poll_fd_opaque *fds,
                avs_net_pollfds_size_t fds_size,
                struct avs_net_poll_fd *events)
        {
            if (work->sock_fd < 0) {
                return core::avs_error_make(AVS_ERROR_CLASS_NET, AVS_ERROR_SUBCLASS_NET_TIMEOUT);
            }

            return core::avs_net_pollfds_add(work->sock_fd, fds, fds_size, events);
        }

        static int ssl_socket_pollfds_get(
                struct avs_net_sock_desc_work *work,
                struct avs_net_poll_fd *events,
                struct avs_net_poll_fd_opaque *fds)
        {
            if (work->sock_fd < 0) {
                return core::avs_error_make(AVS_ERROR_CLASS_NET, AVS_ERROR_SUBCLASS_NET_TIMEOUT);
            }

            return core::avs_net_pollfds_get(work->sock_fd, events, fds);
        }

        static int ssl_socket_sockpeer(
                struct avs_net_sock_desc_work *work,
                bool peer_name,
                uint32_t *address,
                avs_net_port_t *port)
        {
            if (peer_name) {
                return core::avs_net_get_peername(work->sock_fd, address, port);
            }

            return core::avs_net_get_sockname(work->sock_fd, address, port);
        }

        static struct core::avs_net_protocol_ops ssl_protocol_ops {
            .protocol_initialize = ssl_protocol_initialize,
            .protocol_finalize   = ssl_protocol_finalize,
            .allocate_socket     = ssl_allocate_socket,
            .free_socket         = ssl_free_socket,
            .initialize_socket   = ssl_initialize_socket,
            .finalize_socket     = ssl_finalize_socket,
            .setsockopt          = ssl_setsockopt,
            .getsockopt          = ssl_socket_getsockopt,
            .bind                = ssl_socket_bind,
            .connect             = ssl_socket_connect,
            .listen              = ssl_socket_listen,
            .accept              = ssl_socket_accept,
            .close               = ssl_socket_close,
            .shutdown            = ssl_socket_shutdown,
            .sendtov             = ssl_socket_sendtov,
            .recvfromv           = ssl_socket_recvfromv,
            .pollfds_add         = ssl_socket_pollfds_add,
            .pollfds_get         = ssl_socket_pollfds_get,
            .sockpeer            = ssl_socket_sockpeer
        };

        static struct core::avs_net_protocol ssl_protocol {
            .ops             = &ssl_protocol_ops,
            .magic           = core::AVS_NET_PROTOCOL_MAGIC,
            .protocol_id     = SSL_PROTOCOL_ID,
            .proto_work_size = sizeof(struct avs_net_proto_desc_work),
            .sock_work_size  = sizeof(struct avs_net_sock_desc_work),
        };

        static struct core::avs_net_protocol_legacy ssl_protocol_legacy {
            .ops         = &ssl_protocol_ops,
            .protocol_id = SSL_PROTOCOL_ID,
            .mystery     = 0,
            .sz_work     = sizeof(struct avs_net_sock_desc_work),
        };

        void init() {
            log_info("ssl", "initializing");

            if (!core::avs_net_add_protocol) {
                log_warning("ssl", "missing optional avs imports which are required for this module to work");
                return;
            }

            core::avs_net_del_protocol(SSL_PROTOCOL_ID);

            int regist_res = 0;
            if (core::VERSION == core::AVSLEGACY) {
                core::avs_net_add_protocol_legacy(&ssl_protocol_legacy);
            } else {
                core::avs_net_add_protocol(&ssl_protocol);
            }

            if (regist_res) {
                log_fatal("ssl", "failed to register protocol");
            }
        }
    }
}
