#include<uuid/uuid.h>
#include<openssl/sha.h>
#include<openssl/rsa.h>
#include<openssl/hmac.h>
#include<ctype.h>

char* kik_timestamp() {
    unsigned long j = time(NULL);
    unsigned long i1 = (-16777216 & j) >> 24;
    unsigned long i2 = (16711680 & j) >> 16;
    unsigned long i3 = (65280 & j) >> 8;

    unsigned long j2 = (30 & i1) ^ i2 ^ i3;
    unsigned long j3 = (224 & j) >> 5;
    unsigned long j4 = -255 & j;

    if(j2 % 4 == 0) {
        j3 = j3 / 3 * 3;
    } else {
        j3 = j3 / 2 * 2;
    }

	char* timestamp = malloc(17);
	memset(timestamp, 0, 17);
    snprintf(timestamp, 16, "%lu", (j4 | (j3 << 5) | j2));
	return timestamp;
}

char* kik_uuid() {
    uuid_t uuid;
    uuid_generate(uuid);
    char* uuid_lowercase = malloc(37);
    uuid_unparse_lower(uuid, uuid_lowercase);
    return uuid_lowercase;
}

char* kik_passkey() {
    // Sha1 hash password, convert the bytes to hexadecimal
    unsigned char sha1_password_bin[21];
    sha1_password_bin[20] = '\0';
    char sha1_password_hex[41];
    sha1_password_hex[40] = '\0';
    
    if(SHA1(Ckik_password, strlen(Ckik_password), sha1_password_bin) == NULL) {
        Ckik_error("SHA1", __FILE__, __LINE__);
        return NULL;
    }
    for(int i=0;i<20;i++) {
        sprintf(&sha1_password_hex[i*2], "%02x", sha1_password_bin[i]);
    }

    // Prepare salt for pbkdf2 (lowercase username + constant string)

    char lowercase_username[strlen(Ckik_username)+1];
    lowercase_username[strlen(Ckik_username)] = '\0';
    for(int i=0;i<strlen(Ckik_username);i++) {
            lowercase_username[i] = tolower(Ckik_username[i]);
    }
    char salt[128];
    memset(salt, 0, 128);
    strcpy(salt, lowercase_username);
    strcat(salt, "niCRwL7isZHny24qgLvy");

    // PBKDF2 
    unsigned char pbkdf2_bin[17];
    pbkdf2_bin[16] = '\0';
    if(PKCS5_PBKDF2_HMAC_SHA1(sha1_password_hex, strlen(sha1_password_hex), (unsigned char*)salt, strlen(salt), 8192, 16, pbkdf2_bin) == 0) {
        Ckik_error("PKCS5_PBKDF2_HMAC_SHA1", __FILE__, __LINE__);
        return NULL;
    }
    char* passkey = malloc(33);
	passkey[32] = '\0';
    // convert finalkey to hex
    for(int i=0;i<16;i++) {
        sprintf((char*)&passkey[i*2], "%02x", pbkdf2_bin[i]);
    }
    return passkey;
}

