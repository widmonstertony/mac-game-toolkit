/* Local owned-package extraction. No key material is logged or embedded. */
#include <openssl/evp.h>
#include <stdint.h>
#include <stddef.h>

int decrypt_sectors(const unsigned char *key, uint64_t tweak,
                    const unsigned char *input, unsigned char *output, size_t size) {
    if (size % 512) return 0;
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return 0;
    int ok = 1;
    for (size_t off = 0; off < size && ok; off += 512) {
        unsigned char iv[16] = {0};
        uint64_t low = tweak + off / 512;
        uint64_t high = low < tweak;
        for (int i = 0; i < 8; ++i) {
            iv[i] = (unsigned char)(low >> (i * 8));
            iv[i + 8] = (unsigned char)(high >> (i * 8));
        }
        int written = 0, final = 0;
        ok = EVP_DecryptInit_ex(ctx, EVP_aes_128_xts(), NULL, key, iv) == 1
          && EVP_DecryptUpdate(ctx, output + off, &written, input + off, 512) == 1
          && written == 512
          && EVP_DecryptFinal_ex(ctx, output + off + written, &final) == 1
          && final == 0;
    }
    EVP_CIPHER_CTX_free(ctx);
    return ok;
}
