/**
 * Copyright (c) 2018 Cornell University.
 *
 * Author: Ted Yin <tederminant@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _SALTICIDAE_CRYPTO_H
#define _SALTICIDAE_CRYPTO_H

#include "salticidae/type.h"
#include "salticidae/util.h"
#include <openssl/sha.h>
#include <openssl/ssl.h>

namespace salticidae {

class SHA256 {
    SHA256_CTX ctx;

    public:
    SHA256() { reset(); }

    void reset() {
        if (!SHA256_Init(&ctx))
            throw std::runtime_error("openssl SHA256 init error");
    }

    template<typename T>
    void update(const T &data) {
        update(reinterpret_cast<const uint8_t *>(&*data.begin()), data.size());
    }

    void update(const bytearray_t::const_iterator &it, size_t length) {
        update(&*it, length);
    }

    void update(const uint8_t *ptr, size_t length) {
        if (!SHA256_Update(&ctx, ptr, length))
            throw std::runtime_error("openssl SHA256 update error");
    }

    void _digest(bytearray_t &md) {
        if (!SHA256_Final(&*md.begin(), &ctx))
            throw std::runtime_error("openssl SHA256 error");
    }

    void digest(bytearray_t &md) {
        md.resize(32);
        _digest(md);
    }

    bytearray_t digest() {
        bytearray_t md(32);
        _digest(md);
        return std::move(md);
    }
};

class SHA1 {
    SHA_CTX ctx;

    public:
    SHA1() { reset(); }

    void reset() {
        if (!SHA1_Init(&ctx))
            throw std::runtime_error("openssl SHA1 init error");
    }

    template<typename T>
    void update(const T &data) {
        update(reinterpret_cast<const uint8_t *>(&*data.begin()), data.size());
    }

    void update(const bytearray_t::const_iterator &it, size_t length) {
        update(&*it, length);
    }

    void update(const uint8_t *ptr, size_t length) {
        if (!SHA1_Update(&ctx, ptr, length))
            throw std::runtime_error("openssl SHA1 update error");
    }

    void _digest(bytearray_t &md) {
        if (!SHA1_Final(&*md.begin(), &ctx))
            throw std::runtime_error("openssl SHA1 error");
    }

    void digest(bytearray_t &md) {
        md.resize(32);
        _digest(md);
    }

    bytearray_t digest() {
        bytearray_t md(32);
        _digest(md);
        return std::move(md);
    }
};

static thread_local const char *_password;
static inline int _tls_pem_no_passswd(char *, int, int, void *) {
    return -1;
}
static inline int _tls_pem_with_passwd(char *buf, int size, int, void *) {
    size_t _size = strlen(_password) + 1;
    if (_size > (size_t)size)
        throw SalticidaeError(SALTI_ERROR_TLS_X509);
    memmove(buf, _password, _size);
    return _size - 1;
}

class PKey {
    EVP_PKEY *key;
    friend class TLSContext;
    public:
    PKey(EVP_PKEY *key): key(key) {}
    PKey(const PKey &) = delete;
    PKey(PKey &&other): key(other.key) { other.key = nullptr; }
    
    PKey create_privkey_from_pem_file(std::string pem_fname, std::string *password = nullptr) {
        FILE *fp = fopen(pem_fname.c_str(), "r");
        EVP_PKEY *key;
        if (fp == nullptr)
            throw SalticidaeError(SALTI_ERROR_TLS_KEY);
        if (password)
        {
            _password = password->c_str();
            key = PEM_read_PrivateKey(fp, NULL, _tls_pem_with_passwd, NULL);
        }
        else
        {
            key = PEM_read_PrivateKey(fp, NULL, _tls_pem_no_passswd, NULL);
        }
        if (key == nullptr)
            throw SalticidaeError(SALTI_ERROR_TLS_KEY);
        fclose(fp);
        return PKey(key);
    }

    PKey create_privkey_from_der(const uint8_t *der, size_t size) {
        EVP_PKEY *key;
        key = d2i_AutoPrivateKey(NULL, (const unsigned char **)&der, size);
        if (key == nullptr)
            throw SalticidaeError(SALTI_ERROR_TLS_KEY);
        return PKey(key);
    }

    bytearray_t get_pubkey_der() {
        uint8_t *der;
        auto ret = i2d_PublicKey(key, &der);
        if (ret <= 0)
            throw SalticidaeError(SALTI_ERROR_TLS_KEY);
        bytearray_t res(der, der + ret);
        OPENSSL_cleanse(der, ret);
        OPENSSL_free(der);
        return std::move(res);
    }

    bytearray_t get_privkey_der() {
        uint8_t *der;
        auto ret = i2d_PrivateKey(key, &der);
        if (ret <= 0)
            throw SalticidaeError(SALTI_ERROR_TLS_KEY);
        bytearray_t res(der, der + ret);
        OPENSSL_cleanse(der, ret);
        OPENSSL_free(der);
        return std::move(res);
    }

    ~PKey() { if (key) EVP_PKEY_free(key); }
};

class X509 {
    ::X509 *x509;
    friend class TLSContext;
    public:
    X509(::X509 *x509): x509(x509) {}
    X509(const X509 &) = delete;
    X509(X509 &&other): x509(other.x509) { other.x509 = nullptr; }
    
    X509 create_from_pem_file(std::string pem_fname, std::string *password = nullptr) {
        FILE *fp = fopen(pem_fname.c_str(), "r");
        ::X509 *x509;
        if (fp == nullptr)
            throw SalticidaeError(SALTI_ERROR_TLS_X509);
        if (password)
        {
            _password = password->c_str();
            x509 = PEM_read_X509(fp, NULL, _tls_pem_with_passwd, NULL);
        }
        else
        {
            x509 = PEM_read_X509(fp, NULL, _tls_pem_no_passswd, NULL);
        }
        if (x509 == nullptr)
            throw SalticidaeError(SALTI_ERROR_TLS_X509);
        fclose(fp);
        return X509(x509);
    }

    X509 create_from_der(const uint8_t *der, size_t size) {
        ::X509 *x509;
        x509 = d2i_X509(NULL, (const unsigned char **)&der, size);
        if (x509 == nullptr)
            throw SalticidaeError(SALTI_ERROR_TLS_X509);
        return X509(x509);
    }

    PKey get_pubkey() {
        auto key = X509_get_pubkey(x509);
        if (key == nullptr)
            throw SalticidaeError(SALTI_ERROR_TLS_X509);
        return PKey(key);
    }

    ~X509() { if (x509) X509_free(x509); }
};

class TLSContext {
    SSL_CTX *ctx;
    friend class TLS;
    public:
    TLSContext(): ctx(SSL_CTX_new(TLS_method())) {
        if (ctx == nullptr)
            throw std::runtime_error("TLSContext init error");
    }

    TLSContext(const TLSContext &) = delete;
    TLSContext(TLSContext &&other): ctx(other.ctx) { other.ctx = nullptr; }

    void use_cert_file(const std::string &fname) {
        auto ret = SSL_CTX_use_certificate_file(ctx, fname.c_str(), SSL_FILETYPE_PEM);
        if (ret <= 0)
            throw SalticidaeError(SALTI_ERROR_TLS_LOAD_CERT);
    }

    void use_cert(const X509 &x509) {
        auto ret = SSL_CTX_use_certificate(ctx, x509.x509);
        if (ret <= 0)
            throw SalticidaeError(SALTI_ERROR_TLS_LOAD_CERT);
    }

    void use_privkey_file(const std::string &fname) {
        auto ret = SSL_CTX_use_PrivateKey_file(ctx, fname.c_str(), SSL_FILETYPE_PEM);
        if (ret <= 0)
            throw SalticidaeError(SALTI_ERROR_TLS_LOAD_KEY);
    }

    void use_privkey(const PKey &key) {
        auto ret = SSL_CTX_use_PrivateKey(ctx, key.key);
        if (ret <= 0)
            throw SalticidaeError(SALTI_ERROR_TLS_LOAD_KEY);
    }

    bool check_privkey() {
        return SSL_CTX_check_private_key(ctx) > 0;
    }

    ~TLSContext() { if (ctx) SSL_CTX_free(ctx); }
};

using tls_context_t = ArcObj<TLSContext>;

class TLS {
    SSL *ssl;
    public:
    TLS(const tls_context_t &ctx, int fd, bool accept): ssl(SSL_new(ctx->ctx)) {
        if (ssl == nullptr)
            throw std::runtime_error("TLS init error");
        if (!SSL_set_fd(ssl, fd))
            throw SalticidaeError(SALTI_ERROR_TLS_GENERIC);
        if (accept)
            SSL_set_accept_state(ssl);
        else
            SSL_set_connect_state(ssl);
    }

    TLS(const TLS &) = delete;
    TLS(TLS &&other): ssl(other.ssl) { other.ssl = nullptr; }

    bool do_handshake(int &want_io_type) { /* 0 for read, 1 for write */
        auto ret = SSL_do_handshake(ssl);
        if (ret == 1) return true;
        auto err = SSL_get_error(ssl, ret);
        if (err == SSL_ERROR_WANT_WRITE)
            want_io_type = 1;
        else if (err == SSL_ERROR_WANT_READ)
            want_io_type = 0;
        else
            throw SalticidaeError(SALTI_ERROR_TLS_GENERIC);
        return false;
    }

    X509 get_peer_cert() {
        ::X509 *x509 = SSL_get_peer_certificate(ssl);
        if (x509 == nullptr)
            throw SalticidaeError(SALTI_ERROR_TLS_GENERIC);
        return X509(x509);
    }

    inline int send(const void *buff, size_t size) {
        return SSL_write(ssl, buff, size);
    }

    inline int recv(void *buff, size_t size) {
        return SSL_read(ssl, buff, size);
    }

    int get_error(int ret) {
        return SSL_get_error(ssl, ret);
    }

    ~TLS() {
        if (ssl)
        {
            SSL_shutdown(ssl);
            SSL_free(ssl);
        }
    }
};

}

#endif