unsigned char* kik_rsa(char* jid, char* timestamp, char* uuid) {
    /*************************
    * SHA256 *****************
    **************************/
    char data[strlen(jid) + strlen(Ckik_version) + strlen(timestamp) + strlen(uuid) + 1];
    sprintf(data, "%s:%s:%s:%s", jid, Ckik_version, timestamp, uuid);
    unsigned char* sha256_data = SHA256((const unsigned char*)data, strlen(data), NULL);
	if(sha256_data == NULL) {
		Ckik_error("SHA256", __FILE__, __LINE__);
		return NULL;
	}
    /**************************
    * RSA sign ****************
    ***************************/
    char* PrivateKey_str = "-----BEGIN RSA PRIVATE KEY-----\nMIIBPAIBAAJBANEWUEINqV1KNG7Yie9GSM8t75ZvdTeqT7kOF40kvDHIp/C3tX2bcNgLTnGFs8yA2m2p7hKoFLoxh64vZx5fZykCAwEAAQJAT/hC1iC3iHDbQRIdH6E4M9WT72vN326Kc3MKWveT603sUAWFlaEa5T80GBiP/qXt9PaDoJWcdKHr7RqDq+8noQIhAPh5haTSGu0MFs0YiLRLqirJWXa4QPm4W5nz5VGKXaKtAiEA12tpUlkyxJBuuKCykIQbiUXHEwzFYbMHK5E/uGkFoe0CIQC6uYgHPqVhcm5IHqHM6/erQ7jpkLmzcCnWXgT87ABF2QIhAIzrfyKXp1ZfBY9R0H4pbboHI4uatySKcQ5XHlAMo9qhAiEA43zuIMknJSGwa2zLt/3FmVnuCInD6Oun5dbcYnqraJo=\n-----END RSA PRIVATE KEY-----";
    BIO* rsa_bio = BIO_new(BIO_s_mem());
    BIO_write(rsa_bio, PrivateKey_str, strlen(PrivateKey_str));

    EVP_PKEY* pkey = NULL;
    PEM_read_bio_PrivateKey(rsa_bio, &pkey, NULL, NULL);
    BIO_free(rsa_bio);

    RSA* rsa = EVP_PKEY_get1_RSA(pkey);
	if(rsa == NULL) {
		Ckik_error("EVP_PKEY_get1_RSA", __FILE__, __LINE__);
		free(sha256_data);
		return NULL;
	}
	unsigned char* sigret = malloc(RSA_size(rsa));
	unsigned int* siglen = malloc(sizeof(unsigned int));
	if(RSA_sign(NID_sha256, sha256_data, 32, sigret, siglen, rsa) == 0) {
		Ckik_error("RSA_sign", __FILE__, __LINE__);
		EVP_PKEY_free(pkey);
		RSA_free(rsa);
		return NULL;
	}
	RSA_free(rsa);
	/**************************
	* Base64 signature ********
	***************************/
	unsigned char* out = malloc((((*siglen)+1 / 3) + (*siglen)+1 )+1);
	if(EVP_EncodeBlock(out, sigret, *siglen) <= 0) {
		Ckik_error("EVP_EncodeBlock", __FILE__, __LINE__);
		EVP_PKEY_free(pkey);
		free(sigret);
		free(siglen);
		return NULL;
	}
	for(int i=0;out[i] != '\0';i++) {
		switch(out[i]) {
			case '+':
				out[i] = '-';
				break;
			case '/':
				out[i] = '_';
				break;
		}
	}
    EVP_PKEY_free(pkey);
	free(sigret);
	free(siglen);
	return out;
}

