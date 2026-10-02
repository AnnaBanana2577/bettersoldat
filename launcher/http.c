#include "http.h"

#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "sha256.h"

#ifndef SOLDATRELOADED_VERSION
#define SOLDATRELOADED_VERSION "dev"
#endif

static const char *ca_bundle;
static char agent[96] = "soldatreloaded-launcher/" SOLDATRELOADED_VERSION;

void http_set_agent(const char *text) { snprintf(agent, sizeof agent, "%s", text); }

bool http_init(void)
{
#ifndef _WIN32
    // Where the distributions keep theirs: Debian and Ubuntu, Fedora and RHEL, openSUSE,
    // Alpine and Arch (the first), and the older ones.
    static const char *bundles[] = {
        "/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/ca-bundle.pem", "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem",
        "/etc/ssl/cert.pem", "/etc/pki/tls/cacert.pem",
    };
    for (size_t i = 0; i < sizeof bundles / sizeof bundles[0] && !ca_bundle; i++)
        if (files_exists(bundles[i])) ca_bundle = bundles[i];
#endif
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
}

void http_cleanup(void) { curl_global_cleanup(); }

static CURL *open_request(const char *url, char *curl_error)
{
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    // a stalled transfer is given up after half a minute under 1 KB/s
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, agent);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error);
#ifdef _WIN32
    // Schannel fails outright where a revocation list can't be reached (behind some
    // proxies); what is downloaded is checked against the manifest's hashes anyway.
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, (long)CURLSSLOPT_REVOKE_BEST_EFFORT);
#endif
    if (ca_bundle) curl_easy_setopt(curl, CURLOPT_CAINFO, ca_bundle);
    curl_error[0] = '\0';
    return curl;
}

static HttpResult finish(CURL *curl, CURLcode code, const char *curl_error, char *error, size_t error_size)
{
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    if (code != CURLE_OK) {
        snprintf(error, error_size, "%s", curl_error[0] ? curl_error : curl_easy_strerror(code));
        return HTTP_FAILED;
    }
    if (status == 0 || (status >= 200 && status < 300)) return HTTP_OK; // 0: file://, which has no status
    if (status == 404) return HTTP_NOT_FOUND;
    snprintf(error, error_size, "the server answered %ld", status);
    return HTTP_FAILED;
}

// --- into memory -----------------------------------------------------------------------

typedef struct Body {
    char *data;
    size_t size, max;
    CURL *curl;
} Body;

static size_t body_write(char *data, size_t one, size_t n, void *user)
{
    Body *b = user;
    long status = 0;
    curl_easy_getinfo(b->curl, CURLINFO_RESPONSE_CODE, &status);
    if (status >= 300) return n; // an error page: read and dropped
    if (b->size + n > b->max) return 0;
    char *grown = realloc(b->data, b->size + n + 1);
    if (!grown) return 0;
    b->data = grown;
    memcpy(b->data + b->size, data, n);
    b->size += n;
    b->data[b->size] = '\0';
    (void)one;
    return n;
}

HttpResult http_get(const char *url, size_t max, char **body, size_t *size, char *error, size_t error_size)
{
    char curl_error[CURL_ERROR_SIZE];
    CURL *curl = open_request(url, curl_error);
    if (!curl) {
        snprintf(error, error_size, "curl couldn't start");
        return HTTP_FAILED;
    }
    Body b = {.max = max, .curl = curl};
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, body_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    HttpResult result = finish(curl, curl_easy_perform(curl), curl_error, error, error_size);
    if (result == HTTP_OK && !b.data) b.data = calloc(1, 1);
    if (result != HTTP_OK || !b.data) {
        free(b.data);
        return result == HTTP_OK ? HTTP_FAILED : result;
    }
    *body = b.data;
    *size = b.size;
    return HTTP_OK;
}

// Any answer's body, an error page's too, as far as `max` (cut, not failed, past it).
static size_t answer_write(char *data, size_t one, size_t n, void *user)
{
    Body *b = user;
    size_t take = b->size + n > b->max ? b->max - b->size : n;
    if (take) {
        char *grown = realloc(b->data, b->size + take + 1);
        if (!grown) return 0;
        b->data = grown;
        memcpy(b->data + b->size, data, take);
        b->size += take;
        b->data[b->size] = '\0';
    }
    (void)one;
    return n;
}

HttpResult http_request(const char *method, const char *url, const char *body, bool ipv4, long timeout, size_t max,
                        long *status, char **response, char *error, size_t error_size)
{
    char curl_error[CURL_ERROR_SIZE];
    *status = 0;
    *response = NULL;
    CURL *curl = open_request(url, curl_error);
    if (!curl) {
        snprintf(error, error_size, "curl couldn't start");
        return HTTP_FAILED;
    }
    Body b = {.max = max, .curl = curl};
    struct curl_slist *headers = NULL;
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    if (body) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)strlen(body));
    }
    if (ipv4) curl_easy_setopt(curl, CURLOPT_IPRESOLVE, (long)CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, answer_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &b);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
    CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, status);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    if (code != CURLE_OK) {
        free(b.data);
        snprintf(error, error_size, "%s", curl_error[0] ? curl_error : curl_easy_strerror(code));
        return HTTP_FAILED;
    }
    *response = b.data ? b.data : calloc(1, 1);
    return *response ? HTTP_OK : HTTP_FAILED;
}

// --- into a file -----------------------------------------------------------------------

typedef struct Download {
    FILE *out;
    Sha256 hash;
    uint64_t size;
    CURL *curl;
    bool failed_write;
    HttpProgress progress;
    void *user;
} Download;

static size_t download_write(char *data, size_t one, size_t n, void *user)
{
    Download *d = user;
    long status = 0;
    curl_easy_getinfo(d->curl, CURLINFO_RESPONSE_CODE, &status);
    if (status >= 300) return n;
    if (fwrite(data, 1, n, d->out) != n) {
        d->failed_write = true;
        return 0;
    }
    sha256_feed(&d->hash, data, n);
    d->size += n;
    (void)one;
    return n;
}

static int download_progress(void *user, curl_off_t total, curl_off_t done, curl_off_t up_total, curl_off_t up_done)
{
    Download *d = user;
    if (d->progress) d->progress(d->user, (uint64_t)done, total > 0 ? (uint64_t)total : 0);
    (void)up_total;
    (void)up_done;
    return 0;
}

HttpResult http_download(const char *url, const char *path, uint8_t sha256[32], uint64_t *size, HttpProgress progress,
                         void *user, char *error, size_t error_size)
{
    char curl_error[CURL_ERROR_SIZE];
    Download d = {.progress = progress, .user = user};
    if (!files_make_parents(path) || !(d.out = fopen(path, "wb"))) {
        snprintf(error, error_size, "%s can't be written", path);
        return HTTP_FAILED;
    }
    CURL *curl = d.curl = open_request(url, curl_error);
    if (!curl) {
        fclose(d.out);
        snprintf(error, error_size, "curl couldn't start");
        return HTTP_FAILED;
    }
    sha256_init(&d.hash);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, download_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &d);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, download_progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &d);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    HttpResult result = finish(curl, curl_easy_perform(curl), curl_error, error, error_size);
    bool closed = fclose(d.out) == 0;
    if (result == HTTP_OK && (d.failed_write || !closed)) {
        snprintf(error, error_size, "%s can't be written (is the disk full?)", path);
        result = HTTP_FAILED;
    }
    if (result != HTTP_OK) {
        remove(path);
        return result;
    }
    sha256_finish(&d.hash, sha256);
    *size = d.size;
    return HTTP_OK;
}
