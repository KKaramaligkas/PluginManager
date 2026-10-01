#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/resume.h"
#include "../src/util.h"
#include "test.h"

int test_failures, test_checks;
static const char *destination = "ms0:/downloads/test.zip";
static const char *url = "https://example.org/releases/1/test.zip";

static void header(resume_state *s, const char *line)
{
    CHECK_INT(resume_header((char *)line, 1, strlen(line), s), strlen(line));
}

static void partial(void)
{
    resume_state s;
    CHECK_INT(resume_open(&s, url, destination), 0);
    resume_discard(&s);
    fs_close(s.file);
    CHECK_INT(resume_open(&s, url, destination), 0);
    header(&s, "HTTP/1.1 200 OK\r\n"); header(&s, "ETag: \"one\"\r\n");
    header(&s, "Content-Length: 6\r\n"); header(&s, "\r\n");
    CHECK_INT(resume_write(&s, "abc", 3), 3);
    CHECK_INT(resume_close(&s, 0), -1);
    CHECK_INT(fs_size(s.partial), 3);
    CHECK(fs_exists(s.metadata));
}

static void test_resume(void)
{
    partial();
    resume_state s;
    CHECK_INT(resume_open(&s, url, destination), 0);
    CHECK_INT(s.offset, 3); CHECK_STR(s.validator, "\"one\"");
    header(&s, "HTTP/1.1 302 Found\r\n"); header(&s, "\r\n");
    CHECK_INT(resume_write(&s, "redirect body", 13), 13);
    header(&s, "HTTP/2 206 Partial Content\r\n");
    header(&s, "Content-Range: bytes 3-5/6\r\n"); header(&s, "ETag: \"one\"\r\n");
    header(&s, "\r\n"); CHECK_INT(resume_write(&s, "def", 3), 3);
    CHECK_INT(resume_close(&s, 1), 0);
    char *text = fs_read_all(destination, NULL, 100);
    CHECK_STR(text, "abcdef"); free(text);
    CHECK(!fs_exists(s.partial)); CHECK(!fs_exists(s.metadata));
}

static void test_invalid_ranges(void)
{
    const char *ranges[] = {"bytes 0-5/6", "bytes 3-5/5", "bytes 3-2/6",
                           "bytes 9223372036854775808-9/10", "bytes 3-5/*"};
    for (int i = 0; i < 5; i++) {
        partial();
        resume_state s;
        char line[128];
        CHECK_INT(resume_open(&s, url, destination), 0);
        header(&s, "HTTP/1.1 206 Partial Content\r\n");
        snprintf(line, sizeof(line), "Content-Range: %s\r\n", ranges[i]); header(&s, line);
        CHECK_INT(resume_header("\r\n", 1, 2, &s), 0);
        CHECK(s.retry_fresh); CHECK_INT(resume_write(&s, "def", 3), 0);
        resume_close(&s, 0);
        CHECK_INT(fs_size(s.partial), 3); resume_discard(&s);
    }
    partial();
    resume_state s;
    CHECK_INT(resume_open(&s, url, destination), 0);
    header(&s, "HTTP/1.1 200 OK\r\n");
    CHECK_INT(resume_header("\r\n", 1, 2, &s), 0); CHECK(s.retry_fresh);
    resume_close(&s, 0); resume_discard(&s);
    partial();
    CHECK_INT(resume_open(&s, url, destination), 0);
    header(&s, "HTTP/1.1 206 Partial Content\r\n");
    header(&s, "Content-Range: bytes 3-5/6\r\n"); header(&s, "ETag: \"two\"\r\n");
    CHECK_INT(resume_header("\r\n", 1, 2, &s), 0); CHECK(s.retry_fresh);
    resume_close(&s, 0); resume_discard(&s);
}

static void test_changed_url_and_weak_validator(void)
{
    partial();
    resume_state s;
    CHECK_INT(resume_open(&s, "https://example.org/releases/2/test.zip", destination), 0);
    CHECK_INT(s.offset, 0); CHECK_INT(fs_size(s.partial), 0);
    header(&s, "HTTP/1.1 200 OK\r\n"); header(&s, "ETag: W/\"one\"\r\n");
    header(&s, "\r\n"); CHECK_INT(resume_write(&s, "abc", 3), 3);
    CHECK_INT(resume_close(&s, 0), -1); CHECK(!fs_exists(s.partial)); CHECK(!fs_exists(s.metadata));
}

static void test_space(void)
{
    resume_state s;
    CHECK_INT(resume_open(&s, url, destination), 0);
    fs_test_free_bytes(10, -2);
    header(&s, "HTTP/1.1 200 OK\r\n"); header(&s, "Content-Length: 100\r\n");
    CHECK_INT(resume_header("\r\n", 1, 2, &s), 0); CHECK(s.space_error);
    CHECK_INT(resume_write(&s, "abc", 3), 0); resume_close(&s, 0);
    fs_test_free_bytes(-2, -2);
}

int main(void)
{
    if (!getenv("PM_FS_ROOT")) return 2;
    CHECK_INT(fs_mkdirs("ms0:/downloads/", NULL, NULL), 0);
    test_resume(); test_invalid_ranges(); test_changed_url_and_weak_validator(); test_space();
    printf("test_resume: %d checks, %d failures\n", test_checks, test_failures);
    return test_failures ? 1 : 0;
}
