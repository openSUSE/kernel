/*
 * SHA transform algorithm, taken from code written by Peter Gutmann,
 * and placed in the public domain.
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/cryptohash.h>

/* The SHA f()-functions.  */

#define f1(x,y,z)   (z ^ (x & (y ^ z)))		/* Rounds  0-19: x ? y : z */
#define f2(x,y,z)   (x ^ y ^ z)			/* Rounds 20-39: XOR */
#define f3(x,y,z)   ((x & y) + (z & (x ^ y)))	/* Rounds 40-59: majority */
#define f4(x,y,z)   (x ^ y ^ z)			/* Rounds 60-79: XOR */

/* The SHA Mysterious Constants */

#define K1  0x5A827999L			/* Rounds  0-19: sqrt(2) * 2^30 */
#define K2  0x6ED9EBA1L			/* Rounds 20-39: sqrt(3) * 2^30 */
#define K3  0x8F1BBCDCL			/* Rounds 40-59: sqrt(5) * 2^30 */
#define K4  0xCA62C1D6L			/* Rounds 60-79: sqrt(10) * 2^30 */

/*
 * sha_transform: single block SHA1 transform
 *
 * @digest: 160 bit digest to update
 * @data:   512 bits of data to hash
 * @W:      80 words of workspace
 *
 * This function generates a SHA1 digest for a single. Be warned, it
 * does not handle padding and message digest, do not confuse it with
 * the full FIPS 180-1 digest algorithm for variable length messages.
 */
void sha_transform(__u32 *digest, const char *data, __u32 *W)
{
	__u32 A, B, C, D, E;
	__u32 TEMP;
	int i;

	memset(W, 0, sizeof(W));
	for (i = 0; i < 16; i++)
		W[i] = be32_to_cpu(((const __u32 *)data)[i]);
	/*
	 * Do the preliminary expansion of 16 to 80 words.  Doing it
	 * out-of-line line this is faster than doing it in-line on
	 * register-starved machines like the x86, and not really any
	 * slower on real processors.
	 */
	for (i = 0; i < 64; i++) {
		TEMP = W[i] ^ W[i+2] ^ W[i+8] ^ W[i+13];
		W[i+16] = rol32(TEMP, 1);
	}

	/* Set up first buffer and local data buffer */
	A = digest[ 0 ];
	B = digest[ 1 ];
	C = digest[ 2 ];
	D = digest[ 3 ];
	E = digest[ 4 ];

	/* Heavy mangling, in 4 sub-rounds of 20 iterations each. */
	for (i = 0; i < 80; i++) {
		if (i < 40) {
			if (i < 20)
				TEMP = f1(B, C, D) + K1;
			else
				TEMP = f2(B, C, D) + K2;
		} else {
			if (i < 60)
				TEMP = f3(B, C, D) + K3;
			else
				TEMP = f4(B, C, D) + K4;
		}
		TEMP += rol32(A, 5) + E + W[i];
		E = D; D = C; C = rol32(B, 30); B = A; A = TEMP;
	}

	/* Build message digest */
	digest[0] += A;
	digest[1] += B;
	digest[2] += C;
	digest[3] += D;
	digest[4] += E;

	/* W is wiped by the caller */
}

/*
 * sha_init: initialize the vectors for a SHA1 digest
 *
 * @buf: vector to initialize
 */
void sha_init(__u32 *buf)
{
	buf[0] = 0x67452301;
	buf[1] = 0xefcdab89;
	buf[2] = 0x98badcfe;
	buf[3] = 0x10325476;
	buf[4] = 0xc3d2e1f0;
}

