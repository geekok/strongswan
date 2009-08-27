/*
 * Copyright (C) 2009 Martin Willi
 * Copyright (C) 2008 Tobias Brunner
 * Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#include "openssl_ec_public_key.h"
#include "openssl_util.h"

#include <debug.h>

#include <openssl/evp.h>
#include <openssl/ecdsa.h>
#include <openssl/x509.h>

typedef struct private_openssl_ec_public_key_t private_openssl_ec_public_key_t;

/**
 * Private data structure with signing context.
 */
struct private_openssl_ec_public_key_t {
	/**
	 * Public interface for this signer.
	 */
	openssl_ec_public_key_t public;
	
	/**
	 * EC key object
	 */
	EC_KEY *ec;
	
	/**
	 * reference counter
	 */
	refcount_t ref;
};

/**
 * Convert a chunk to an ECDSA_SIG (which must already exist). r and s
 * of the signature have to be concatenated in the chunk.
 */
static bool chunk2sig(const EC_GROUP *group, chunk_t chunk, ECDSA_SIG *sig)
{
	return openssl_bn_split(chunk, sig->r, sig->s);
}

/**
 * Verification of a signature as in RFC 4754
 */
static bool verify_signature(private_openssl_ec_public_key_t *this,
								int hash_type, chunk_t data, chunk_t signature)
{
	chunk_t hash = chunk_empty;
	ECDSA_SIG *sig;
	bool valid = FALSE;
	
	if (hash_type == NID_undef)
	{
		hash = data;
	}
	else
	{
		if (!openssl_hash_chunk(hash_type, data, &hash))
		{
			return FALSE;
		}
	}
	
	sig = ECDSA_SIG_new();
	if (!sig)
	{
		goto error;
	}
	
	if (!chunk2sig(EC_KEY_get0_group(this->ec), signature, sig))
	{
		goto error;
	}
	valid = (ECDSA_do_verify(hash.ptr, hash.len, sig, this->ec) == 1);
	
error:
	if (sig)
	{
		ECDSA_SIG_free(sig);
	}
	if (hash_type != NID_undef)
	{
		chunk_free(&hash);
	}
	return valid;
}


/**
 * Verification of the default signature using SHA-1
 */
static bool verify_default_signature(private_openssl_ec_public_key_t *this,
								chunk_t data, chunk_t signature)
{
	bool valid = FALSE;
	chunk_t hash = chunk_empty;
	u_char *p;
	ECDSA_SIG *sig;
	
	/* remove any preceding 0-bytes from signature */
	while (signature.len && *(signature.ptr) == 0x00)
	{
		signature.len -= 1;
		signature.ptr++;
	}
	
	p = signature.ptr;
	sig = d2i_ECDSA_SIG(NULL, (const u_char**)&p, signature.len);
	if (!sig)
	{
		return FALSE;
	}
	
	if (!openssl_hash_chunk(NID_sha1, data, &hash))
	{
		goto error;
	}
	
	valid = (ECDSA_do_verify(hash.ptr, hash.len, sig, this->ec) == 1);

error:
	if (sig)
	{
		ECDSA_SIG_free(sig);
	}
	chunk_free(&hash);
	return valid;
}

/**
 * Implementation of public_key_t.get_type.
 */
static key_type_t get_type(private_openssl_ec_public_key_t *this)
{
	return KEY_ECDSA;
}

/**
 * Implementation of public_key_t.verify.
 */
static bool verify(private_openssl_ec_public_key_t *this, signature_scheme_t scheme, 
				   chunk_t data, chunk_t signature)
{
	switch (scheme)
	{
		case SIGN_ECDSA_WITH_NULL:
			return verify_signature(this, NID_undef, data, signature);
		case SIGN_ECDSA_WITH_SHA1:
			return verify_default_signature(this, data, signature);
		case SIGN_ECDSA_256:
			return verify_signature(this, NID_sha256, data, signature);
		case SIGN_ECDSA_384:
			return verify_signature(this, NID_sha384, data, signature);
		case SIGN_ECDSA_521:
			return verify_signature(this, NID_sha512, data, signature);
		default:
			DBG1("signature scheme %N not supported in EC",
				 signature_scheme_names, scheme);
			return FALSE;
	}
}

/**
 * Implementation of public_key_t.get_keysize.
 */
static bool encrypt_(private_openssl_ec_public_key_t *this,
					 chunk_t crypto, chunk_t *plain)
{
	DBG1("EC public key encryption not implemented");
	return FALSE;
}

/**
 * Implementation of public_key_t.get_keysize.
 */
static size_t get_keysize(private_openssl_ec_public_key_t *this)
{
	return EC_FIELD_ELEMENT_LEN(EC_KEY_get0_group(this->ec));
}

/**
 * Calculate fingerprint from a EC_KEY, also used in ec private key.
 */