unsigned char* build_hmac_key() {
	unsigned char apk_signature[] = { 0x30, 0x82, 0x03, 0x84, 0x30, 0x82, 0x02, 0x6C, 0xA0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x04, 0x4C, 0x23, 0xD6, 0x25, 0x30, 0x0D, 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x05, 0x05, 0x00, 0x30, 0x81, 0x83, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x43, 0x41, 0x31, 0x10, 0x30, 0x0E, 0x06, 0x03, 0x55, 0x04, 0x08, 0x13, 0x07, 0x4F, 0x6E, 0x74, 0x61, 0x72, 0x69, 0x6F, 0x31, 0x11, 0x30, 0x0F, 0x06, 0x03, 0x55, 0x04, 0x07, 0x13, 0x08, 0x57, 0x61, 0x74, 0x65, 0x72, 0x6C, 0x6F, 0x6F, 0x31, 0x1D, 0x30, 0x1B, 0x06, 0x03, 0x55, 0x04, 0x0A, 0x13, 0x14, 0x4B, 0x69, 0x6B, 0x20, 0x49, 0x6E, 0x74, 0x65, 0x72, 0x61, 0x63, 0x74, 0x69, 0x76, 0x65, 0x20, 0x49, 0x6E, 0x63, 0x2E, 0x31, 0x1B, 0x30, 0x19, 0x06, 0x03, 0x55, 0x04, 0x0B, 0x13, 0x12, 0x4D, 0x6F, 0x62, 0x69, 0x6C, 0x65, 0x20, 0x44, 0x65, 0x76, 0x65, 0x6C, 0x6F, 0x70, 0x6D, 0x65, 0x6E, 0x74, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x0A, 0x43, 0x68, 0x72, 0x69, 0x73, 0x20, 0x42, 0x65, 0x73, 0x74, 0x30, 0x1E, 0x17, 0x0D, 0x31, 0x30, 0x30, 0x36, 0x32, 0x34, 0x32, 0x32, 0x30, 0x33, 0x31, 0x37, 0x5A, 0x17, 0x0D, 0x33, 0x37, 0x31, 0x31, 0x30, 0x39, 0x32, 0x32, 0x30, 0x33, 0x31, 0x37, 0x5A, 0x30, 0x81, 0x83, 0x31, 0x0B, 0x30, 0x09, 0x06, 0x03, 0x55, 0x04, 0x06, 0x13, 0x02, 0x43, 0x41, 0x31, 0x10, 0x30, 0x0E, 0x06, 0x03, 0x55, 0x04, 0x08, 0x13, 0x07, 0x4F, 0x6E, 0x74, 0x61, 0x72, 0x69, 0x6F, 0x31, 0x11, 0x30, 0x0F, 0x06, 0x03, 0x55, 0x04, 0x07, 0x13, 0x08, 0x57, 0x61, 0x74, 0x65, 0x72, 0x6C, 0x6F, 0x6F, 0x31, 0x1D, 0x30, 0x1B, 0x06, 0x03, 0x55, 0x04, 0x0A, 0x13, 0x14, 0x4B, 0x69, 0x6B, 0x20, 0x49, 0x6E, 0x74, 0x65, 0x72, 0x61, 0x63, 0x74, 0x69, 0x76, 0x65, 0x20, 0x49, 0x6E, 0x63, 0x2E, 0x31, 0x1B, 0x30, 0x19, 0x06, 0x03, 0x55, 0x04, 0x0B, 0x13, 0x12, 0x4D, 0x6F, 0x62, 0x69, 0x6C, 0x65, 0x20, 0x44, 0x65, 0x76, 0x65, 0x6C, 0x6F, 0x70, 0x6D, 0x65, 0x6E, 0x74, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x03, 0x13, 0x0A, 0x43, 0x68, 0x72, 0x69, 0x73, 0x20, 0x42, 0x65, 0x73, 0x74, 0x30, 0x82, 0x01, 0x22, 0x30, 0x0D, 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01, 0x05, 0x00, 0x03, 0x82, 0x01, 0x0F, 0x00, 0x30, 0x82, 0x01, 0x0A, 0x02, 0x82, 0x01, 0x01, 0x00, 0xE2, 0xB9, 0x4E, 0x55, 0x61, 0xE9, 0xA2, 0x37, 0x8B, 0x65, 0x7E, 0x66, 0x50, 0x78, 0x09, 0xFB, 0x8E, 0x58, 0xD9, 0xFB, 0xDC, 0x35, 0xAD, 0x2A, 0x23, 0x81, 0xB8, 0xD4, 0xB5, 0x1F, 0xCF, 0x50, 0x36, 0x04, 0x82, 0xEC, 0xB3, 0x16, 0x77, 0xBD, 0x95, 0x05, 0x4F, 0xAA, 0xEC, 0x86, 0x4D, 0x60, 0xE2, 0x33, 0xBF, 0xE6, 0xB4, 0xC7, 0x60, 0x32, 0xE5, 0x54, 0x0E, 0x5B, 0xC1, 0x95, 0xEB, 0xF5, 0xFF, 0x9E, 0xDF, 0xE3, 0xD9, 0x9D, 0xAE, 0x8C, 0xA9, 0xA5, 0x26, 0x6F, 0x36, 0x40, 0x4E, 0x8A, 0x9F, 0xCD, 0xF2, 0xB0, 0x96, 0x05, 0xB0, 0x89, 0x15, 0x9A, 0x0F, 0xFD, 0x40, 0x46, 0xEC, 0x71, 0xAA, 0x11, 0xC7, 0x63, 0x9E, 0x2A, 0xE0, 0xD5, 0xC3, 0xE1, 0xC2, 0xBA, 0x8C, 0x21, 0x60, 0xAF, 0xA3, 0x0E, 0xC8, 0xA0, 0xCE, 0x4A, 0x77, 0x64, 0xF2, 0x8B, 0x9A, 0xE1, 0xAD, 0x3C, 0x86, 0x7D, 0x12, 0x8B, 0x9E, 0xAF, 0x02, 0xEF, 0x0B, 0xF6, 0x0E, 0x29, 0x92, 0xE7, 0x5A, 0x0D, 0x4C, 0x26, 0x64, 0xDA, 0x99, 0xAC, 0x23, 0x06, 0x24, 0xB3, 0x0C, 0xEA, 0x37, 0x88, 0xB2, 0x3F, 0x5A, 0xBB, 0x61, 0x17, 0x3D, 0xB4, 0x76, 0xF0, 0xA7, 0xCF, 0x26, 0x16, 0x0B, 0x8C, 0x51, 0xDE, 0x09, 0x70, 0xC6, 0x32, 0x79, 0xA6, 0xBF, 0x5D, 0xEF, 0x11, 0x6A, 0x70, 0x09, 0xCA, 0x60, 0xE8, 0xA9, 0x5F, 0x46, 0x75, 0x9D, 0xD0, 0x1D, 0x91, 0xEF, 0xCC, 0x67, 0x0A, 0x46, 0x71, 0x66, 0xA9, 0xD6, 0x28, 0x5F, 0x63, 0xF8, 0x62, 0x6E, 0x87, 0xFB, 0xE8, 0x3A, 0x03, 0xDA, 0x70, 0x44, 0xAC, 0xDD, 0x82, 0x6B, 0x96, 0x2C, 0x26, 0xE6, 0x27, 0xAB, 0x11, 0x05, 0x92, 0x5C, 0x74, 0xFE, 0xB7, 0x77, 0x43, 0xC1, 0x3D, 0xDD, 0x29, 0xB5, 0x5B, 0x31, 0x08, 0x3F, 0x5C, 0xF3, 0x8F, 0xC2, 0x92, 0x42, 0x39, 0x02, 0x03, 0x01, 0x00, 0x01, 0x30, 0x0D, 0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x05, 0x05, 0x00, 0x03, 0x82, 0x01, 0x01, 0x00, 0x9F, 0x89, 0xDD, 0x38, 0x49, 0x26, 0x76, 0x48, 0x54, 0xA4, 0xA6, 0x41, 0x3B, 0xA9, 0x81, 0x38, 0xCC, 0xE5, 0xAD, 0x96, 0xBF, 0x1F, 0x48, 0x30, 0x60, 0x2C, 0xE8, 0x4F, 0xEA, 0xDD, 0x19, 0xC1, 0x5B, 0xAD, 0x83, 0x13, 0x0B, 0x65, 0xDC, 0x4A, 0x3B, 0x7C, 0x8D, 0xE8, 0x96, 0x8A, 0xCA, 0x5C, 0xDF, 0x89, 0x20, 0x0D, 0x6A, 0xCF, 0x2E, 0x75, 0x30, 0x54, 0x6A, 0x0E, 0xE2, 0xBC, 0xF1, 0x9F, 0x67, 0x34, 0x0B, 0xE8, 0xA7, 0x37, 0x77, 0x83, 0x67, 0x28, 0x84, 0x6F, 0xAD, 0x7F, 0x31, 0xA3, 0xC4, 0xEE, 0xAD, 0x16, 0x08, 0x1B, 0xED, 0x28, 0x8B, 0xB0, 0xF0, 0xFD, 0xC7, 0x35, 0x88, 0x0E, 0xBD, 0x86, 0x34, 0xC9, 0xFC, 0xA3, 0xA6, 0xC5, 0x05, 0xCE, 0xA3, 0x55, 0xBD, 0x91, 0x50, 0x22, 0x26, 0xE1, 0x77, 0x8E, 0x96, 0xB0, 0xC6, 0x7D, 0x6A, 0x3C, 0x3F, 0x79, 0xDE, 0x6F, 0x59, 0x44, 0x29, 0xF2, 0xB6, 0xA0, 0x35, 0x91, 0xC0, 0xA0, 0x1C, 0x3F, 0x14, 0xBB, 0x6F, 0xF5, 0x6D, 0x75, 0x15, 0xBB, 0x2F, 0x38, 0xF6, 0x4A, 0x00, 0xFF, 0x07, 0x83, 0x4E, 0xD3, 0xA0, 0x6D, 0x70, 0xC3, 0x8F, 0xC1, 0x80, 0x04, 0xF8, 0x5C, 0xAB, 0x3C, 0x93, 0x7D, 0x3F, 0x94, 0xB3, 0x66, 0xE2, 0x55, 0x25, 0x58, 0x92, 0x9B, 0x98, 0xD0, 0x88, 0xCF, 0x1C, 0x45, 0xCD, 0xC0, 0x34, 0x07, 0x55, 0xE4, 0x30, 0x56, 0x98, 0xA7, 0x06, 0x7F, 0x69, 0x6F, 0x4E, 0xCF, 0xCE, 0xEA, 0xFB, 0xD7, 0x20, 0x78, 0x75, 0x37, 0x19, 0x9B, 0xCA, 0xC6, 0x74, 0xDA, 0xB5, 0x46, 0x43, 0x35, 0x9B, 0xAD, 0x3E, 0x22, 0x9D, 0x58, 0x8E, 0x32, 0x49, 0x41, 0x94, 0x1E, 0x02, 0x70, 0xC3, 0x55, 0xDC, 0x38, 0xF9, 0x56, 0x04, 0x69, 0xB4, 0x52, 0xC3, 0x65, 0x60, 0xAD, 0x5A, 0xB9, 0x61, 0x9B, 0x6E, 0xB3, 0x37, 0x05 };

	int source_bytes_len = strlen("hello") + 904  + strlen(Ckik_version) + strlen(Ckik_classes_dex_sha1_digest) + strlen("bar"); 
	unsigned char source_bytes[source_bytes_len];
    strcpy(source_bytes, "hello");
    for(int i=0;i<904;i++) {
        source_bytes[i+5] = apk_signature[i];
    }
    sprintf(&source_bytes[909], "%s%sbar", Ckik_version, Ckik_classes_dex_sha1_digest);	
	
	// Sha1 source bytes
	unsigned char sha1_source_bytes[20];
	sha1_source_bytes[0] = '\0';
	SHA1(source_bytes, source_bytes_len, sha1_source_bytes);
	if(sha1_source_bytes == NULL) {
		Ckik_error("SHA1", __FILE__, __LINE__);
		return NULL;
	}
	// Base64 encode the hash
	unsigned char* out = malloc(32);
	if(EVP_EncodeBlock(out, sha1_source_bytes, 20) <= 0) {
		Ckik_error("EVP_EncodeBlock", __FILE__, __LINE__);
		return NULL;
	}
	return out;
}

char* kik_hmac(char* timestamp, char* jid) {
	char data[strlen(timestamp) + strlen(jid) + 1];
	sprintf(data, "%s:%s", timestamp, jid);
	unsigned char* key = build_hmac_key();
	if(key == NULL) {
		Ckik_error("build_hmac_key", __FILE__, __LINE__);
		return NULL;
	}
	unsigned int hmac_bin_len;
	unsigned char* hmac_bin = HMAC(EVP_sha1(), key, strlen(key), data, strlen(data), NULL, &hmac_bin_len);
	if(hmac_bin == NULL) {
		Ckik_error("HMAC", __FILE__, __LINE__);
		return NULL;
	}
	free(key);
    char* hmac_hex = malloc((hmac_bin_len * 2)+1);
    hmac_hex[hmac_bin_len * 2] = '\0';
    // convert hmac_bin to hex
    for(int i=0;i<hmac_bin_len;i++) {
        sprintf(&hmac_hex[i*2], "%02x", hmac_bin[i]);
    }
	return hmac_hex;
}