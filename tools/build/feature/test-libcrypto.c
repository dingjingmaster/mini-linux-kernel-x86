// SPDX-License-Identifier: GPL-2.0
#include <openssl/sha.h>
#include <openssl/md5.h>

/*
 * The MD5_* API have been deprecated since OpenSSL 3.0, which causes the
 * feature test to fail silently. This is a workaround.
 */
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

int main(void)
{
	MD5_CTX context;
	unsigned char md[MD5_DIGEST_LENGTH + SHA_DIGEST_LENGTH];
	unsigned char dat[] = "12345";

	MD5_Init(&context);
	MD5_Update(&context, &dat[0], sizeof(dat));
	MD5_Final(&md[0], &context);

	SHA1(&dat[0], sizeof(dat), &md[0]);

	return 0;
}