bool openssl_ec_fingerprint(EC_KEY *ec, key_encoding_type_t type, chunk_t *fp)
{
	hasher_t *hasher;
	chunk_t key;
	u_char *p;
	
	if (lib->encoding->get_cache(lib->encoding, type, ec, fp))
	{
		return TRUE;
	}
	switch (type)
	{
		case KEY_ID_PUBKEY_SHA1:
			key = chunk_alloc(i2o_ECPublicKey(ec, NULL));
			p = key.ptr;
			i2o_ECPublicKey(ec, &p);
			break;
		case KEY_ID_PUBKEY_INFO_SHA1:
			key = chunk_alloc(i2d_EC_PUBKEY(ec, NULL));
			p = key.ptr;
			i2d_EC_PUBKEY(ec, &p);
			break;
		default:
			return FALSE;
	}
	hasher = lib->crypto->create_hasher(lib->crypto, HASH_SHA1);
	if (!hasher)
	{
		DBG1("SHA1 hash algorithm not supported, fingerprinting failed");
		free(key.ptr);
		return FALSE;
	}
	hasher->allocate_hash(hasher, key, fp);
	hasher->destroy(hasher);
	lib->encoding->cache(lib->encoding, type, ec, *fp);
	return TRUE;
}

/**
 * Implementation of private_key_t.get_fingerprint.
 */
static bool get_fingerprint(private_openssl_ec_public_key_t *this,
							key_encoding_type_t type, chunk_t *fingerprint)
{
	return openssl_ec_fingerprint(this->ec, type, fingerprint);
}

/**
 * Implementation of private_key_t.get_encoding.
 */
static bool get_encoding(private_openssl_ec_public_key_t *this,
						 key_encoding_type_t type, chunk_t *encoding)
{
	u_char *p;
	
	switch (type)
	{
		case KEY_PUB_SPKI_ASN1_DER:
		{
			*encoding = chunk_alloc(i2d_EC_PUBKEY(this->ec, NULL));
			p = encoding->ptr;
			i2d_EC_PUBKEY(this->ec, &p);
			return TRUE;
		}
		default:
			return FALSE;
	}
}

/**
 * Implementation of public_key_t.get_ref.
 */
static public_key_t* get_ref(private_openssl_ec_public_key_t *this)
{
	ref_get(&this->ref);
	return &this->public.interface;
}

/**
 * Implementation of openssl_ec_public_key.destroy.
 */
static void destroy(private_openssl_ec_public_key_t *this)
{
	if (ref_put(&this->ref))
	{
		if (this->ec)
		{
			lib->encoding->clear_cache(lib->encoding, this->ec);
			EC_KEY_free(this->ec);
		}
		free(this);
	}
}

/**
 * Generic private constructor
 */
static private_openssl_ec_public_key_t *create_empty()
{
	private_openssl_ec_public_key_t *this = malloc_thing(private_openssl_ec_public_key_t);
	
	this->public.interface.get_type = (key_type_t (*)(public_key_t *this))get_type;
	this->public.interface.verify = (bool (*)(public_key_t *this, signature_scheme_t scheme, chunk_t data, chunk_t signature))verify;
	this->public.interface.encrypt = (bool (*)(public_key_t *this, chunk_t crypto, chunk_t *plain))encrypt_;
	this->public.interface.get_keysize = (size_t (*) (public_key_t *this))get_keysize;
	this->public.interface.equals = public_key_equals;
	this->public.interface.get_fingerprint = (bool(*)(public_key_t*, key_encoding_type_t type, chunk_t *fp))get_fingerprint;
	this->public.interface.get_encoding = (bool(*)(public_key_t*, key_encoding_type_t type, chunk_t *encoding))get_encoding;
	this->public.interface.get_ref = (public_key_t* (*)(public_key_t *this))get_ref;
	this->public.interface.destroy = (void (*)(public_key_t *this))destroy;
	
	this->ec = NULL;
	this->ref = 1;
	
	return this;
}

/**
 * Load a public key from an ASN1 encoded blob
 */
static openssl_ec_public_key_t *load(chunk_t blob)
{
	private_openssl_ec_public_key_t *this = create_empty();
	u_char *p = blob.ptr;
	
	this->ec = d2i_EC_PUBKEY(NULL, (const u_char**)&p, blob.len);
	
	if (!this->ec)
	{
		destroy(this);
		return NULL;
	}
	return &this->public;
}

typedef struct private_builder_t private_builder_t;

/**
 * Builder implementation for key loading
 */
struct private_builder_t {
	/** implements the builder interface */
	builder_t public;
	/** loaded public key */
	openssl_ec_public_key_t *key;
};

/**
 * Implementation of builder_t.build
 */
static openssl_ec_public_key_t *build(private_builder_t *this)
{
	openssl_ec_public_key_t *key = this->key;
	
	free(this);
	return key;
}

/**
 * Implementation of builder_t.add
 */
static void add(private_builder_t *this, builder_part_t part, ...)
{
	if (!this->key)
	{
		va_list args;
		
		switch (part)
		{
			case BUILD_BLOB_ASN1_DER:
			{
				va_start(args, part);
				this->key = load(va_arg(args, chunk_t));
				va_end(args);
				return;
			}
			default:
				break;
		}
	}
	if (this->key)
	{
		destroy((private_openssl_ec_public_key_t*)this->key);
	}
	builder_cancel(&this->public);
}

/**
 * Builder construction function
 */
builder_t *openssl_ec_public_key_builder(key_type_t type)
{
	private_builder_t *this;
	
	if (type != KEY_ECDSA)
	{
		return NULL;
	}
	
	this = malloc_thing(private_builder_t);
	
	this->key = NULL;
	this->public.add = (void(*)(builder_t *this, builder_part_t part, ...))add;
	this->public.build = (void*(*)(builder_t *this))build;
	
	return &this->public;
}

